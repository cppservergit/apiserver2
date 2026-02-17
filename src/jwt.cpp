#include "jwt.hpp"
#include "json_parser.hpp"
#include "env.hpp"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/crypto.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <vector>
#include <stdexcept>
#include <array>
#include <cassert>
#include <memory>
#include <mutex>
#include <expected>

using namespace json;

namespace jwt {

// --- Implementation of detail::service and helpers ---
namespace detail {
namespace { // Anonymous namespace for private helpers
    
    using sha256_digest = std::array<unsigned char, 32>;

    std::string base64url_encode(std::string_view data) {
        std::string buffer( ((data.length() + 2) / 3) * 4, '\0');
        int len = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(buffer.data()), (const unsigned char*)data.data(), data.length());
        buffer.resize(len);
        for (char& c : buffer) {
            if (c == '+') c = '-';
            else if (c == '/') c = '_';
        }
        if (auto pos = buffer.find('='); pos != std::string::npos) buffer.resize(pos);
        return buffer;
    }

    std::expected<std::string, error_code> base64url_decode(std::string_view data) {
        std::string b64_str(data);
        for (char& c : b64_str) {
            if (c == '-') c = '+';
            else if (c == '_') c = '/';
        }
        while (b64_str.length() % 4) b64_str += '=';
        
        auto bio = std::unique_ptr<BIO, decltype(&BIO_free_all)>(BIO_new_mem_buf(b64_str.data(), b64_str.length()), &BIO_free_all);
        if (!bio) return std::unexpected(error_code::invalid_format);
        
        auto b64 = std::unique_ptr<BIO, decltype(&BIO_free)>(BIO_new(BIO_f_base64()), &BIO_free);
        BIO_set_flags(b64.get(), BIO_FLAGS_BASE64_NO_NL);
        bio.reset(BIO_push(b64.release(), bio.release()));

        std::string decoded_str(b64_str.length(), '\0');
        int decoded_len = BIO_read(bio.get(), decoded_str.data(), decoded_str.length());
        if (decoded_len < 0) return std::unexpected(error_code::invalid_format);
        decoded_str.resize(decoded_len);
        return decoded_str;
    }

    sha256_digest hmac_sha256(std::string_view secret, std::string_view data) {
        sha256_digest digest;
        unsigned int hash_len = 0;
        HMAC(EVP_sha256(), secret.data(), secret.length(), (const unsigned char*)data.data(), data.length(), digest.data(), &hash_len);
        assert(hash_len == digest.size());
        return digest;
    }

    bool constant_time_compare(std::string_view a, std::string_view b) {
        return a.length() == b.length() && CRYPTO_memcmp(a.data(), b.data(), a.length()) == 0;
    }

    // FIX: New helper to purely decode the claims without any validation.
    std::expected<claims_map, error_code> decode_claims_unvalidated(std::string_view token) {
        using enum jwt::error_code;
        const auto first_dot = token.find('.');
        if (first_dot == std::string_view::npos) return std::unexpected(invalid_format);
        const auto second_dot = token.rfind('.');
        if (second_dot == first_dot) return std::unexpected(invalid_format);

        const std::string_view payload_b64 = token.substr(first_dot + 1, second_dot - first_dot - 1);
        auto payload_decoded = base64url_decode(payload_b64);
        if (!payload_decoded) return std::unexpected(invalid_format);
        
        try {
            return json_parser(*payload_decoded).get_map();
        } catch (const parsing_error&) {
            return std::unexpected(invalid_json);
        }
    }

} // anonymous namespace

// Constructor updated to initialize m_mfa_timeout
service::service(std::string secret, std::chrono::seconds timeout, std::chrono::seconds mfa_timeout)
    : m_secret{std::move(secret)}, m_timeout{timeout}, m_mfa_timeout{mfa_timeout} {
    if (m_secret.empty()) throw std::invalid_argument("jwt secret cannot be empty.");
}

service::~service() noexcept = default;
service::service(service&&) noexcept = default;

std::string service::sign(std::string_view data) const {
    const sha256_digest signature_raw = hmac_sha256(m_secret, data);
    return base64url_encode({(const char*)signature_raw.data(), signature_raw.size()});
}

std::expected<std::string, error_code> service::get_token(const claims_map& claims) const {
    static const std::string header_b64 = base64url_encode(R"({"alg":"HS256","typ":"JWT"})");
    auto claims_copy = claims;
    const auto now = std::chrono::system_clock::now();
    
    // Determine expiration based on 'preauth' claim
    std::chrono::seconds duration = m_timeout;
    if (auto it = claims.find("preauth"); it != claims.end() && it->second == "true") {
        duration = m_mfa_timeout;
    }

    claims_copy["iat"] = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
    claims_copy["exp"] = std::to_string(std::chrono::duration_cast<std::chrono::seconds>((now + duration).time_since_epoch()).count());
    
    std::string payload_str;
    try {
        payload_str = json_parser::build(claims_copy);
    } catch (const output_error&) {
        return std::unexpected(error_code::json_creation_failed);
    }

    const std::string payload_b64 = base64url_encode(payload_str);
    const std::string signing_input = header_b64 + "." + payload_b64;
    const std::string signature_b64 = sign(signing_input);

    return signing_input + "." + signature_b64;
}

std::expected<claims_map, error_code> service::is_valid(std::string_view token) const {
    return validate_and_decode(token, true);
}

// FIX: get_claims now only decodes, it does not validate.
std::expected<claims_map, error_code> service::get_claims(std::string_view token) const {
    return decode_claims_unvalidated(token);
}

std::expected<claims_map, error_code> service::validate_and_decode(std::string_view token, bool verify_signature) const {
    using enum jwt::error_code;
    
    // FIX: Start by decoding the claims without validation.
    auto claims_result = decode_claims_unvalidated(token);
    if (!claims_result) {
        return std::unexpected(claims_result.error());
    }
    auto claims = *claims_result;

    if (verify_signature) {
        const auto second_dot = token.rfind('.');
        // Note: format has already been checked by decode_claims_unvalidated
        const std::string_view signing_input = token.substr(0, second_dot);
        const std::string_view signature_b64 = token.substr(second_dot + 1);

        auto received_signature_decoded = base64url_decode(signature_b64);
        if (!received_signature_decoded) return std::unexpected(invalid_format);

        const sha256_digest expected_signature_raw = hmac_sha256(m_secret, signing_input);
        if (!constant_time_compare({(const char*)expected_signature_raw.data(), expected_signature_raw.size()}, *received_signature_decoded)) {
            return std::unexpected(invalid_signature);
        }
    }
    
    const auto it = claims.find("exp");
    if (it == claims.end()) return std::unexpected(missing_expiration_claim);
    
    long long exp_val;
    if (auto [ptr, ec] = std::from_chars(it->second.data(), it->second.data() + it->second.size(), exp_val); ec != std::errc{}) {
        return std::unexpected(invalid_claim_format);
    }

    if (std::chrono::system_clock::now() > std::chrono::system_clock::from_time_t(exp_val)) {
        return std::unexpected(token_expired);
    }

    return claims;
}

} // namespace detail


// --- Internal Global Service Instance ---
namespace {
    detail::service& get_service() {
        static std::unique_ptr<detail::service> g_jwt_service = []{
            auto secret = env::get<std::string>("JWT_SECRET", "a-secure-secret-key-that-is-at-least-32-bytes-long");
            auto timeout = env::get<long>("JWT_TIMEOUT_SECONDS", 3600);
            // Default MFA timeout to 300 seconds (5 minutes)
            auto mfa_timeout = env::get<long>("JWT_MFA_TIMEOUT_SECONDS", 300);
            return std::make_unique<detail::service>(std::move(secret), std::chrono::seconds(timeout), std::chrono::seconds(mfa_timeout));
        }();
        return *g_jwt_service;
    }
}

// --- Public API Implementation ---

std::string sign(std::string_view data) {
    return get_service().sign(data);
}

std::expected<std::string, error_code> get_token(const claims_map& claims) {
    return get_service().get_token(claims);
}

std::expected<claims_map, error_code> is_valid(std::string_view token) {
    return get_service().is_valid(token);
}

std::expected<claims_map, error_code> get_claims(std::string_view token) {
    return get_service().get_claims(token);
}

std::string to_string(const error_code err) {
    using enum jwt::error_code;
    switch (err) {
        case token_expired: return "token has expired.";
        case invalid_signature: return "token signature is invalid.";
        case invalid_format: return "token format is invalid.";
        case invalid_json: return "failed to parse json in payload.";
        case missing_expiration_claim: return "expiration claim is missing.";
        case invalid_claim_format: return "a claim has an invalid format.";
        case json_creation_failed: return "failed to create internal json structure.";
        case token_too_long: return "a part of the token exceeds the maximum length limit.";
        case not_initialized: return "jwt service is not initialized.";
    }
    return "unknown jwt error.";
}

} // namespace jwt
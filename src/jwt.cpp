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

using namespace json;

namespace jwt {

// --- Internal Global Service Instance ---
namespace {
    // This function manages the singleton instance of the JWT service.
    // Initialization is thread-safe due to the static local variable.
    detail::service& get_service() {
        static std::unique_ptr<detail::service> g_jwt_service = []{
            auto secret = env::get<std::string>("JWT_SECRET", "a-secure-secret-key-that-is-at-least-32-bytes-long");
            auto timeout = env::get<long>("JWT_TIMEOUT_SECONDS", 3600);
            return std::make_unique<detail::service>(std::move(secret), std::chrono::seconds(timeout));
        }();
        return *g_jwt_service;
    }
}

// --- Public API Implementation ---

std::string sign(std::string_view data) {
    return get_service().sign(data);
}

std::expected<std::string, error_code> get_token(const std::map<std::string, std::string>& claims) {
    return get_service().get_token(claims);
}

std::expected<std::map<std::string, std::string, std::less<>>, error_code> is_valid(std::string_view token) {
    return get_service().is_valid(token);
}

std::expected<std::map<std::string, std::string, std::less<>>, error_code> get_claims(std::string_view token) {
    return get_service().get_claims(token);
}


// FIX: Restored the missing implementation of to_string
std::string to_string(const error_code err) {
    switch (err) {
        case error_code::token_expired: return "token has expired.";
        case error_code::invalid_signature: return "token signature is invalid.";
        case error_code::invalid_format: return "token format is invalid.";
        case error_code::invalid_json: return "failed to parse json in payload.";
        case error_code::missing_expiration_claim: return "expiration claim is missing.";
        case error_code::invalid_claim_format: return "a claim has an invalid format.";
        case error_code::json_creation_failed: return "failed to create internal json structure.";
        case error_code::token_too_long: return "a part of the token exceeds the maximum length limit.";
        case error_code::not_initialized: return "jwt service is not initialized.";
    }
    return "unknown jwt error.";
}


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
} // anonymous namespace

service::service(std::string secret, std::chrono::seconds timeout)
    : m_secret{std::move(secret)}, m_timeout{timeout} {
    if (m_secret.empty()) throw std::invalid_argument("jwt secret cannot be empty.");
}

service::~service() = default;
service::service(service&&) noexcept = default;

std::string service::sign(std::string_view data) const {
    const sha256_digest signature_raw = hmac_sha256(m_secret, data);
    return base64url_encode({(const char*)signature_raw.data(), signature_raw.size()});
}

std::expected<std::string, error_code> service::get_token(const std::map<std::string, std::string>& claims) const {
    static const std::string header_b64 = base64url_encode(R"({"alg":"HS256","typ":"JWT"})");
    auto claims_copy = claims;
    const auto now = std::chrono::system_clock::now();
    claims_copy["iat"] = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
    claims_copy["exp"] = std::to_string(std::chrono::duration_cast<std::chrono::seconds>((now + m_timeout).time_since_epoch()).count());
    
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

std::expected<std::map<std::string, std::string, std::less<>>, error_code> service::is_valid(std::string_view token) const {
    return validate_and_decode(token, true);
}

std::expected<std::map<std::string, std::string, std::less<>>, error_code> service::get_claims(std::string_view token) const {
    return validate_and_decode(token, false);
}

std::expected<std::map<std::string, std::string, std::less<>>, error_code> service::validate_and_decode(std::string_view token, bool verify_signature) const {
    const auto first_dot = token.find('.');
    if (first_dot == std::string_view::npos) return std::unexpected(error_code::invalid_format);
    const auto second_dot = token.rfind('.');
    if (second_dot == first_dot) return std::unexpected(error_code::invalid_format);

    const std::string_view signing_input = token.substr(0, second_dot);
    const std::string_view signature_b64 = token.substr(second_dot + 1);
    
    if (verify_signature) {
        auto received_signature_decoded = base64url_decode(signature_b64);
        if (!received_signature_decoded) return std::unexpected(error_code::invalid_format);

        const sha256_digest expected_signature_raw = hmac_sha256(m_secret, signing_input);
        if (!constant_time_compare({(const char*)expected_signature_raw.data(), expected_signature_raw.size()}, *received_signature_decoded)) {
            return std::unexpected(error_code::invalid_signature);
        }
    }
    
    const std::string_view payload_b64 = token.substr(first_dot + 1, second_dot - first_dot - 1);
    auto payload_decoded = base64url_decode(payload_b64);
    if (!payload_decoded) return std::unexpected(error_code::invalid_format);
    
    std::map<std::string, std::string, std::less<>> claims;
    try {
        claims = json_parser(*payload_decoded).get_map();
    } catch (const parsing_error&) {
        return std::unexpected(error_code::invalid_json);
    }

    const auto it = claims.find("exp");
    if (it == claims.end()) return std::unexpected(error_code::missing_expiration_claim);
    
    long long exp_val;
    if (auto [ptr, ec] = std::from_chars(it->second.data(), it->second.data() + it->second.size(), exp_val); ec != std::errc{}) {
        return std::unexpected(error_code::invalid_claim_format);
    }

    if (std::chrono::system_clock::now() > std::chrono::system_clock::from_time_t(exp_val)) {
        return std::unexpected(error_code::token_expired);
    }

    return claims;
}

} // namespace detail
} // namespace jwt

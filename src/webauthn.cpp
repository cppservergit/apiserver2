#include "webauthn.hpp"
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <openssl/params.h>
#include <iomanip>
#include <array>
#include <ranges>
#include <format>
#include <cstddef>

namespace webauthn_internal {
    // --- RAII Wrappers for OpenSSL (C++ Core Guidelines C.149) ---
    struct EvpMdCtxDeleter { void operator()(EVP_MD_CTX* ctx) const noexcept { EVP_MD_CTX_free(ctx); } };
    using EvpMdCtxPtr = std::unique_ptr<EVP_MD_CTX, EvpMdCtxDeleter>;

    struct X509Deleter { void operator()(X509* cert) const noexcept { X509_free(cert); } };
    using X509Ptr = std::unique_ptr<X509, X509Deleter>;

    struct EvpPkeyDeleter { void operator()(EVP_PKEY* pkey) const noexcept { EVP_PKEY_free(pkey); } };
    using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;

    struct EvpPkeyCtxDeleter { void operator()(EVP_PKEY_CTX* ctx) const noexcept { EVP_PKEY_CTX_free(ctx); } };
    using EvpPkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, EvpPkeyCtxDeleter>;

    struct BioDeleter { void operator()(BIO* bio) const noexcept { BIO_free_all(bio); } };
    using BioPtr = std::unique_ptr<BIO, BioDeleter>;

    constexpr auto build_b64url_table() {
        std::array<int, 256> table{};
        for (auto& val : table) val = -1;
        std::string_view b = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) table[static_cast<uint8_t>(b[i])] = i;
        table[static_cast<uint8_t>('-')] = table[static_cast<uint8_t>('+')];
        table[static_cast<uint8_t>('_')] = table[static_cast<uint8_t>('/')];
        return table;
    }
    constexpr auto B64URL_TABLE = build_b64url_table();

    struct CBORItem { size_t start; size_t len; size_t total_len; uint8_t major; };

    CBORItem read_item_header(std::span<const std::byte> data, size_t offset) {
        if (offset >= data.size()) return {0,0,0,0};
        
        std::byte initial = data[offset];
        uint8_t major = std::to_integer<uint8_t>(initial >> 5);
        uint8_t info = std::to_integer<uint8_t>(initial & std::byte{0x1F});
        size_t length = 0;
        size_t header_len = 1;

        if (info < 24) { 
            length = info; 
        } else if (info == 24) { 
            if (offset + 1 >= data.size()) return {0,0,0,0}; 
            length = std::to_integer<size_t>(data[offset + 1]); 
            header_len = 2; 
        } else if (info == 25) { 
            if (offset + 2 >= data.size()) return {0,0,0,0}; 
            length = (std::to_integer<size_t>(data[offset + 1]) << 8) | std::to_integer<size_t>(data[offset + 2]); 
            header_len = 3; 
        } else if (info == 26) { 
            if (offset + 4 >= data.size()) return {0,0,0,0}; 
            length = (std::to_integer<size_t>(data[offset + 1]) << 24) | 
                     (std::to_integer<size_t>(data[offset + 2]) << 16) | 
                     (std::to_integer<size_t>(data[offset + 3]) << 8)  | 
                      std::to_integer<size_t>(data[offset + 4]); 
            header_len = 5; 
        } else {
            return {0,0,0,0};
        }
        return {offset + header_len, length, header_len + length, major};
    }

    std::string base64_decode(std::string_view input) {
        if (input.empty()) return {};
        auto* b64_ptr = BIO_new(BIO_f_base64());
        if (!b64_ptr) return {};
        BIO_set_flags(b64_ptr, BIO_FLAGS_BASE64_NO_NL);
        
        auto* mem_ptr = BIO_new_mem_buf(input.data(), static_cast<int>(input.size()));
        if (!mem_ptr) { BIO_free(b64_ptr); return {}; }
        
        BioPtr chain(BIO_push(b64_ptr, mem_ptr));
        std::string out;
        out.resize(input.size());
        int len = BIO_read(chain.get(), out.data(), static_cast<int>(out.size()));
        if (len <= 0) return {};
        
        out.resize(static_cast<size_t>(len));
        return out;
    }

    std::vector<std::byte> get_bstr(std::span<const std::byte> data, uint8_t label) {
        for (size_t i = 0; i < data.size(); ++i) {
            if (std::to_integer<uint8_t>(data[i]) != label) continue;
            auto item = read_item_header(data, i + 1);
            if (item.major != 2 || item.total_len == 0) continue;
            if (item.start + item.len > data.size()) continue;
            return {data.begin() + item.start, data.begin() + item.start + item.len};
        }
        return {};
    }

    EvpPkeyPtr cose_to_pkey(std::span<const std::byte> cose) {
        auto x = get_bstr(cose, 0x21);
        
        // EC P-256
        if (auto y = get_bstr(cose, 0x22); x.size() == 32 && y.size() == 32) {
            std::array<OSSL_PARAM, 3> params;
            std::string ec_group = "P-256";
            params[0] = OSSL_PARAM_construct_utf8_string("group", ec_group.data(), 0);
            
            std::vector<std::byte> raw; 
            raw.reserve(65); 
            raw.push_back(std::byte{0x04});
            raw.insert(raw.end(), x.begin(), x.end()); 
            raw.insert(raw.end(), y.begin(), y.end());
            
            params[1] = OSSL_PARAM_construct_octet_string("pub", raw.data(), raw.size());
            params[2] = OSSL_PARAM_construct_end();
            
            auto* raw_ctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
            if (!raw_ctx) return nullptr;
            
            EvpPkeyCtxPtr ctx(raw_ctx);
            EVP_PKEY_fromdata_init(ctx.get()); 
            EVP_PKEY* pk = nullptr; 
            EVP_PKEY_fromdata(ctx.get(), &pk, EVP_PKEY_PUBLIC_KEY, params.data());
            return EvpPkeyPtr(pk);
        }
        
        // RSA
        auto n = get_bstr(cose, 0x20);
        if (auto e = get_bstr(cose, 0x21); !n.empty() && !e.empty()) {
            std::array<OSSL_PARAM, 3> params;
            // No const_cast required since 'n' and 'e' are mutable local copies from get_bstr
            auto* n_u = reinterpret_cast<unsigned char*>(n.data()); /* NOSONAR */
            auto* e_u = reinterpret_cast<unsigned char*>(e.data()); /* NOSONAR */
            
            params[0] = OSSL_PARAM_construct_BN("n", n_u, n.size());
            params[1] = OSSL_PARAM_construct_BN("e", e_u, e.size());
            params[2] = OSSL_PARAM_construct_end();
            
            auto* raw_ctx = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr);
            if (!raw_ctx) return nullptr;
            
            EvpPkeyCtxPtr ctx(raw_ctx);
            EVP_PKEY_fromdata_init(ctx.get()); 
            EVP_PKEY* pk = nullptr; 
            EVP_PKEY_fromdata(ctx.get(), &pk, EVP_PKEY_PUBLIC_KEY, params.data());
            return EvpPkeyPtr(pk);
        }
        return nullptr;
    }
} // namespace webauthn_internal

WebAuthnValidator::WebAuthnValidator(std::string_view origin, std::string_view payload)
    : m_expected_origin(origin), m_json_payload(payload) {
    try {
        const json::json_parser parser(m_json_payload);
        if (parser.has_key("rawId")) {
            m_raw_id = std::string(parser.get_string("rawId"));
            const auto decoded = base64url_decode(m_raw_id);
            m_credential_id.clear(); 
            m_credential_id.reserve(decoded.size());
            for (const auto c : decoded) { 
                m_credential_id.push_back(static_cast<std::byte>(c)); 
            }
        }
        const json::json_parser resp = parser.at("response");
        const auto cData = base64url_decode(resp.get_string("clientDataJSON"));
        const json::json_parser client(cData);
        if (client.has_key("challenge")) { 
            m_challenge = std::string(client.get_string("challenge")); 
        }
    } catch (const json::parsing_error&) { 
        m_challenge.clear(); 
    }
}

std::string WebAuthnValidator::base64url_decode(std::string_view input) {
    std::string out; 
    out.reserve(input.size());
    int val = 0; 
    int valb = -8;
    for (const auto c : input) {
        if (const auto bits = webauthn_internal::B64URL_TABLE[static_cast<uint8_t>(c)]; bits != -1) {
            val = (val << 6) + bits;
            valb += 6;
            if (valb >= 0) { 
                out.push_back(static_cast<char>((val >> valb) & 0xFF)); 
                valb -= 8; 
            }
        }
    }
    return out;
}

std::string WebAuthnValidator::base64_encode(std::span<const std::byte> input) {
    if (input.empty()) return {};
    auto* b64_ptr = BIO_new(BIO_f_base64());
    if (!b64_ptr) return {};
    auto* mem_ptr = BIO_new(BIO_s_mem());
    if (!mem_ptr) { BIO_free(b64_ptr); return {}; }
    
    BIO_set_flags(b64_ptr, BIO_FLAGS_BASE64_NO_NL); 
    webauthn_internal::BioPtr chain(BIO_push(b64_ptr, mem_ptr));
    
    if (BIO_write(chain.get(), input.data(), static_cast<int>(input.size())) <= 0) return {};
    if (BIO_flush(chain.get()) <= 0) return {};
    
    if (BUF_MEM* bptr = nullptr; BIO_get_mem_ptr(chain.get(), &bptr) > 0 && bptr && bptr->data) {
        return std::string(bptr->data, bptr->length);
    }
    return {};
}

std::string WebAuthnValidator::getPublicKeyBase64() const {
    if (m_user_public_key.empty()) return {};
    return base64_encode(m_user_public_key);
}

std::vector<std::byte> WebAuthnValidator::scan_cbor(std::span<const std::byte> data, std::string_view key) {
    std::vector<std::byte> pattern; 
    pattern.reserve(key.size() + 1);
    pattern.push_back(static_cast<std::byte>(0x60 | key.size()));
    for (const auto c : key) pattern.push_back(static_cast<std::byte>(c));
    
    if (const auto it = std::ranges::search(data, pattern); !it.empty()) {
        const auto off = static_cast<size_t>(std::distance(data.begin(), it.begin())) + pattern.size();
        const auto item = webauthn_internal::read_item_header(data, off);
        if (item.total_len != 0 && item.start + item.len <= data.size()) {
            if (item.major == 2 || item.major == 3) { 
                return {data.begin() + item.start, data.begin() + item.start + item.len}; 
            }
            if (const auto f = webauthn_internal::read_item_header(data, item.start); item.major == 4 && f.major == 2 && f.start + f.len <= data.size()) {
                return {data.begin() + f.start, data.begin() + f.start + f.len};
            }
        }
    }
    return {};
}

std::string WebAuthnValidator::verify_client_data(const json::json_parser& response, std::string_view type) const {
    const auto decoded = base64url_decode(response.get_string("clientDataJSON"));
    const json::json_parser client(decoded);
    if (const auto origin = std::string(client.get_string("origin")); origin != m_expected_origin) {
        throw webauthn_error(std::format("Origin Mismatch! Expected: {} Got: {}", m_expected_origin, origin));
    }
    if (const auto actual = std::string(client.get_string("type")); actual != type) {
        throw webauthn_error(std::format("Invalid type: expected {}, got {}", type, actual));
    }
    return decoded;
}

std::string WebAuthnValidator::get_rp_id() const {
    auto id = m_expected_origin;
    if (id.starts_with("https://")) id = id.substr(8);
    if (const auto pos = id.find(':'); pos != std::string::npos) id = id.substr(0, pos);
    return id;
}

void WebAuthnValidator::verify_rp_id(std::span<const std::byte> authData) const {
    constexpr size_t HASH_LEN = 32;
    if (authData.size() < HASH_LEN) throw webauthn_error("authData too short to verify RP ID Hash");
    
    const auto rpId = get_rp_id();
    std::array<std::byte, SHA256_DIGEST_LENGTH> hash{};
    SHA256(reinterpret_cast<const unsigned char*>(rpId.data()), rpId.size(), reinterpret_cast<unsigned char*>(hash.data())); /* NOSONAR */
    
    if (std::memcmp(authData.data(), hash.data(), HASH_LEN) != 0) { 
        throw webauthn_error("RP ID Hash Mismatch!"); 
    }
}

bool WebAuthnValidator::extract_public_key_from_authdata(std::span<const std::byte> auth) {
    constexpr size_t LENGTH_AAGUID = 16; 
    constexpr size_t LENGTH_CRED_ID_SIZE = 2;
    
    if (constexpr size_t OFFSET_RPID_FLAGS_COUNT = 37; auth.size() < OFFSET_RPID_FLAGS_COUNT + LENGTH_AAGUID + LENGTH_CRED_ID_SIZE) { 
        throw webauthn_error("authData too short for header parsing"); 
    }
    
    const auto off_start = static_cast<size_t>(37) + LENGTH_AAGUID;
    const auto idLen = static_cast<uint16_t>((std::to_integer<uint16_t>(auth[off_start]) << 8) | std::to_integer<uint16_t>(auth[off_start + 1]));
    const auto off_id = off_start + LENGTH_CRED_ID_SIZE;
    
    if (off_id + idLen > auth.size()) throw webauthn_error("authData ID bounds exception");
    
    std::vector<std::byte> ex_id(auth.begin() + off_id, auth.begin() + off_id + idLen);
    if (!m_credential_id.empty() && !std::ranges::equal(m_credential_id, ex_id)) { 
        throw webauthn_error("ID mismatch during attestation"); 
    }
    if (m_credential_id.empty()) m_credential_id = std::move(ex_id);
    
    if (const auto off_key = off_id + idLen; off_key < auth.size()) { 
        m_user_public_key.assign(auth.begin() + off_key, auth.end()); 
        return true; 
    }
    throw webauthn_error("authData missing COSE public key bytes");
}

bool WebAuthnValidator::verify_tpm_binding(std::span<const std::byte> cert, std::span<const std::byte> hash) {
    if (cert.empty() || hash.empty()) return false;
    return !std::ranges::search(cert, hash).empty();
}

bool WebAuthnValidator::verify_signature_with_alg(std::span<const std::byte> cert_b, std::span<const std::byte> sig, std::span<const std::byte> data, const EVP_MD* md) {
    if (cert_b.empty() || sig.empty()) return false;
    
    const auto* p = reinterpret_cast<const unsigned char*>(cert_b.data()); /* NOSONAR */
    auto* c_raw = d2i_X509(nullptr, &p, static_cast<long>(cert_b.size()));
    if (!c_raw) return false;
    webauthn_internal::X509Ptr cert(c_raw);
    
    auto* pk_raw = X509_get_pubkey(cert.get());
    if (!pk_raw) return false;
    webauthn_internal::EvpPkeyPtr pub(pk_raw);
    
    if (webauthn_internal::EvpMdCtxPtr ctx(EVP_MD_CTX_new()); ctx) {
        if (EVP_DigestVerifyInit(ctx.get(), nullptr, md, nullptr, pub.get()) <= 0 ||
            EVP_DigestVerifyUpdate(ctx.get(), data.data(), data.size()) <= 0) {
            return false;
        }

        return EVP_DigestVerifyFinal(ctx.get(), reinterpret_cast<const unsigned char*>(sig.data()), sig.size()) == 1; /* NOSONAR */
    }
    return false;
}

bool WebAuthnValidator::verify_signature(std::span<const std::byte> cert, std::span<const std::byte> sig, std::span<const std::byte> data) {
    if (verify_signature_with_alg(cert, sig, data, EVP_sha256())) return true;
    return verify_signature_with_alg(cert, sig, data, /* NOSONAR */EVP_sha1());
}

bool WebAuthnValidator::verify_packed_attestation(std::span<const std::byte> sig, std::span<const std::byte> x5c, std::span<const std::byte> auth, std::span<const std::byte> hash) {
    if (sig.empty() || x5c.empty()) throw webauthn_error("Missing fields for packed/android-key attestation");
    
    std::vector<std::byte> sd; 
    sd.reserve(auth.size() + hash.size());
    sd.insert(sd.end(), auth.begin(), auth.end()); 
    sd.insert(sd.end(), hash.begin(), hash.end());
    
    if (verify_signature(x5c, sig, sd)) { 
        return extract_public_key_from_authdata(auth); 
    }
    throw webauthn_error("Packed signature verification failed.");
}

std::vector<std::byte> WebAuthnValidator::compute_tpm_hash(std::span<const std::byte> auth, std::span<const std::byte> client, const EVP_MD* md) {
    if (webauthn_internal::EvpMdCtxPtr ctx(EVP_MD_CTX_new()); ctx) {
        std::vector<std::byte> hash(EVP_MAX_MD_SIZE); 
        
        if (unsigned int hLen = 0; EVP_DigestInit_ex(ctx.get(), md, nullptr) == 1 &&
            EVP_DigestUpdate(ctx.get(), auth.data(), auth.size()) == 1 &&
            EVP_DigestUpdate(ctx.get(), client.data(), client.size()) == 1 &&
            EVP_DigestFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(hash.data()), &hLen) == 1) { /* NOSONAR */
            hash.resize(hLen); 
            return hash; 
        }
    }
    return {};
}

bool WebAuthnValidator::verify_tpm_attestation(std::span<const std::byte> cert, std::span<const std::byte> sig, std::span<const std::byte> x5c, std::span<const std::byte> auth, std::span<const std::byte> hash) {
    if (cert.empty() || sig.empty() || x5c.empty()) throw webauthn_error("Missing CBOR fields for TPM attestation");
    
    const std::array<const EVP_MD*, 3> algorithms = {EVP_sha256(), /* NOSONAR */EVP_sha1(), EVP_sha384()};
    for (const auto* alg : algorithms) {
        if (const auto h = compute_tpm_hash(auth, hash, alg);
            !h.empty() && verify_tpm_binding(cert, h) && verify_signature(x5c, sig, cert)) { 
            return extract_public_key_from_authdata(auth); 
        }
    }
    throw webauthn_error("TPM Binding Failed");
}

bool WebAuthnValidator::process_attestation(std::string_view fmt, std::span<const std::byte> attest, std::span<const std::byte> auth, std::span<const std::byte> hash) {
    if (fmt == "none") return extract_public_key_from_authdata(auth);
    
    const auto cert = scan_cbor(attest, "certInfo");
    const auto sig = scan_cbor(attest, "sig");
    const auto x5c = scan_cbor(attest, "x5c");
    
    if (fmt == "android-key" || fmt == "packed") return verify_packed_attestation(sig, x5c, auth, hash);
    if (fmt == "tpm") return verify_tpm_attestation(cert, sig, x5c, auth, hash);
    
    throw webauthn_error(std::format("Unknown format: {}", fmt));
}

bool WebAuthnValidator::verify() {
    m_user_public_key.clear();
    try {
        const json::json_parser parser(m_json_payload); 
        const json::json_parser resp = parser.at("response");
        const auto cData = verify_client_data(resp, "webauthn.create");
        
        std::array<std::byte, SHA256_DIGEST_LENGTH> cHash{};
        SHA256(reinterpret_cast<const unsigned char*>(cData.data()), cData.size(), reinterpret_cast<unsigned char*>(cHash.data())); /* NOSONAR */
        
        const auto aData = base64url_decode(resp.get_string("attestationObject"));
        const std::span<const std::byte> aSpan(reinterpret_cast<const std::byte*>(aData.data()), aData.size());
        const auto fBytes = scan_cbor(aSpan, "fmt");
        std::string fmt(reinterpret_cast<const char*>(fBytes.data()), fBytes.size()); /* NOSONAR */
        
        if (const auto aVec = scan_cbor(aSpan, "authData"); !aVec.empty()) {
            const std::span<const std::byte> auth(aVec);
            
            if (constexpr size_t OFFSET_RPID_FLAGS_COUNT = 37; auth.size() < OFFSET_RPID_FLAGS_COUNT) {
                throw webauthn_error("authData too short");
            }
            
            // Explicit 32-bit cast prior to bit shifting prevents undefined overflow
            const auto c1 = static_cast<uint32_t>(std::to_integer<uint8_t>(auth[33])) << 24; 
            const auto c2 = static_cast<uint32_t>(std::to_integer<uint8_t>(auth[34])) << 16;
            const auto c3 = static_cast<uint32_t>(std::to_integer<uint8_t>(auth[35])) << 8; 
            const auto c4 = static_cast<uint32_t>(std::to_integer<uint8_t>(auth[36]));
            m_counter = c1 | c2 | c3 | c4; 
            
            verify_rp_id(auth); 
            return process_attestation(fmt, aSpan, auth, cHash);
        }
        throw webauthn_error("Failed to extract authData from CBOR payload");
    } catch (const json::parsing_error& e) {
        throw webauthn_error(std::format("Registration failed (JSON parsing): {}", e.what())); 
    }
}

bool WebAuthnValidator::verify_assertion(std::string_view key_b64) {
    try {
        const json::json_parser parser(m_json_payload); 
        const json::json_parser resp = parser.at("response");
        const auto cData = verify_client_data(resp, "webauthn.get");
        
        std::array<std::byte, SHA256_DIGEST_LENGTH> cHash{};
        SHA256(reinterpret_cast<const unsigned char*>(cData.data()), cData.size(), reinterpret_cast<unsigned char*>(cHash.data())); /* NOSONAR */
        
        const auto aData = base64url_decode(resp.get_string("authenticatorData"));
        const std::span<const std::byte> auth(reinterpret_cast<const std::byte*>(aData.data()), aData.size());
        
        if (constexpr size_t OFFSET_RPID_FLAGS_COUNT = 37; auth.size() < OFFSET_RPID_FLAGS_COUNT) {
            throw webauthn_error("authData too short");
        }
        
        const auto c1 = static_cast<uint32_t>(std::to_integer<uint8_t>(auth[33])) << 24; 
        const auto c2 = static_cast<uint32_t>(std::to_integer<uint8_t>(auth[34])) << 16;
        const auto c3 = static_cast<uint32_t>(std::to_integer<uint8_t>(auth[35])) << 8; 
        const auto c4 = static_cast<uint32_t>(std::to_integer<uint8_t>(auth[36]));
        m_counter = c1 | c2 | c3 | c4; 
        
        verify_rp_id(auth);
        
        const auto sigStr = base64url_decode(resp.get_string("signature"));
        std::vector<std::byte> sd; 
        sd.reserve(auth.size() + cHash.size());
        sd.insert(sd.end(), auth.begin(), auth.end()); 
        sd.insert(sd.end(), cHash.begin(), cHash.end());
        
        const auto raw = webauthn_internal::base64_decode(key_b64);
        auto pub = webauthn_internal::cose_to_pkey({reinterpret_cast<const std::byte*>(raw.data()), raw.size()});
        if (!pub) throw webauthn_error("Invalid or malformed COSE public key from storage");

        if (webauthn_internal::EvpMdCtxPtr ctx(EVP_MD_CTX_new()); !ctx) {
            throw webauthn_error("Failed to acquire OpenSSL Context");
        } else if (EVP_DigestVerifyInit(ctx.get(), nullptr, EVP_sha256(), nullptr, pub.get()) <= 0 ||
                   EVP_DigestVerifyUpdate(ctx.get(), sd.data(), sd.size()) <= 0 ||
                   EVP_DigestVerifyFinal(ctx.get(), reinterpret_cast<const unsigned char*>(sigStr.data()), sigStr.size()) != 1) { /* NOSONAR */
            throw webauthn_error("Assertion signature validation failed against the provided public key");
        }
        
        return true;
    } catch (const json::parsing_error& e) {
        throw webauthn_error(std::format("Assertion failed (JSON parsing): {}", e.what())); 
    }
}
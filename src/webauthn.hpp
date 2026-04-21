#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <span>
#include <string_view>
#include <cstdint>
#include <cstddef> // for std::byte
#include <memory>
#include <stdexcept>

// OpenSSL
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/sha.h>

// Project dependencies
#include "json_parser.hpp"

// Specific exception for WebAuthn validation failures
class webauthn_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class WebAuthnValidator {
public:
    WebAuthnValidator(std::string_view expected_origin, std::string_view json_payload);

    // Returns the challenge extracted internally from the payload
    [[nodiscard]] const std::string& getChallenge() const noexcept {
        return m_challenge;
    }

    // Verifies the registration payload.
    // Throws webauthn_error on validation failure.
    [[nodiscard]] bool verify();

    /**
     * @brief Verifies a WebAuthn login (assertion) payload.
     * @param public_key_b64 The stored public key in Base64 format.
     * @return true if valid, false otherwise.
     */
    [[nodiscard]] bool verify_assertion(std::string_view public_key_b64);

    /**
     * @brief Returns the sign counter extracted from authenticatorData.
     * Only valid after a successful verify() or verify_assertion() call.
     */
    [[nodiscard]] uint32_t getCounter() const noexcept {
        return m_counter;
    }

    // Returns the extracted User Public Key (COSE format) as a span (Zero copy)
    [[nodiscard]] std::span<const std::byte> getPublicKey() const noexcept {
        return m_user_public_key;
    }

    // Returns the Public Key encoded as a Base64 string (Ready for DB)
    [[nodiscard]] std::string getPublicKeyBase64() const;

    // Returns the extracted Credential ID as a span (Zero copy)
    [[nodiscard]] std::span<const std::byte> getCredentialId() const noexcept {
        return m_credential_id;
    }

    // Returns the Credential ID encoded as a Base64 string (Ready for DB)
    [[nodiscard]] const std::string& getCredentialIdBase64() const noexcept {
        return m_raw_id;
    }

private:
    std::string m_expected_origin;
    std::string m_json_payload;
    std::string m_challenge;
    std::string m_raw_id;
    std::vector<std::byte> m_user_public_key;
    std::vector<std::byte> m_credential_id;
    uint32_t m_counter{0};

    // Decoding & Encoding
    [[nodiscard]] static std::string base64url_decode(std::string_view input);
    [[nodiscard]] static std::string base64_encode(std::span<const std::byte> input);
    
    // CBOR Parsing Utilities
    [[nodiscard]] static std::vector<std::byte> scan_cbor(std::span<const std::byte> data, std::string_view key_name);
    [[nodiscard]] bool extract_public_key_from_authdata(std::span<const std::byte> authData);

    // Cryptographic Verifiers
    [[nodiscard]] static bool verify_signature(std::span<const std::byte> cert_bytes, std::span<const std::byte> signature, std::span<const std::byte> signed_data);
    [[nodiscard]] static bool verify_signature_with_alg(std::span<const std::byte> cert_bytes, std::span<const std::byte> signature, std::span<const std::byte> signed_data, const EVP_MD* digest);
    
    // Extracted format-specific validations
    [[nodiscard]] bool verify_packed_attestation(std::span<const std::byte> signature, std::span<const std::byte> x5c, std::span<const std::byte> authData, std::span<const std::byte> clientDataHash);
    [[nodiscard]] bool verify_tpm_attestation(std::span<const std::byte> certInfo, std::span<const std::byte> signature, std::span<const std::byte> x5c, std::span<const std::byte> authData, std::span<const std::byte> clientDataHash);
    
    [[nodiscard]] static std::vector<std::byte> compute_tpm_hash(std::span<const std::byte> authData, std::span<const std::byte> clientDataHash, const EVP_MD* md);
    [[nodiscard]] static bool verify_tpm_binding(std::span<const std::byte> certInfo, std::span<const std::byte> expectedHash);

    // --- Refactoring Helpers ---
    [[nodiscard]] std::string verify_client_data(const json::json_parser& response, std::string_view expected_type) const;
    void verify_rp_id(std::span<const std::byte> authData) const;
    [[nodiscard]] bool process_attestation(std::string_view fmt, std::span<const std::byte> attestSpan, std::span<const std::byte> authData, std::span<const std::byte> clientDataHash);
    [[nodiscard]] std::string get_rp_id() const;
};
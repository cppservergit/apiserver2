#ifndef JWT_HPP
#define JWT_HPP

#include <string>
#include <string_view>
#include <map>
#include <expected>
#include <chrono>
#include <functional> // For std::less

/**
 * @file jwt.hpp
 * @brief A C++23 component for creating, signing, and validating JSON Web Tokens (JWT).
 * This module initializes itself from environment variables:
 * - JWT_SECRET: The secret key for signing.
 * - JWT_TIMEOUT_SECONDS: The token's validity duration.
 */

namespace jwt {

// --- Public API ---

enum class error_code {
    token_expired,
    invalid_signature,
    invalid_format,
    invalid_json,
    missing_expiration_claim,
    invalid_claim_format,
    json_creation_failed,
    token_too_long,
    not_initialized
};

std::string to_string(const error_code err);

// *** SONARCLOUD FIX ***
// Use a type alias for the claims map with a transparent comparator (std::less<>).
// This allows for more efficient lookups with string_view keys.
using claims_map = std::map<std::string, std::string, std::less<>>;

/**
 * @brief Signs the given data using HMAC-SHA256 with the configured secret.
 * @param data The data to sign.
 * @return The Base64URL-encoded signature as a std::string.
 * @throws std::runtime_error if the service is not initialized.
 */
[[nodiscard]] std::string sign(std::string_view data);

/**
 * @brief Creates and signs a new JWT with the given claims.
 * @param claims A map of key-value pairs to include in the token's payload.
 * @return A std::expected containing the signed JWT string on success, or an error_code on failure.
 */
[[nodiscard]] std::expected<std::string, error_code> get_token(const claims_map& claims);

/**
 * @brief Validates a JWT string, including its signature and expiration.
 * @param token The JWT string to validate.
 * @return A std::expected containing a map of the token's claims on success, or an error_code on failure.
 */
[[nodiscard]] std::expected<claims_map, error_code> is_valid(std::string_view token);

/**
 * @brief Decodes a JWT and returns its claims without verifying the signature.
 *
 * This method still validates the token's format and checks the expiration time.
 * It is useful for secure endpoints where the signature has already been
 * verified by the server's dispatch logic.
 *
 * @param token The JWT string to decode.
 * @return A std::expected containing a map of the token's claims on success, or an error_code on failure.
 */
[[nodiscard]] std::expected<claims_map, error_code> get_claims(std::string_view token);


// --- Implementation Details (not for direct use) ---
namespace detail {
    class service {
    public:
        service(std::string secret, std::chrono::seconds timeout);
        // *** SONARCLOUD FIX ***
        // Destructors should never throw. Added the noexcept specifier.
        ~service() noexcept;
        service(const service&) = delete;
        service& operator=(const service&) = delete;
        service(service&&) noexcept;
        service& operator=(service&&) = delete;

        [[nodiscard]] std::string sign(std::string_view data) const;
        [[nodiscard]] std::expected<std::string, error_code> get_token(const claims_map& claims) const;
        [[nodiscard]] std::expected<claims_map, error_code> is_valid(std::string_view token) const;
        [[nodiscard]] std::expected<claims_map, error_code> get_claims(std::string_view token) const;
    
    private:
        [[nodiscard]] std::expected<claims_map, error_code> validate_and_decode(std::string_view token, bool verify_signature) const;
        const std::string m_secret;
        const std::chrono::seconds m_timeout;
    };
} // namespace detail

} // namespace jwt

#endif // JWT_HPP

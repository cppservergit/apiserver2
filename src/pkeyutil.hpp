#ifndef PKEYUTIL_HPP
#define PKEYUTIL_HPP

#include <string>
#include <string_view>

// Result of a decryption operation
struct DecryptionResult {
    bool success;
    std::string content; // On success, contains the decrypted text. On failure, an error message.
};


/**
 * @brief Decrypts a file encrypted with an RSA public key.
 *
 * This function reads a file and decrypts it
 * using the private key named "private.pem" located in the same directory.
 *
 * @param filename The path to the encrypted file.
 * @return A DecryptionResult struct.
 */
[[nodiscard]] DecryptionResult decrypt(std::string_view filename) noexcept;

#endif // PKEYUTIL_HPP

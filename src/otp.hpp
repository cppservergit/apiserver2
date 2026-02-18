#ifndef OTP_HPP
#define OTP_HPP

#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <expected>
#include <chrono>
#include <algorithm>
#include <format>
#include <cmath>
#include <cstddef> // For std::byte
#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace otp {

    namespace detail {
        // Base32 decoding lookup table (RFC 4648)
        static constexpr int8_t base32_lookup[] = {
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0-15
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 16-31
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 32-47
            -1, -1, 26, 27, 28, 29, 30, 31, -1, -1, -1, -1, -1, -1, -1, -1, // 48-63 (2-7)
            -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, // 64-79 (A-O)
            15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, // 80-95 (P-Z)
            -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, // 96-111 (a-o)
            15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1  // 112-127 (p-z)
        };

        // Use std::byte for raw binary data
        inline std::expected<std::vector<std::byte>, std::string> decode_base32(std::string_view input) {
            std::vector<std::byte> output;
            output.reserve(input.length() * 5 / 8);

            int buffer = 0;
            int bits_left = 0;

            for (char c : input) {
                if (c == '=' || std::isspace(c)) continue;

                // FIX: Cast to unsigned char to safely handle all ranges (0-255)
                unsigned char uc = static_cast<unsigned char>(c);

                // Now we check if it's out of bounds (>= 128) OR invalid in the table (-1)
                if (uc >= 128 || base32_lookup[uc] == -1) {
                    return std::unexpected("Invalid Base32 character encountered");
                }
                
                buffer = (buffer << 5) | base32_lookup[uc];
                bits_left += 5;

                if (bits_left >= 8) {
                    output.push_back(static_cast<std::byte>((buffer >> (bits_left - 8)) & 0xFF));
                    bits_left -= 8;
                }
            }
            return output;
        }

        inline std::expected<std::string, std::string> generate_hotp(
            const std::vector<std::byte>& key, 
            uint64_t counter, 
            int digits) 
        {
            // Convert counter to Big Endian (Network Byte Order)
            uint64_t counter_be = 0;
            for (int i = 0; i < 8; i++) {
                reinterpret_cast<uint8_t*>(&counter_be)[i] = (counter >> ((7 - i) * 8)) & 0xFF;
            }

            // Using std::byte for the hash buffer
            std::array<std::byte, EVP_MAX_MD_SIZE> hash{};
            unsigned int hash_len = 0;

            // Using OpenSSL HMAC (SHA1 for standard TOTP/Google Authenticator)
            // Note: key.data() returns std::byte*, which implicitly converts to void* required by HMAC.
            // We must cast hash.data() (std::byte*) to unsigned char* for OpenSSL.
            if (!HMAC(EVP_sha1(), key.data(), static_cast<int>(key.size()),
                      reinterpret_cast<const unsigned char*>(&counter_be), sizeof(counter_be),
                      reinterpret_cast<unsigned char*>(hash.data()), &hash_len)) {
                return std::unexpected("OpenSSL HMAC calculation failed");
            }

            // RFC 4226 Truncation
            // Use std::to_integer to extract values from std::byte for arithmetic/indexing
            int offset = std::to_integer<int>(hash[hash_len - 1] & std::byte{0x0F});
            
            // Extract 32-bit integer from the hash at the calculated offset
            // We cast each byte to int before shifting
            int binary = (std::to_integer<int>(hash[offset] & std::byte{0x7f}) << 24) |
                         (std::to_integer<int>(hash[offset + 1] & std::byte{0xff}) << 16) |
                         (std::to_integer<int>(hash[offset + 2] & std::byte{0xff}) << 8) |
                         (std::to_integer<int>(hash[offset + 3] & std::byte{0xff}));

            int otp = binary % static_cast<int>(std::pow(10, digits));
            
            // Format with leading zeros
            return std::format("{:0{}}", otp, digits);
        }
    }

    /**
     * @brief Validates a Time-based One-Time Password (TOTP) token.
     *
     * Drop-in replacement using OpenSSL. Thread-safe and lock-free.
     *
     * @param seconds The time-step size (usually 30).
     * @param token The 6 or 8 digit token string to validate.
     * @param secretb32 The user's secret key, encoded in Base32.
     * @return A std::expected. On success, contains `true`. On failure, contains an error string.
     */
    [[nodiscard]] inline std::expected<bool, std::string> is_valid_token(
        const int seconds,
        std::string_view token,
        std::string_view secretb32) noexcept 
    {
        if (token.empty() || secretb32.empty()) {
            return std::unexpected("Invalid parameters: token or secret are empty");
        }
        
        int digits = static_cast<int>(token.size());
        if (digits != 6 && digits != 8) {
            return std::unexpected("Invalid token size (must be 6 or 8)");
        }

        try {
            // 1. Decode Secret
            auto secret_bytes = detail::decode_base32(secretb32);
            if (!secret_bytes.has_value()) {
                return std::unexpected("Base32 decode failed: " + secret_bytes.error());
            }

            // 2. Calculate Time Steps
            const auto now = std::chrono::system_clock::now().time_since_epoch();
            const uint64_t current_timestamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();
            const uint64_t current_step = current_timestamp / seconds;

            // 3. Check Window (Current, Previous, Next)
            // Matching liboath's 'window=1' behavior which checks +/- 1 step.
            for (uint64_t step = current_step - 1; step <= current_step + 1; ++step) {
                auto generated_otp = detail::generate_hotp(*secret_bytes, step, digits);
                
                if (!generated_otp.has_value()) {
                    return std::unexpected(generated_otp.error());
                }

                if (*generated_otp == token) {
                    return true;
                }
            }

            return std::unexpected("Token validation failed: mismatch");

        } catch (const std::exception& e) {
            return std::unexpected(std::string("An unexpected exception occurred: ") + e.what());
        }
    }

} // namespace otp

#endif // OTP_HPP
#ifndef OTP_HPP
#define OTP_HPP

#include <string>
#include <string_view>
#include <memory>
#include <chrono>
#include <format>
#include <expected>
#include <mutex>
#include <oath.h>

namespace otp {

namespace detail {
    // Custom deleter for the secret allocated by liboath.
    struct OATH_Free_Deleter {
        void operator()(char* ptr) const {
            oath_free(ptr);
        }
    };
    using unique_OATH_secret = std::unique_ptr<char, OATH_Free_Deleter>;

    // RAII class to manage the global liboath state in a thread-safe manner.
    class OathLibraryManager {
    public:
        OathLibraryManager() {
            // Lock and initialize the library only if it hasn't been done yet.
            std::scoped_lock lock(m_mutex);
            if (!m_is_initialized) {
                if (oath_init() != OATH_OK) {
                    // This is a fatal, non-recoverable error.
                    throw std::runtime_error("Failed to initialize liboath globally.");
                }
                m_is_initialized = true;
            }
        }

        ~OathLibraryManager() {
            // The last instance to be destroyed will clean up.
            // In a typical application, this happens at program exit.
            std::scoped_lock lock(m_mutex);
            if (m_is_initialized) {
                // This check is mostly for correctness; in practice, it will be true.
                oath_done();
                m_is_initialized = false;
            }
        }

    private:
        // Static members to ensure single initialization across all uses.
        inline static std::mutex m_mutex;
        inline static bool m_is_initialized = false;
    };

} // namespace detail

/**
 * @brief Validates a Time-based One-Time Password (TOTP) token.
 *
 * This function is thread-safe. It validates a given TOTP token against a
 * Base32 encoded secret, checking within a specified time window.
 *
 * @param seconds The time-step size for the TOTP algorithm (e.g., 30 or 60).
 * @param token The 6 or 8 digit token string to validate.
 * @param secretb32 The user's secret key, encoded in Base32.
 * @return A std::expected. On success, contains `true`. On failure, contains an error message string.
 */
[[nodiscard]] inline std::expected<bool, std::string> is_valid_token(
    const int seconds,
    std::string_view token,
    std::string_view secretb32) noexcept 
{
    if (token.empty() || secretb32.empty()) {
        return std::unexpected("Invalid parameters: token or secret are empty");
    }
    if (token.size() != 6 && token.size() != 8) {
        return std::unexpected("Invalid token size");
    }

    try {
        // This object ensures oath_init() has been called safely.
        // Its destruction will handle oath_done() when no more instances exist.
        detail::OathLibraryManager manager;

        char* raw_secret = nullptr;
        size_t secretlen = 0;
        int rc = oath_base32_decode(secretb32.data(), secretb32.size(), &raw_secret, &secretlen);
        
        // Use RAII for the allocated secret string.
        detail::unique_OATH_secret secret(raw_secret);

        if (rc != OATH_OK) {
            return std::unexpected(std::format("liboath oath_base32_decode() failed: {}", oath_strerror(rc)));
        }

        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

        // A window of 1 allows the token to be valid for the previous, current, and next time-step.
        constexpr int window = 1; 

        rc = oath_totp_validate(secret.get(), secretlen, now, seconds, window, nullptr, token.data());
        
        if (rc < 0) { // On failure, oath_totp_validate returns a negative error code.
             return std::unexpected(std::format("liboath oath_totp_validate() failed: {}", oath_strerror(rc)));
        }
        
        // On success, it returns the position of the matching window, which will be >= 0.
        return true;

    } catch (const std::exception& e) {
        return std::unexpected(std::string("An unexpected exception occurred: ") + e.what());
    }
}

} // namespace otp

#endif // OTP_HPP

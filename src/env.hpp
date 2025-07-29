#ifndef ENV_HPP
#define ENV_HPP

#include "pkeyutil.hpp"
#include <string>
#include <stdexcept>
#include <concepts>
#include <string_view>
#include <unordered_map>
#include <charconv>
#include <cstdlib>

namespace env {

    /**
     * @brief Exception thrown when an environment variable cannot be resolved.
     */
    class error : public std::runtime_error {
    public:
        explicit error(const std::string& message) 
            : std::runtime_error("env::get: " + message) {}
    };

    /**
     * @brief Concept for types supported by env::get.
     */
    template <typename T>
    concept Supported = std::same_as<T, std::string> ||
                        std::same_as<T, int> ||
                        std::same_as<T, long> ||
                        std::same_as<T, bool>;

    namespace detail {
        // Use an inline thread_local cache to avoid repeated getenv calls.
        inline thread_local std::unordered_map<std::string, std::string> g_cache;

        // Helper functions for type conversion
        template <Supported T>
        T convert(std::string_view value, const std::string& key);

        template <>
        inline std::string convert<std::string>(std::string_view value, const std::string&) {
            return std::string(value);
        }

        template <>
        inline int convert<int>(std::string_view value, const std::string& key) {
            int result{};
            auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
            if (ec != std::errc() || ptr != value.data() + value.size()) {
                throw error("invalid int for key '" + key + "': " + std::string(value));
            }
            return result;
        }

        template <>
        inline long convert<long>(std::string_view value, const std::string& key) {
            long result{};
            auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
            if (ec != std::errc() || ptr != value.data() + value.size()) {
                throw error("invalid long for key '" + key + "': " + std::string(value));
            }
            return result;
        }

        template <>
        inline bool convert<bool>(std::string_view value, const std::string& key) {
            if (value == "1") return true;
            if (value == "0") return false;
            throw error("invalid bool for key '" + key + "' (expected '0' or '1'): " + std::string(value));
        }

        inline std::string fetch_string(const std::string& key) {
            if (auto it = g_cache.find(key); it != g_cache.end()) {
                return it->second;
            }

            const char* raw = std::getenv(key.c_str());
            if (!raw) throw error("missing environment variable: " + key);

            std::string value = raw;
            if (value.ends_with(".enc")) {
                const auto result = decrypt(value);
                if (!result.success) {
                    throw error("decryption failed for file '" + value + "' (from key '" + key + "'): " + result.content);
                }
                value = result.content;
            }

            g_cache[key] = value;
            return value;
        }
    } // namespace detail


    /**
     * @brief Gets an environment variable with type conversion.
     */
    template <Supported T>
    [[nodiscard]] inline T get(const std::string& key) {
        const std::string value = detail::fetch_string(key);
        return detail::convert<T>(value, key);
    }

    /**
     * @brief Gets an environment variable with fallback.
     */
    template <Supported T>
    [[nodiscard]] inline T get(const std::string& key, const T& fallback) {
        try {
            return get<T>(key);
        } catch (const env::error&) {
            return fallback;
        }
    }
}

#endif // ENV_HPP

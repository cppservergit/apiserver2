#ifndef ENV_HPP
#define ENV_HPP

#include "util.hpp"
#include "pkeyutil.hpp"
#include <string>
#include <stdexcept>
#include <concepts>
#include <string_view>
#include <unordered_map>
#include <charconv>
#include <cstdlib>
#include <functional> // Required for std::equal_to

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
                        std::same_as<T, size_t> ||
                        std::same_as<T, bool>;

    namespace detail {

        // Replaced the global variable with a function that returns a reference
        // to a static thread_local cache, now using the transparent hasher.
        inline std::unordered_map<std::string, std::string, util::string_hash, util::string_equal>& get_cache() noexcept {
            static thread_local std::unordered_map<std::string, std::string, util::string_hash, util::string_equal> g_cache;
            return g_cache;
        }

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
        inline size_t convert<size_t>(std::string_view value, const std::string& key) {
            size_t result{};
            // Note: std::from_chars is strict and does not skip whitespace.
            auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
            if (ec != std::errc() || ptr != value.data() + value.size()) {
                throw error("invalid size_t for key '" + key + "': " + std::string(value));
            }
            return result;
        }

        template <>
        inline bool convert<bool>(std::string_view value, const std::string& key) {
            if (value == "1") return true;
            if (value == "0") return false;
            throw error("invalid bool for key '" + key + "' (expected '0' or '1'): " + std::string(value));
        }

        inline std::string fetch_string(std::string_view key) {
            auto& cache = get_cache();
            // This find operation is now transparent and avoids a string allocation on cache hits.
            if (auto it = cache.find(key); it != cache.end()) {
                return it->second;
            }

            // std::getenv requires a null-terminated string, so we must create a 
            // temporary std::string here for the C-API call and for potential insertion.
            const std::string key_str(key);
            const char* raw = std::getenv(key_str.c_str());
            if (!raw) throw error("missing environment variable: " + key_str);

            std::string value = raw;
            if (value.ends_with(".enc")) {
                const auto result = decrypt(value);
                if (!result.success) {
                    throw error("decryption failed for file '" + value + "' (from key '" + key_str + "'): " + result.content);
                }
                value = result.content;
            }

            cache[key_str] = value;
            return value;
        }
    } // namespace detail


    /**
     * @brief Gets an environment variable with type conversion.
     */
    template <Supported T>
    [[nodiscard]] inline T get(const std::string& key) {
        // The std::string key is implicitly converted to a std::string_view for fetch_string.
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

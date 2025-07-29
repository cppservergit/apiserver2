#ifndef WEBAPI_PATH_HPP
#define WEBAPI_PATH_HPP

#include <string_view>
#include <stdexcept>
#include <string>
#include <algorithm>

// A simple exception class for compile-time errors
class consteval_error : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

/**
 * @class webapi_path
 * @brief A compile-time validated URI path for a REST API endpoint.
 *
 * This class uses a consteval constructor to ensure that all API paths are
 * checked for correctness at compile time, preventing a class of runtime errors.
 */
struct webapi_path {
public:
    /**
     * @brief Constructs and validates the path at compile time.
     * @param path The URI path string.
     * @throws consteval_error if the path is invalid.
     */
    consteval explicit webapi_path(std::string_view path) : m_path{path} {
        if (path.empty() || !path.starts_with('/')) {
            throw consteval_error("Invalid WebAPI path: must start with '/'");
        }
        if (path.length() > 1 && path.ends_with('/')) {
            throw consteval_error("Invalid WebAPI path: cannot end with '/'");
        }
        
        constexpr std::string_view valid_chars{"abcdefghijklmnopqrstuvwxyz_-0123456789/"};
        for(const char c : path) {
            if (valid_chars.find(c) == std::string_view::npos) {
                throw consteval_error("Invalid WebAPI path: contains an invalid character");
            }
        }
    }
    
    // Allow implicit conversion to string_view for convenience
    /* NOSONAR */
    [[nodiscard]] explicit constexpr operator std::string_view() const noexcept {
        return m_path;
    }

    [[nodiscard]] constexpr std::string_view get() const noexcept {
        return m_path;
    }

private:
    std::string_view m_path;
};

#endif // WEBAPI_PATH_HPP

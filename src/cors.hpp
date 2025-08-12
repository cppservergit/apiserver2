#ifndef CORS_HPP
#define CORS_HPP

#include <string>
#include <string_view>
#include <optional>
#include <unordered_set>
#include <functional> // For std::equal_to
#include "util.hpp"

namespace cors {

/**
 * @brief Checks if a given request origin is present in the set of allowed origins.
 * @param origin The value of the 'Origin' header from the http::request.
 * @param allowed_origins A reference to the server's configured set of allowed origins,
 * which must use a transparent hasher and comparator.
 * @return `true` if the origin is permitted, `false` otherwise.
 */
[[nodiscard]] inline bool is_origin_allowed(
    const std::optional<std::string_view>& origin,
    const std::unordered_set<std::string, util::string_hash, util::string_equal>& allowed_origins)
{
    // If there is no Origin header, it's not a cross-origin request. Allow.
    if (!origin.has_value()) {
        return true;
    }

    // With the transparent hasher, we can now use the string_view directly for the lookup,
    // avoiding the creation of a temporary std::string.
    return allowed_origins.contains(*origin);
}

} // namespace cors

#endif // CORS_HPP

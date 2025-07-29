#ifndef CORS_HPP
#define CORS_HPP

#include <string>
#include <string_view>
#include <optional>
#include <unordered_set>

namespace cors {

/**
 * @brief Checks if a given request origin is present in the set of allowed origins.
 * * This function implements the core logic for CORS validation.
 * - If the request does not contain an 'Origin' header, it is not a CORS request, and is allowed to proceed.
 * - If the 'Origin' header is present, its value must be an exact match to one of the
 * entries in the `allowed_origins` set.
 * * @param origin The value of the 'Origin' header from the http::request.
 * @param allowed_origins A reference to the server's configured set of allowed origin strings.
 * @return `true` if the origin is permitted, `false` otherwise.
 */
[[nodiscard]] inline bool is_origin_allowed(
    const std::optional<std::string_view>& origin,
    const std::unordered_set<std::string>& allowed_origins)
{
    // If there is no Origin header, it's not a cross-origin request. Allow.
    if (!origin.has_value()) {
        return true;
    }

    // If the Origin header is present, its value must exist in our set of allowed origins.
    // The .contains() method on std::unordered_set is efficient for this lookup.
    // Note: .contains() requires the key type to be compatible, which std::string is for a
    // set of std::strings. We dereference the optional to get the string_view, which can
    // be implicitly converted to a std::string for the lookup.
    return allowed_origins.contains(std::string(*origin));
}

} // namespace cors

#endif // CORS_HPP

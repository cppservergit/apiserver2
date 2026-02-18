#ifndef MFA_HPP
#define MFA_HPP

#include "server.hpp"
#include "logger.hpp"
#include "sql.hpp"
#include "jwt.hpp"
#include <string>
#include <string_view>
#include <optional>
#include <array>

// --- Helper Functions for TOTP Logic ---

[[nodiscard]] inline std::optional<std::string> fetch_user_secret(std::string_view user) {
    auto rs = sql::query("LOGINDB", "{CALL cpp_get_secret(?)}", user);
    if (rs.empty()) return std::nullopt;
    
    // Check if the column "totp_secret" exists or has content
    auto secret = rs.at(0).get_value<std::string>("totp_secret");
    if (secret.empty()) return std::nullopt;
    
    return secret;
}

[[nodiscard]] inline std::optional<std::string> generate_post_auth_token(const jwt::claims_map& claims, std::string_view user) {
    // Construct new claims list excluding "preauth"
    jwt::claims_map new_claims;
    static constexpr std::array<std::string_view, 4> keys_to_copy = {
        "user", "email", "roles", "sessionId"
    };

    // Cleaner loop without nested if/try_emplace logic block
    for (const auto& key : keys_to_copy) {
        // Safe search using string_view (requires transparent comparator in map)
        if (auto it = claims.find(std::string(key)); it != claims.end()) {
            new_claims.try_emplace(std::string(key), it->second);
        }
    }

    auto token_result = jwt::get_token(new_claims);
    if (!token_result.has_value()) {
        util::log::error("Failed to generate final system token for user {}", user);
        return std::nullopt;
    }
    return *token_result;
}

#endif // MFA_HPP
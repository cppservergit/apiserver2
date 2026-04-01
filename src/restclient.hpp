#ifndef RESTCLIENT_HPP
#define RESTCLIENT_HPP

#include "http_client.hpp"
#include "json_parser.hpp"
#include "util.hpp"
#include "jwt.hpp"
#include <string>
#include <string_view>
#include <map>
#include <format>
#include <stdexcept>
#include <chrono>

/**
 * @brief Custom exception for errors originating from the RemoteCustomerService.
 */
class RemoteServiceError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/**
 * @class RemoteCustomerService
 * @brief A stateless service class to interact with a remote customer API.
 */
class RemoteCustomerService {
public:
    /**
     * @brief Fetches customer information from the remote API.
     * @param req The incoming HTTP request (used to extract tracing headers like x-request-id).
     * @param customer_id The ID of the customer to fetch.
     */
    static http_response get_customer_info(const http::request& req, std::string_view customer_id) {
        const std::string uri = "/api/customer";
        
        // This will use the cached token if valid
        const std::string token = login_and_get_token(req);
        
        const std::map<std::string, std::string, std::less<>> payload = {
            {"id", std::string(customer_id)}
        };
        const std::string body = json::json_parser::build(payload);
        
        std::map<std::string, std::string, std::less<>> headers = {
            {"Authorization", std::format("Bearer {}", token)},
            {"Content-Type", "application/json"}
        };

        // Propagate x-request-id if present in the original request
        if (auto request_id_opt = req.get_header_value("x-request-id"); request_id_opt) {
            headers.try_emplace("x-request-id", *request_id_opt);
        }

        util::log::debug("Fetching remote customer info from {} with payload {}", uri, body);
        
        // Use thread-local client member to reuse connections (Keep-Alive)
        const auto response = m_client.post(get_url() + uri, body, headers);
        if (response.status_code != 200) {
            util::log::error("Remote API {} failed with status {}: {}", uri, response.status_code, response.body);
            // If we get a 401, we might want to invalidate the cache, but for now simple error handling
            if (response.status_code == 401) {
                m_session.token.clear(); // Force re-login next time
            }
            throw RemoteServiceError("Remote service invocation failed.");
        }
        return response;
    }

private:
    struct Session {
        std::string token;
        std::chrono::system_clock::time_point expires_at;
    };

    /**
     * @brief Authenticates with the remote API and returns a JWT. 
     * Uses a lock-free thread_local cache to store the token for 3 minutes.
     */
    static std::string login_and_get_token(const http::request& req) {
        const auto now = std::chrono::system_clock::now();

        // Check cache: if token exists and hasn't expired, return it.
        if (!m_session.token.empty() && now < m_session.expires_at) {
            return m_session.token;
        }

        // --- Perform Login ---
        const std::map<std::string, std::string, std::less<>> login_payload = {
            {"username", get_user()},
            {"password", get_pass()}
        };
        const std::string login_body = json::json_parser::build(login_payload);

        util::log::debug("Logging into remote API at {}", get_url());

        std::map<std::string, std::string, std::less<>> headers = {{"Content-Type", "application/json"}};
        
        if (auto request_id_opt = req.get_header_value("x-request-id"); request_id_opt) {
            headers.try_emplace("x-request-id", *request_id_opt);
        }

        // Reuse the thread-local client member
        const http_response login_response = m_client.post(get_url() + "/api/login", login_body, headers);

        if (login_response.status_code != 200) {
            util::log::error("Remote API login failed with status {}: {}", login_response.status_code, login_response.body);
            throw RemoteServiceError("Failed to authenticate with remote service.");
        }

        json::json_parser token_parser(login_response.body);
        const std::string id_token(token_parser.get_string("id_token"));

        if (id_token.empty()) {
            util::log::error("Remote API login response did not contain an id_token.");
            throw RemoteServiceError("Invalid response from remote authentication service.");
        }

        // Parse JWT to get expiration
        auto claims_result = jwt::get_claims(id_token);
        if (!claims_result) {
            util::log::error("Failed to parse JWT claims from remote token: {}", jwt::to_string(claims_result.error()));
            throw RemoteServiceError("Invalid JWT token received from remote service.");
        }

        const auto& claims = *claims_result;
        auto exp_it = claims.find("exp");
        if (exp_it == claims.end()) {
            util::log::error("Remote JWT does not contain an 'exp' claim.");
            throw RemoteServiceError("Insecure JWT received from remote service (no expiration).");
        }

        try {
            // Convert 'exp' (seconds since epoch) to time_point
            uint64_t exp_timestamp = std::stoull(exp_it->second);
            auto expires_at = std::chrono::system_clock::time_point{std::chrono::seconds{exp_timestamp}};

            // Update thread-local cache
            m_session.token = id_token;
            m_session.expires_at = expires_at;

        } catch (const std::exception& e) {
            util::log::error("Failed to parse 'exp' claim: {}", e.what());
            throw RemoteServiceError("Invalid 'exp' format in remote JWT.");
        }

        return id_token;
    }

    // --- Thread-Safe Persistent Client Member (C++17 inline static) ---
    // This avoids the "function-local static" warning and provides a clean thread-local instance per thread.
    static inline thread_local http_client m_client{};
    
    // --- Thread-Safe Session Cache ---
    // Stores the token and its creation time per thread to avoid mutexes.
    static inline thread_local Session m_session{};

    // --- Configuration Getters using thread-safe function-local statics ---
    // Assuming env::get is available via "util.hpp" or similar included headers
    static const std::string& get_url()  { static const std::string url = env::get<std::string>("REMOTE_API_URL"); return url; }
    static const std::string& get_user() { static const std::string user = env::get<std::string>("REMOTE_API_USER"); return user; }
    static const std::string& get_pass() { static const std::string pass = env::get<std::string>("REMOTE_API_PASS"); return pass; }
};

#endif // RESTCLIENT_HPP
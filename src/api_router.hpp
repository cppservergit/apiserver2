#ifndef API_ROUTER_HPP
#define API_ROUTER_HPP

#include "http_request.hpp"
#include "http_response.hpp"
#include "input_validator.hpp"
#include "webapi_path.hpp"
#include <string_view>
#include <unordered_map>
#include <functional>
#include <memory>

// A type alias for our API handler functions
using api_handler_func = std::function<void(const http::request&, http::response&)>;

// A type-erased wrapper for our validation logic
using validator_func = std::function<void(const http::request&)>;

/**
 * @struct api_endpoint
 * @brief Holds all the information for a registered API endpoint.
 */
struct api_endpoint {
    http::method method;
    validator_func validator;
    api_handler_func handler;
    bool is_secure;
};

/**
 * @class api_router
 * @brief The central catalog for registering and looking up API endpoints.
 */
class api_router {
public:
    /**
     * @brief Registers a new API endpoint.
     */
    template<typename Validator>
    void register_api(webapi_path path, http::method method, const Validator& v, api_handler_func handler, bool is_secure = true) {
        validator_func vf = [v](const http::request& req) {
            v.validate(req);
        };
        m_routes[path.get()] = {method, std::move(vf), std::move(handler), is_secure};
    }

    /**
     * @brief Registers an API endpoint that has no validation rules.
     */
    void register_api(webapi_path path, http::method method, api_handler_func handler, bool is_secure = true) {
        validator_func vf = [](const http::request&){};
        m_routes[path.get()] = {method, std::move(vf), std::move(handler), is_secure};
    }

    /**
     * @brief Finds the handler for a given request path.
     */
    [[nodiscard]] const api_endpoint* find_handler(std::string_view path) const {
        if (auto it = m_routes.find(path); it != m_routes.end()) {
            return &it->second;
        }
        return nullptr;
    }

private:
    std::unordered_map<std::string_view, api_endpoint> m_routes;
};

#endif // API_ROUTER_HPP

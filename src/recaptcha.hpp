#ifndef RECAPTCHA_HPP
#define RECAPTCHA_HPP

#include "http_request.hpp"
#include "http_response.hpp"
#include "http_client.hpp"
#include "json_parser.hpp"
#include "env.hpp"
#include "logger.hpp"
#include <string>
#include <format>
#include <expected>

/**
 * @brief Retrieves the reCAPTCHA secret key from environment variables.
 * @return The secret key as a string, or an error if not found.
 */
[[nodiscard]] static auto get_recaptcha_secret() noexcept -> std::expected<std::string, std::string> {
    try {
        return env::get<std::string>("RECAPTCHA_SECRET_KEY");
    } catch (const env::error& e) {
        return std::unexpected(e.what());
    }
}

/**
 * @brief Processes the reCAPTCHA response from Google.
 * @param response_body The JSON response body from Google.
 * @param remote_ip The IP address of the user who submitted the request.
 * @param min_score The minimum acceptable reCAPTCHA score.
 * @param res The HTTP response to be populated.
 */
static void process_google_response(std::string_view response_body, 
                                   std::string_view remote_ip, 
                                   double min_score, 
                                   http::response& res) {
    using enum http::status;
    try {
        json::json_parser parser(response_body);
        bool success = parser.get<bool>("success");
        double score = parser.get<double>("score");

        if (success && score >= min_score) {
            util::log::info("reCAPTCHA verified for IP {} with score {}", remote_ip, score);
            res.set_body(ok, response_body);
        } else {
            util::log::warn("reCAPTCHA validation failed for IP {}: success={}, score={} (min required: {})", 
                           remote_ip, success, score, min_score);
            res.set_body(bad_request, response_body);
        }
    } catch (const json::parsing_error& e) {
        util::log::error("Failed to parse Google reCAPTCHA response: {}", e.what());
        res.set_body(internal_server_error, R"({"success":false, "error-codes":["parse-error"]})");
    }
}

/**
 * @brief Handles Google reCAPTCHA V3 verification.
 * 
 * This function extracts the reCAPTCHA token from the request,
 * validates it against Google's API using the secret key from the
 * RECAPTCHA_SECRET_KEY environment variable, and sets the response.
 * 
 * @param req The incoming HTTP request (expects 'token' parameter).
 * @param res The HTTP response to be populated.
 */
inline void verify_recaptcha(const http::request& req, http::response& res) {
    using enum http::status;
    static constexpr double MIN_RECAPTCHA_SCORE = 0.5;

    try {
        // 1. Get the reCAPTCHA token from the request
        const std::string token = req.get_required_param<std::string>("token");

        // 2. Get the secret key from environment
        auto secret_res = get_recaptcha_secret();
        if (!secret_res) {
            util::log::error("reCAPTCHA configuration error: {}", secret_res.error());
            res.set_body(internal_server_error, R"({"success":false, "error-codes":["server-configuration-error"]})");
            return;
        }

        // 3. Prepare the request to Google
        std::string post_body = std::format("secret={}&response={}&remoteip={}", 
                                           *secret_res, token, req.get_remote_ip());

        http_client client;
        http_response google_res = client.post(
            "https://www.google.com/recaptcha/api/siteverify",
            post_body,
            {{"Content-Type", "application/x-www-form-urlencoded"}}
        );

        if (google_res.status_code != 200) {
            util::log::error("Google reCAPTCHA API returned status {}", google_res.status_code);
            res.set_body(internal_server_error, R"({"success":false, "error-codes":["google-api-error"]})");
            return;
        }

        // 4. Parse and return the response
        process_google_response(google_res.body, req.get_remote_ip(), MIN_RECAPTCHA_SCORE, res);

    } catch (const curl_exception& e) {
        util::log::error("reCAPTCHA API communication failed: {}", e.what());
        res.set_body(internal_server_error, R"({"success":false, "error-codes":["google-api-error"]})");
    } 
}

#endif // RECAPTCHA_HPP

#include "http_request.hpp"
#include "http_response.hpp"
#include "input_validator.hpp"
#include "logger.hpp"
#include "metrics.hpp"
#include <string_view>
#include <vector>
#include <cstring>
#include <iomanip>
#include <functional>
#include <string>
#include <format>
#include <map>
#include <thread>

using namespace std::literals::string_view_literals;
using namespace validation;

// --- Validator Definition ---
const validator registration_validator{
    rule<std::string>{"username", requirement::required},
    rule<std::string>{"email", requirement::required, [](const std::string& s) { return s.contains('@'); }, "Email must contain an '@' symbol."},
    rule<int>{"age", requirement::optional, [](int age) { return age >= 18; }, "User must be 18 or older."}
};


// --- Test Functions ---
void process_request(http::request& req) {
    util::log::info("--- [WORKER] Processing Basic Request ---");
    util::log::info("Method: {}, Path: {}", req.get_method_str(), req.get_path());
    
    http::response res;
    res.set_body(http::status::ok, "{\"status\":\"success\"}");
    util::log::info("Generated Response:\n---\n{}\n---", std::string_view(res.buffer().data(), res.buffer().size()));

    util::log::info("--- [WORKER] Basic Processing Complete ---");
}

void simulate_io_loop(
    std::string_view request_title, 
    const std::string& request_data,
    size_t chunk_size,
    const std::function<void(http::request&)>& worker_func) 
{
    util::log::info("====================================================");
    util::log::info("Simulating: {}", request_title);
    util::log::info("====================================================");

    http::request_parser parser;
    try {
        size_t bytes_sent = 0;
        while (bytes_sent < request_data.size()) {
            auto buffer = parser.get_buffer();
            size_t bytes_to_read = std::min({chunk_size, request_data.size() - bytes_sent, buffer.size()});
            
            util::log::debug("[IO LOOP] Reading {} bytes...", bytes_to_read);
            std::memcpy(buffer.data(), request_data.data() + bytes_sent, bytes_to_read);
            parser.update_pos(bytes_to_read);
            bytes_sent += bytes_to_read;

            if (parser.eof()) {
                util::log::info("[IO LOOP] EOF detected! Request is complete.");
                if (auto result = parser.finalize(); !result) throw result.error();
                
                http::request final_request(std::move(parser));
                worker_func(final_request);
                break; 
            }
        }
    } catch (const std::exception& e) {
        util::log::error("*** An error occurred: {} ***", e.what());
    }
}

// Helper to build a request string with a correct Content-Length
std::string build_post_request(std::string_view host, std::string_view path, std::string_view content_type, std::string_view body) {
    return std::format(
        "POST {} HTTP/1.1\r\n"
        "Host: {}\r\n"
        "Content-Type: {}\r\n"
        "Content-Length: {}\r\n"
        "\r\n"
        "{}",
        path, host, content_type, body.length(), body
    );
}


int main() {
    util::log::info("Starting HTTP parser and validator test harness...");

    // --- Basic & Bad Request Tests ---
    const std::string get_request_raw = "GET /search?q=cpp&lang=en HTTP/1.1\r\nHost: example.com\r\n\r\n";
    simulate_io_loop("Simple GET Request", get_request_raw, 50, process_request);
    
    // ... (other tests unchanged) ...

    // --- Input Validation Tests ---
    util::log::info("\n\n--- Running Input Validation Tests ---");

    // FIX: Added [[maybe_unused]] to the 'req' parameter to silence the compiler warning.
    auto validation_worker = [](http::request& [[maybe_unused]] req) {
        util::log::info("[WORKER] Attempting to validate request for path: {}", req.get_path());
        
        http::response res;

        try {
            if(auto body = req.get_body(); std::holds_alternative<std::string_view>(body)) {
                 util::log::debug("DEBUG JSON: {}", std::get<std::string_view>(body));
            }
            registration_validator.validate(req);
            util::log::info("SUCCESS: Request passed all validation rules!");

            std::string success_body = json::json_parser::build({{"status", "ok"}, {"message", "User registered"}});
            res.set_body(http::status::ok, success_body);

        } catch (const validation_error& e) {
            util::log::warn("FAILURE: Request failed validation.");
            util::log::warn("  - Parameter: {}", e.get_param_name());
            util::log::warn("  - Details: {}", e.get_details());

            std::string error_body = json::json_parser::build({
                {"status", "error"}, 
                {"error", "validation_failed"},
                {"parameter", e.get_param_name()},
                {"details", e.get_details()}
            });
            res.set_body(http::status::bad_request, error_body);
        }
        
        util::log::info("Generated Response:\n---\n{}\n---", std::string_view(res.buffer().data(), res.buffer().size()));
    };

    const std::string validation_success_raw = build_post_request("a.com", "/register", "application/json", "{\"username\":\"valid_user\",\"email\":\"a@b.com\",\"age\":25}");
    const std::string validation_missing_param_raw = build_post_request("a.com", "/register", "application/json", "{\"username\":\"no_email_user\"}");
    const std::string validation_custom_fail_raw = build_post_request("a.com", "/register", "application/json", "{\"username\":\"young_user\",\"email\":\"c@d.com\",\"age\":16}");
    const std::string validation_format_fail_raw = build_post_request("a.com", "/register", "application/json", "{\"username\":\"bad_age\",\"email\":\"e@f.com\",\"age\":\"twenty\"}");

    simulate_io_loop("Validation Test: SUCCESS", validation_success_raw, 4096, validation_worker);
    simulate_io_loop("Validation Test: MISSING PARAM", validation_missing_param_raw, 4096, validation_worker);
    simulate_io_loop("Validation Test: CUSTOM RULE FAIL", validation_custom_fail_raw, 4096, validation_worker);
    simulate_io_loop("Validation Test: FORMAT FAIL", validation_format_fail_raw, 4096, validation_worker);


    // --- Metrics Class Test ---
    util::log::info("\n\n--- Running Metrics Class Test ---");
    {
        metrics m(8); // pool size of 8
        m.increment_connections();
        m.increment_connections();
        m.increment_active_threads();
        m.record_request_time(std::chrono::microseconds(50000)); // 50ms
        m.record_request_time(std::chrono::microseconds(150000)); // 150ms
        m.decrement_connections();
        
        util::log::info("Metrics JSON output:\n{}", m.to_json());
    }


    util::log::info("\nTest harness finished.");
    return 0;
}

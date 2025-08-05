#include "server.hpp"
#include "logger.hpp"
#include "webapi_path.hpp"
#include "sql.hpp"
#include "input_validator.hpp"
#include "util.hpp"
#include "json_parser.hpp"
#include "jwt.hpp"
#include "http_client.hpp"
#include <functional>
#include <algorithm> 
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <string_view> 
#include <ranges>      

// Use namespaces to make code less verbose
using namespace validation;
using enum http::status;
using enum http::method;

// --- Custom Exception for File Operations ---
class file_system_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};


// --- Validators ---
const validator customer_validator{
    rule<std::string>{"id", requirement::required, 
        [](std::string_view s) {
            return s.length() == 5 && std::ranges::all_of(s, [](unsigned char c){ return std::isalpha(c); });
        }, 
        "Customer ID must be exactly 5 alphabetic characters."
    }
};

const validator login_validator{
    rule<std::string>{"username", requirement::required, [](std::string_view s) { return s.length() >= 6 && !s.contains(' '); }, "User must be at least 6 characters long and contain no spaces."},
    rule<std::string>{"password", requirement::required, [](std::string_view s) { return s.length() >= 6 && !s.contains(' '); }, "Password must be at least 6 characters long and contain no spaces."}
};

const validator sales_validator{
    rule<std::chrono::year_month_day>{"start_date", requirement::required},
    rule<std::chrono::year_month_day>{"end_date", requirement::required}
};

const validator upload_validator{
    rule<std::string>{"title", requirement::required}
};


// --- User-Defined API Handlers ---
void hello_world([[maybe_unused]] const http::request& req, http::response& res) {
    res.set_body(ok, R"({"message":"Hello, World!"})");
}

void get_shippers([[maybe_unused]] const http::request& req, http::response& res) {
    res.set_body(ok, sql::get("DB1", "{CALL sp_shippers_view}").value_or("[]"));
}

void get_products([[maybe_unused]] const http::request& req, http::response& res) {
    res.set_body(ok, sql::get("DB1", "{CALL sp_products_view}").value_or("[]"));
}

void get_customer(const http::request& req, http::response& res) {
    auto id_result = req.get_value<std::string>("id");
    const std::string& customer_id = **id_result;
    const auto json_result = sql::get("DB1", "{CALL sp_customer_get(?)}", customer_id);
    res.set_body(
        json_result ? ok : not_found,
        json_result.value_or(R"({"error":"Customer not found"})")
    );
    if (auto claims = jwt::get_claims(req.get_bearer_token().value_or("")); claims.has_value()) {
        util::log::info("Information requested by user '{}' for customer: '{}'", claims->at("user"), customer_id);
    }
}

void login(const http::request& req, http::response& res) {
    const auto user = **req.get_value<std::string>("username");
    const auto password = **req.get_value<std::string>("password");
    const std::string session_id = util::get_uuid();
    const std::string_view remote_ip = req.get_remote_ip();

    sql::resultset rs = sql::query("LOGINDB", "{CALL cpp_dblogin(?,?,?,?)}", user, password, session_id, remote_ip);

    if (rs.empty()) {
        res.set_body(unauthorized, R"({"error":"Invalid credentials"})");
        return;
    }

    const auto& row = rs.at(0);
    if (row.get_value<std::string>("status") == "INVALID") {
        const std::string error_code = row.get_value<std::string>("error_code");
        const std::string error_desc = row.get_value<std::string>("error_description");
        util::log::warn("Login failed for user '{}' from {}: {} - {}", user, remote_ip, error_code, error_desc);
        res.set_body(unauthorized, std::format(R"({{"error":"{}", "description":"{}"}})", error_code, error_desc));
    } else {
        const std::string email = row.get_value<std::string>("email");
        const std::string display_name = row.get_value<std::string>("displayname");
        const std::string role_names = row.get_value<std::string>("rolenames");

        auto token_result = jwt::get_token({
            {"user", user},
            {"email", email},
            {"roles", role_names},
            {"sessionId", session_id}
        });

        if (!token_result) {
            util::log::error("JWT creation failed for user '{}': {}", user, jwt::to_string(token_result.error()));
            res.set_body(internal_server_error, R"({"error":"Could not generate session token."})");
            return;
        }

        const std::map<std::string, std::string, std::less<>> response_data = {
            {"displayname", display_name},
            {"token_type", "bearer"},
            {"id_token", *token_result}
        };
        std::string success_body = json::json_parser::build(response_data);

		util::log::info("Login OK for user '{}': sessionId {} - from {}", user, session_id, remote_ip);

        res.set_body(ok, success_body);
    }
}

void get_sales_by_category(const http::request& req, http::response& res) {
    auto start_date = **req.get_value<std::chrono::year_month_day>("start_date");
    auto end_date = **req.get_value<std::chrono::year_month_day>("end_date");

    if (start_date >= end_date) {
        res.set_body(bad_request, R"({"error":"start_date must be before end_date"})");
        return;
    }

    // FIX (Issue #11): Simplify optional handling with value_or.
    res.set_body(ok, sql::get("DB1", "{CALL sp_sales_by_category(?,?)}", start_date, end_date).value_or("[]"));
}

void upload_file(const http::request& req, http::response& res) {
    using enum http::status;
    const auto blob_path_str = env::get<std::string>("BLOB_PATH", "");
    if (blob_path_str.empty()) {
        util::log::error("BLOB_PATH environment variable is not set.");
        res.set_body(internal_server_error, R"({"error":"File upload is not configured on the server."})");
        return;
    }
    const std::filesystem::path blob_path(blob_path_str);

    const http::multipart_item* file_part = req.get_file_upload("file1");
    if (!file_part) {
        res.set_body(bad_request, R"({"error":"Missing 'file1' part in multipart form data."})");
        return;
    }
    
    const auto title = **req.get_value<std::string>("title");

    try {
        std::filesystem::create_directories(blob_path);
        const std::filesystem::path original_filename(file_part->filename);
        const std::string new_filename = util::get_uuid() + original_filename.extension().string();
        const std::filesystem::path dest_path = blob_path / new_filename;

        util::log::info("Saving uploaded file '{}' as '{}' with title '{}'", file_part->filename, dest_path.string(), title);

        std::ofstream out_file(dest_path, std::ios::binary);
        if (!out_file) {
            // FIX (Issue #12): Throw the dedicated exception type.
            throw file_system_error(std::format("Could not open destination file for writing: {}", util::str_error_cpp(errno)));
        }
        
        out_file.write(file_part->content.data(), file_part->content.size());
        if (!out_file) {
            // FIX (Issue #12): Throw the dedicated exception type.
            throw file_system_error(std::format("An error occurred while writing to the destination file: {}", util::str_error_cpp(errno)));
        }
        
        out_file.close();

        sql::exec(
            "DB1",
            "{call sp_blob_add(?, ?, ?, ?, ?)}",
            title,
            new_filename,
            file_part->filename,
            file_part->content_type,
            file_part->content.size()
        );

        const std::map<std::string, std::string, std::less<>> response_data = {
            {"status", "ok"},
            {"title", title},
            {"originalFilename", std::string(file_part->filename)},
            {"savedFilename", new_filename},
            {"size", std::to_string(file_part->content.size())}
        };
        std::string success_body = json::json_parser::build(response_data);
        res.set_body(ok, success_body);

    } catch (const file_system_error& e) {
        util::log::error("File upload failed: {}", e.what());
        res.set_body(internal_server_error, R"({"error":"Failed to save uploaded file."})");
    }
}


int main() {
    try {
        util::log::info("Application starting...");

        server s;
        
        //
        s.register_api(webapi_path{"/hello"}, get, &hello_world, false);
        s.register_api(webapi_path{"/login"}, post, login_validator, &login, false);
        s.register_api(webapi_path{"/shippers"}, get, &get_shippers, true);
        s.register_api(webapi_path{"/products"}, get, &get_products, true);
        s.register_api(webapi_path{"/customer"}, get, customer_validator, &get_customer, true);
        s.register_api(webapi_path{"/sales"}, post, sales_validator, &get_sales_by_category, true);
        s.register_api(webapi_path{"/upload"}, post, upload_validator, &upload_file, true);

        s.start();

        util::log::info("Application shutting down gracefully.");

    // FIX (Issue #16): Catch the more specific, dedicated exception.
    } catch (const file_system_error& e) {
        util::log::critical("A critical file system error occurred: {}", e.what());
        return 1;
    } catch (const server_error& e) {
        util::log::critical("A critical server error occurred: {}", e.what());
        return 1;
    } catch (const std::exception& e) {
        util::log::critical("An unexpected error occurred: {}", e.what());
        return 1;
    } catch (...) {
        util::log::critical("An unknown error occurred.");
        return 1;
    }

    return 0;
}


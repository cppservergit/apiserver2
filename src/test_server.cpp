#include "server.hpp"
#include "logger.hpp"
#include "webapi_path.hpp"
#include "sql.hpp"
#include "input_validator.hpp"
#include "util.hpp"
#include "json_parser.hpp"
#include "jwt.hpp"
#include <functional>
#include <algorithm> 
#include <chrono>
#include <filesystem> // For file path operations
#include <fstream>    // For file writing
#include <cctype>     // For std::isalpha

// Use namespaces to make code less verbose
using namespace validation;

// --- Validators ---

const validator customer_validator{
    rule<std::string>{"id", requirement::required, 
        [](const std::string& s) {
            // The ID must be 5 characters long and all characters must be alphabetic.
            return s.length() == 5 && std::all_of(s.begin(), s.end(), [](unsigned char c){ return std::isalpha(c); });
        }, 
        "Customer ID must be exactly 5 alphabetic characters."
    }
};

const validator login_validator{
    rule<std::string>{"username", requirement::required, [](const std::string& s) { return s.length() >= 6 && s.find(' ') == std::string::npos; }, "User must be at least 6 characters long and contain no spaces."},
    rule<std::string>{"password", requirement::required, [](const std::string& s) { return s.length() >= 6 && s.find(' ') == std::string::npos; }, "Password must be at least 6 characters long and contain no spaces."}
};

const validator sales_validator{
    rule<std::chrono::year_month_day>{"start_date", requirement::required},
    rule<std::chrono::year_month_day>{"end_date", requirement::required}
};

// Validator for the file upload API.
const validator upload_validator{
    rule<std::string>{"title", requirement::required}
};


// --- User-Defined API Handlers ---

void hello_world([[maybe_unused]] const http::request& req, http::response& res) {
    res.set_body(http::status::ok, R"({"message":"Hello, World!"})");
}

void get_shippers([[maybe_unused]] const http::request& req, http::response& res) {
    if (auto json_result = sql::get("DB1", "{CALL sp_shippers_view}")) {
        res.set_body(http::status::ok, *json_result);
    } else {
        res.set_body(http::status::ok, "[]");
    }
}

void get_products([[maybe_unused]] const http::request& req, http::response& res) {
    if (auto json_result = sql::get("DB1", "{CALL sp_products_view}")) {
        res.set_body(http::status::ok, *json_result);
    } else {
        res.set_body(http::status::ok, "[]");
    }
}

void get_customer(const http::request& req, http::response& res) {
    auto id_result = req.get_value<std::string>("id");
    const std::string& customer_id = **id_result;

    if (auto json_result = sql::get("DB1", "{CALL sp_customer_get(?)}", customer_id)) {
        res.set_body(http::status::ok, *json_result);
    } else {
        res.set_body(http::status::not_found, R"({"error":"Customer not found"})");
    }
}

void login(const http::request& req, http::response& res) {
    const auto user = **req.get_value<std::string>("username");
    const auto password = **req.get_value<std::string>("password");
    const std::string session_id = util::get_uuid();
    const std::string_view remote_ip = req.get_remote_ip();

    sql::resultset rs = sql::query("LOGINDB", "{CALL cpp_dblogin(?,?,?,?)}", user, password, session_id, remote_ip);

    if (rs.empty()) {
        res.set_body(http::status::unauthorized, R"({"error":"Invalid credentials"})");
        return;
    }

    const auto& row = rs.at(0);
    if (row.get_value<std::string>("status") == "INVALID") {
        const std::string error_code = row.get_value<std::string>("error_code");
        const std::string error_desc = row.get_value<std::string>("error_description");
        util::log::warn("Login failed for user '{}' from {}: {} - {}", user, remote_ip, error_code, error_desc);
        res.set_body(http::status::unauthorized, std::format(R"({{"error":"{}", "description":"{}"}})", error_code, error_desc));
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
            res.set_body(http::status::internal_server_error, R"({"error":"Could not generate session token."})");
            return;
        }

        std::string success_body = json::json_parser::build({
            {"displayname", display_name},
            {"token_type", "bearer"},
            {"id_token", *token_result}
        });

		util::log::info("Login OK for user '{}': sessionId {} - from {}", user, session_id, remote_ip);

        res.set_body(http::status::ok, success_body);
    }
}

void get_sales_by_category(const http::request& req, http::response& res) {
    auto start_date = **req.get_value<std::chrono::year_month_day>("start_date");
    auto end_date = **req.get_value<std::chrono::year_month_day>("end_date");

    if (start_date >= end_date) {
        res.set_body(http::status::bad_request, R"({"error":"start_date must be before end_date"})");
        return;
    }

    // The sql::get function now handles the std::chrono::year_month_day to string conversion automatically.
    if (auto json_result = sql::get("DB1", "{CALL sp_sales_by_category(?,?)}", start_date, end_date)) {
        res.set_body(http::status::ok, *json_result);
    } else {
        res.set_body(http::status::ok, "[]");
    }
}

/**
 * @brief API handler for uploading a single file with a title.
 *
 * Expects a multipart/form-data request with a file part named 'file1'
 * and a text field named 'title'. Saves the file with a unique UUID name.
 */
void upload_file(const http::request& req, http::response& res) {
    // 1. Get the configured blob storage path from the environment.
    const auto blob_path_str = env::get<std::string>("BLOB_PATH", "");
    if (blob_path_str.empty()) {
        util::log::error("BLOB_PATH environment variable is not set.");
        res.set_body(http::status::internal_server_error, R"({"error":"File upload is not configured on the server."})");
        return;
    }
    const std::filesystem::path blob_path(blob_path_str);

    // 2. Retrieve the file part from the request.
    const http::multipart_item* file_part = req.get_file_upload("file1");
    if (!file_part) {
        res.set_body(http::status::bad_request, R"({"error":"Missing 'file1' part in multipart form data."})");
        return;
    }
    
    // 3. Retrieve the validated title. The validator guarantees it exists.
    const auto title = **req.get_value<std::string>("title");

    // 4. Construct the full destination path and save the file.
    try {
        std::filesystem::create_directories(blob_path);

        // Generate a unique filename using a UUID, but keep the original extension.
        const std::filesystem::path original_filename(std::string(file_part->filename));
        const std::string new_filename = util::get_uuid() + original_filename.extension().string();
        const std::filesystem::path dest_path = blob_path / new_filename;

        util::log::info("Saving uploaded file '{}' as '{}' with title '{}'", file_part->filename, dest_path.string(), title);

        std::ofstream out_file(dest_path, std::ios::binary);
        if (!out_file) {
            throw std::runtime_error(std::format("Could not open destination file for writing: {}", util::str_error_cpp(errno)));
        }
        
        out_file.write(file_part->content.data(), file_part->content.size());
        if (!out_file) {
            throw std::runtime_error(std::format("An error occurred while writing to the destination file: {}", util::str_error_cpp(errno)));
        }
        
        out_file.close();

        // Call the stored procedure with the file's metadata.
        sql::exec(
            "DB1",
            "{call sp_blob_add(?, ?, ?, ?, ?)}",
            title,
            new_filename,
            file_part->filename,
            file_part->content_type,
            file_part->content.size()
        );

        // 5. Send a success response.
        std::string success_body = json::json_parser::build({
            {"status", "ok"},
            {"title", title},
            {"originalFilename", std::string(file_part->filename)},
            {"savedFilename", new_filename},
            {"size", std::to_string(file_part->content.size())}
        });
        res.set_body(http::status::ok, success_body);

    } catch (const std::exception& e) {
        util::log::error("File upload failed: {}", e.what());
        res.set_body(http::status::internal_server_error, R"({"error":"Failed to save uploaded file."})");
    }
}


/**
 * @brief The main entry point for the HTTP server application.
 */
int main() {
    try {
        util::log::info("Application starting...");

        server s;
        
        s.register_api(webapi_path{"/hello"}, http::method::get, &hello_world, false);
        s.register_api(webapi_path{"/shippers"}, http::method::get, &get_shippers, true);
        s.register_api(webapi_path{"/products"}, http::method::get, &get_products, true);
        s.register_api(webapi_path{"/customer"}, http::method::get, customer_validator, &get_customer, true);
        s.register_api(webapi_path{"/login"}, http::method::post, login_validator, &login, false);
        s.register_api(webapi_path{"/sales"}, http::method::post, sales_validator, &get_sales_by_category, true);

        // Register the file upload API with its new validator.
        s.register_api(
            webapi_path{"/upload"},
            http::method::post,
            upload_validator,
            &upload_file,
            true // File uploads should be secure.
        );

        s.start();

        util::log::info("Application shutting down gracefully.");

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

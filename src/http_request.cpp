#include "http_request.hpp"
#include "jwt.hpp"
#include <utility>
#include <format>
#include <locale> 
#include <memory> 
#include <algorithm> // for search

using namespace std::literals::string_view_literals;

namespace {

// Per RFC 7230 (and 9112), a 'token' is 1*tchar
// tchar = "!" / "#" / "$" / "%" / "&" / "'" / "*"
//       / "+" / "-" / "." / "^" / "_" / "`" / "|" / "~"
//       / DIGIT / ALPHA
inline bool is_valid_header_key(std::string_view key) {
    if (key.empty()) {
        return false;
    }
    constexpr std::string_view valid_tchars =
        "!#$%&'*+-.^_`|~"
        "0123456789"
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    
    // Scoped initializer for SonarCloud compliance
    if (auto pos = key.find_first_not_of(valid_tchars); pos != std::string_view::npos) {
        return false;
    }
    return true;
}

// Per RFC 7230, field-value can be complex, but for security,
// we *must* prohibit bare CR and LF to prevent response splitting.
inline bool is_valid_header_value(std::string_view value) {
    if (auto pos = value.find_first_of("\r\n"sv); pos != std::string_view::npos) {
        return false;
    }
    return true;
}

// --- NEW HELPERS for Path Validation ---
constexpr size_t MAX_PATH_LENGTH = 2048;

// Validates against path traversal and other bad characters.
// We are explicitly disallowing URL-encoded characters ('%') in the path
// to block a class of obfuscation attacks.
inline bool is_valid_path(std::string_view path) {
    if (path.empty()) {
        return false;
    } 
    
    if (path[0] != '/') {
        return false; // Must be an absolute path starting with '/'
    }

    // Check for invalid characters using init-statement
    if (constexpr std::string_view invalid_chars = "%\0\r\n\\"sv; path.find_first_of(invalid_chars) != std::string_view::npos) {
        return false;
    }

    // Check for path traversal ".." using C++23 contains
    if (path.contains(".."sv)) {
        return false;
    }

    return true;
}

// Helper to trim whitespace from string_view
inline std::string_view trim_sv(std::string_view sv) {
    sv.remove_prefix(std::min(sv.find_first_not_of(" \t"sv), sv.size()));
    if (const auto last = sv.find_last_not_of(" \t"sv); last != std::string_view::npos) {
        sv = sv.substr(0, last + 1);
    }
    return sv;
}

// Helper to sanitize filenames (keep only basename) to prevent directory traversal
inline std::string_view sanitize_filename(std::string_view filename) {
    if (auto last_sep = filename.find_last_of("/\\"); last_sep != std::string_view::npos) {
        filename.remove_prefix(last_sep + 1);
    }
    return filename;
}

} // namespace

namespace http {

// ===================================================================
//         request_parser: Implementation
// ===================================================================
request_parser::request_parser() = default;

request_parser::~request_parser() noexcept = default;

request_parser::request_parser(request_parser&&) noexcept = default;
request_parser& request_parser::operator=(request_parser&&) noexcept = default;

// Helper function to process individual parameters
// IMPLEMENTED as a class member to access private 'multipart_part_headers'
void request_parser::process_parameter(std::string_view param, request_parser::multipart_part_headers& headers) {
    param = trim_sv(param);
    if (param.empty()) {
        return;
    }

    // Using init-statement to limit scope of 'eq_pos'
    if (auto eq_pos = param.find('='); eq_pos != std::string_view::npos) {
        std::string_view p_key = param.substr(0, eq_pos);
        std::string_view p_val = param.substr(eq_pos + 1);

        // Strip surrounding quotes
        if (p_val.size() >= 2 && p_val.front() == '"') {
            p_val.remove_prefix(1);
            if (p_val.back() == '"') {
                p_val.remove_suffix(1);
            }
        }

        if (p_key == "name"sv) {
            headers.field_name = p_val;
        } else if (p_key == "filename"sv) {
            headers.filename = sanitize_filename(p_val);
        }
    }
}

// REPLACED: Robust parser that respects quotes and sanitizes filenames
auto request_parser::parse_part_headers(std::string_view part_headers_sv) const -> multipart_part_headers {
    multipart_part_headers headers;

    // Separate lambda for extraction logic to keep function clean
    auto extract_params_from_content = [&](std::string_view content) {
        size_t start = 0;
        bool in_quotes = false;
        
        for (size_t i = 0; i <= content.size(); ++i) {
            const bool is_end = (i == content.size());
            const bool is_semicolon = !is_end && content[i] == ';';
            
            // Toggle quotes state
            if (!is_end && content[i] == '"') {
                in_quotes = !in_quotes;
                continue;
            }

            // Split token only if we are at the end or hit a semicolon outside quotes
            if (is_end || (is_semicolon && !in_quotes)) {
                // Extract and process the parameter using the helper member function
                process_parameter(content.substr(start, i - start), headers);
                start = i + 1; 
            }
        }
    };

    for (const auto line_range : part_headers_sv | std::views::split("\r\n"sv)) {
        std::string_view line(line_range.begin(), line_range.end());
        if (line.empty()) {
            continue;
        }

        if (auto colon_pos = line.find(':'); colon_pos != std::string_view::npos) {
            std::string_view key = line.substr(0, colon_pos);
            std::string_view value = line.substr(colon_pos + 1);

            if (sv_ci_equal{}(key, "Content-Disposition")) {
                extract_params_from_content(value);
            } else if (sv_ci_equal{}(key, "Content-Type")) {
                headers.content_type = trim_sv(value);
            }
        }
    }
    return headers;
}

// REPLACED: Safe boundary handling
void request_parser::process_multipart_part(std::string_view part_sv) {
    if (part_sv.starts_with("\r\n"sv)) {
        part_sv.remove_prefix(2);
    }
    if (part_sv.ends_with("\r\n"sv)) { 
         part_sv.remove_suffix(2); 
    }

    // FIX: Handle LFLF (double newline) attack for header termination
    size_t headers_end_pos = part_sv.find("\r\n\r\n"sv);
    size_t delimiter_len = 4;

    if (headers_end_pos == std::string_view::npos) {
        headers_end_pos = part_sv.find("\n\n"sv);
        delimiter_len = 2;
    }

    if (headers_end_pos == std::string_view::npos) {
        return; 
    }

    const auto part_headers_sv = part_sv.substr(0, headers_end_pos);
    const auto part_content_sv = part_sv.substr(headers_end_pos + delimiter_len);
    
    const multipart_part_headers headers = parse_part_headers(part_headers_sv);
    
    if (!headers.field_name) {
        return;
    }

    if (headers.filename) {
        m_fileParts.emplace_back(*headers.filename, part_content_sv, headers.content_type.value_or(""), *headers.field_name);
    } else {
        m_params.try_emplace(*headers.field_name, part_content_sv);
    }
}

auto request_parser::get_buffer() noexcept -> std::span<char> {
    return m_buffer->buffer();
}

void request_parser::update_pos(ssize_t bytes_read) {
    if (m_isFinalized) {
        return;
    }
    m_buffer->update_pos(bytes_read);
}

auto request_parser::eof() -> bool {
    using enum http::method;

    if (!find_and_store_header_end()) {
        return false;
    }
    if (!parse_and_store_method()) {
        return false;
    }
    
    if (m_identifiedMethod == get || m_identifiedMethod == options) {
        return true;
    }
    
    if (m_identifiedMethod == post) {
        if (!parse_and_store_content_length()) {
            return false;
        }
        return m_buffer->size() >= (*m_identifiedHeaderSize + *m_identifiedContentLength);
    }

    return false;
}

auto request_parser::finalize() -> std::expected<void, request_parse_error> {
    using enum http::method;

    if (m_isFinalized) {
        return {};
    }

    if (!eof()) {
        return std::unexpected(request_parse_error("Attempted to finalize before request reached eof()."));
    }

    const auto request_sv = m_buffer->view();
    
    if (const auto first_line_end_pos = request_sv.find("\r\n"sv); first_line_end_pos == std::string_view::npos) {
        return std::unexpected(request_parse_error("Malformed request: request line not found."));
    } else {
        if (auto err = parse_request_line(request_sv.substr(0, first_line_end_pos))) {
            return std::unexpected(*err);
        }
        
        m_parsedMethod = m_identifiedMethod.value_or(unknown);

        const auto headers_end_pos_marker = *m_identifiedHeaderSize - 4;
        const auto headers_sv = request_sv.substr(first_line_end_pos + 2, headers_end_pos_marker - (first_line_end_pos + 2));
        if (auto err = parse_headers(headers_sv)) {
            return std::unexpected(*err);
        }
    }
    
    m_headerSize = *m_identifiedHeaderSize;

    if (m_parsedMethod == post) {
        if (!m_identifiedContentLength.has_value()) {
             return std::unexpected(request_parse_error("POST request without Content-Length header."));
        }
        m_contentLength = *m_identifiedContentLength;

        if (m_contentLength > 0) {
            auto it = m_headers.find("content-type");
            if (it == m_headers.end()) {
                return std::unexpected(request_parse_error("POST request with body is missing Content-Type header."));
            }

            const auto& content_type = it->second;
            if (!content_type.starts_with("application/json") && !content_type.starts_with("multipart/form-data")) {
                return std::unexpected(request_parse_error(
                    std::format("Unsupported Content-Type for POST: {}", content_type)
                ));
            }
        }
        
        if (auto err = parse_body()) {
            return std::unexpected(*err);
        }
    }

    m_isFinalized = true;
    return {};
}

auto request_parser::find_and_store_header_end() -> bool {
    if (m_identifiedHeaderSize.has_value()) {
        return true;
    }
    const auto current_buffer_view = m_buffer->view();
    
    if (const auto headers_end_pos = current_buffer_view.find("\r\n\r\n"sv); headers_end_pos != std::string_view::npos) {
        m_identifiedHeaderSize = headers_end_pos + 4;
        return true;
    }
    
    return false;
}

auto request_parser::parse_and_store_method() -> bool {
    using enum http::method;

    if (m_identifiedMethod.has_value()) {
        return true;
    }
    if (!m_identifiedHeaderSize.has_value()) {
        return false;
    }

    const auto current_buffer_view = m_buffer->view();
    if (current_buffer_view.empty() || current_buffer_view.size() < *m_identifiedHeaderSize) {
        return false;
    }

    if (const auto request_line_end = current_buffer_view.find("\r\n"sv); request_line_end == std::string_view::npos || request_line_end == 0 || request_line_end >= (*m_identifiedHeaderSize - 4)) {
        return false;
    } else {
        const std::string_view request_line_sv = current_buffer_view.substr(0, request_line_end);
        
        if (const auto method_space_pos = request_line_sv.find(' '); method_space_pos == std::string_view::npos) {
            m_identifiedMethod = unknown;
            return false;
        } else {
            if (const std::string_view method_sv = request_line_sv.substr(0, method_space_pos); method_sv == "GET"sv) {
                m_identifiedMethod = get;
            } else if (method_sv == "POST"sv) {
                m_identifiedMethod = post;
            } else if (method_sv == "OPTIONS"sv) {
                m_identifiedMethod = options;
            } else {
                m_identifiedMethod = unknown;
            }
        }
    }
    
    return m_identifiedMethod != unknown;
}

auto request_parser::parse_and_store_content_length() -> bool {
    if (m_identifiedMethod != method::post) {
        return true;
    }
    if (m_identifiedContentLength.has_value()) {
        return true;
    }
    if (!m_identifiedHeaderSize.has_value()) {
        return false;
    }

    const auto current_buffer_view = m_buffer->view();
    
    // Check request_line_end
    const auto request_line_end = current_buffer_view.find("\r\n"sv);
    if (request_line_end == std::string_view::npos) {
        return false;
    }

    const size_t headers_part_start = request_line_end + 2;
    const size_t headers_part_length = (*m_identifiedHeaderSize - 4) - headers_part_start;

    if (headers_part_start >= *m_identifiedHeaderSize || headers_part_start + headers_part_length > current_buffer_view.size()) {
        return false;
    }

    std::string_view headers_part = current_buffer_view.substr(headers_part_start, headers_part_length);

    for (const auto line_range : headers_part | std::views::split("\r\n"sv)) {
        std::string_view header_line(line_range.begin(), line_range.end());
        
        auto colon_pos = header_line.find(':');
        if (colon_pos == std::string_view::npos || !sv_ci_equal{}(header_line.substr(0, colon_pos), "Content-Length"sv)) {
            continue;
        }

        std::string_view cl_value_sv = header_line.substr(colon_pos + 1);
        cl_value_sv.remove_prefix(std::min(cl_value_sv.find_first_not_of(" \t"sv), cl_value_sv.size()));
        
        size_t temp_cl = 0;
        auto [ptr, ec] = std::from_chars(cl_value_sv.data(), cl_value_sv.data() + cl_value_sv.size(), temp_cl);
        
        if (ec == std::errc() && ptr == cl_value_sv.data() + cl_value_sv.size()) {
            m_identifiedContentLength = temp_cl;
            return true;
        }
        return false; // Malformed Content-Length value
    }
    return false; 
}

auto request_parser::parse_request_line(std::string_view request_line) -> std::optional<request_parse_error> {
    auto parts = request_line | std::views::split(' ') | std::views::common;
    auto it = parts.begin();
    if (it == parts.end()) {
        return request_parse_error("Malformed request line: empty.");
    }
    ++it;
    if (it == parts.end()) {
        return request_parse_error("Malformed request line: missing URI.");
    }
    
    const std::string_view uri_sv(std::to_address((*it).begin()), std::ranges::distance(*it));
    
    if (auto err = parse_uri(uri_sv); err.has_value()) {
        return err;
    }
    return std::nullopt;
}

// Security: Strict URI validation using C++23 contains and zero-query policy
auto request_parser::parse_uri(std::string_view uri) -> std::optional<request_parse_error> {
    // 1. Strict Requirement: Fail if any query parameters are present.
    if (uri.contains('?')) {
        return request_parse_error(std::format("URI query parameters are not allowed. URI: '{}'", uri));
    }

    // 2. Check for max length
    if (uri.length() > MAX_PATH_LENGTH) {
        return request_parse_error(std::format("URI exceeds maximum length of {}. URI: '{}'", MAX_PATH_LENGTH, uri));
    }

    // 3. Set path (entire URI is path since no '?')
    m_path = uri;

    // 4. Validate the path characters
    if (!is_valid_path(m_path)) {
        return request_parse_error(std::format("Invalid URI path: contains forbidden characters or traversal sequences. URI: '{}'", uri));
    }

    return std::nullopt;
}

auto request_parser::parse_headers(std::string_view headers_sv) -> std::optional<request_parse_error> {
    for (const auto line_range : headers_sv | std::views::split("\r\n"sv)) {
        std::string_view header_line(line_range.begin(), line_range.end());
        if (header_line.empty()) {
            continue;
        }

        if (auto pos = header_line.find(':'); pos != std::string_view::npos) {
            auto key = header_line.substr(0, pos);
            
            if (!is_valid_header_key(key)) {
                return request_parse_error(std::format("Invalid header key: {}", key));
            }
            
            auto value = header_line.substr(pos + 1);
            value.remove_prefix(std::min(value.find_first_not_of(" \t"sv), value.size()));

            if (!is_valid_header_value(value)) {
                return request_parse_error(std::format("Invalid characters in header value for key: {}", key));
            }

            // Security: Reject Transfer-Encoding (HSR protection)
            if (sv_ci_equal{}(key, "Transfer-Encoding")) {
                return request_parse_error("Transfer-Encoding is not supported.");
            }
            
            // Security: Reject duplicate Host headers
            if (sv_ci_equal{}(key, "Host") && m_headers.contains("Host")) {
                return request_parse_error("Duplicate Host header detected.");
            }
            
            m_headers.try_emplace(std::string(key), value);
        } else {
            return request_parse_error(std::format("Malformed header line: {}", header_line));
        }
    }
    return std::nullopt;
}

auto request_parser::parse_body() -> std::optional<request_parse_error> {
    const auto body_view = m_buffer->view().substr(m_headerSize, m_contentLength);
    auto it = m_headers.find("content-type");

    if (it == m_headers.end()) {
        m_body = body_view;
        return std::nullopt;
    }
    
    const auto& content_type = it->second;
    if (content_type.starts_with("application/json"sv)) {
        try {
            m_jsonPayload = std::make_unique<json::json_parser>(body_view);
            m_body = body_view;
        } catch (const json::parsing_error& e) {
            return request_parse_error(std::string("JSON parse error: ") + e.what());
        }
        return std::nullopt;
    }
    
    if (content_type.starts_with("multipart/form-data"sv)) {
        const std::string_view boundary_prefix = "boundary="sv;
        if (auto boundary_pos = content_type.find(boundary_prefix); boundary_pos != std::string_view::npos) {
            auto boundary = content_type.substr(boundary_pos + boundary_prefix.length());
            if (boundary.starts_with('"')) {
                boundary.remove_prefix(1);
            }
            if (boundary.ends_with('"')) {
                boundary.remove_suffix(1);
            }
            return parse_multipart_form_data(boundary);
        } else {
            return request_parse_error("Malformed multipart/form-data: boundary not found.");
        }
    }
    
    m_body = body_view;
    return std::nullopt;
}

auto request_parser::parse_multipart_form_data(std::string_view boundary) -> std::optional<request_parse_error> {
    const std::string full_boundary = "--" + std::string(boundary);
    const auto body_view = m_buffer->view().substr(m_headerSize, m_contentLength);
    
    for (const auto part_range : body_view | std::views::split(full_boundary) | std::views::drop(1)) {
        process_multipart_part({std::to_address(part_range.begin()), static_cast<size_t>(std::ranges::distance(part_range))});
    }
    return std::nullopt;
}


// ===================================================================
//         request: Implementation
// ===================================================================

request::request(request_parser&& parser, std::string_view remote_ip)
    : m_buffer(std::move(parser.m_buffer)),
      m_jsonPayload(std::move(parser.m_jsonPayload)),
      m_method(parser.m_parsedMethod),
      m_headers(std::move(parser.m_headers)),
      m_params(std::move(parser.m_params)),
      m_body(std::move(parser.m_body)),
      m_fileParts(std::move(parser.m_fileParts)),
      m_path(parser.m_path),
      m_remote_ip(remote_ip)
{}

request::~request() noexcept = default;
request::request(request&&) noexcept = default;
request& request::operator=(request&&) noexcept = default;

auto request::get_method() const noexcept -> method { return m_method; }

auto request::get_method_str() const noexcept -> std::string_view {
    using enum http::method;
    switch (m_method) {
        case get:     return "GET"sv;
        case post:    return "POST"sv;
        case options: return "OPTIONS"sv;
        default:      return "UNKNOWN"sv;
    }
}

auto request::get_remote_ip() const noexcept -> std::string_view {
    return m_remote_ip;
}

auto request::get_header_value(std::string_view key) const noexcept -> std::optional<std::string_view> {
    if (auto it = m_headers.find(key); it != m_headers.end()) {
        return it->second;
    }
    return std::nullopt;
}

auto request::get_headers() const noexcept -> const header_map& { return m_headers; }
auto request::get_params() const noexcept -> const param_map& { return m_params; }
auto request::get_body() const noexcept -> const request_body& { return m_body; }
auto request::get_path() const noexcept -> std::string_view { return m_path; }
auto request::get_file_parts() const noexcept -> const std::vector<multipart_item>& { return m_fileParts; }

auto request::get_bearer_token() const noexcept -> std::optional<std::string_view> {
    if (auto it = m_headers.find("Authorization"); it != m_headers.end() && it->second.starts_with("Bearer "sv)) {
        return it->second.substr(7);
    }    
    return std::nullopt;
}

auto request::get_file_upload(std::string_view field_name) const noexcept -> const multipart_item* {
    auto it = std::ranges::find_if(m_fileParts, [&](const auto& item){
        return item.field_name == field_name;
    });
    return (it != m_fileParts.end()) ? std::to_address(it) : nullptr;
}

// Helper for parsing date/time from a string_view
template<typename T>
concept is_chrono_type = std::is_same_v<T, std::chrono::system_clock::time_point> ||
                         std::is_same_v<T, std::chrono::year_month_day>;

template <is_chrono_type T>
auto parse_chrono_type(std::string_view sv) -> std::optional<T> {
    T value{};
    std::istringstream iss{std::string(sv)};
    iss.imbue(std::locale::classic());

    if constexpr (std::is_same_v<T, std::chrono::system_clock::time_point>) {
        // Try ISO 8601 format first
        std::chrono::from_stream(iss, "%Y-%m-%dT%H:%M:%S", value);
        if (!iss.fail()) {
            return value;
        }
        iss.clear();
        iss.seekg(0);
        std::chrono::from_stream(iss, "%Y-%m-%d %H:%M:%S", value);
        if (!iss.fail()) {
            return value;
        }
    } else if constexpr (std::is_same_v<T, std::chrono::year_month_day>) {
        std::chrono::from_stream(iss, "%Y-%m-%d", value);
        if (!iss.fail()) {
            iss >> std::ws;
            if (iss.eof()) {
                return value;
            }
        }
    }
    return std::nullopt;
}


template <typename t>
auto request::get_value(std::string_view param_name) const noexcept -> std::expected<std::optional<t>, param_error> {
    std::optional<std::string_view> value_sv_opt;
    if (auto it = m_params.find(param_name); it != m_params.end()) {
        value_sv_opt = it->second;
    } else if (m_jsonPayload && m_jsonPayload->has_key(param_name)) {
        value_sv_opt = m_jsonPayload->get_string(param_name);
    }

    if (!value_sv_opt) {
        return std::optional<t>{};
    }
    
    const auto& value_sv = *value_sv_opt;
    auto make_error = [&]() { return std::unexpected{param_error{std::string(param_name), std::string(value_sv)}}; };

    if constexpr (std::is_same_v<t, std::string>) {
        return std::optional{std::string(value_sv)};
    } else if constexpr (std::is_same_v<t, std::string_view>) {
        return std::optional{value_sv};
    } else if constexpr (is_chrono_type<t>) {
        if (auto parsed_value = parse_chrono_type<t>(value_sv)) {
            return std::optional{parsed_value};
        } else {
            return make_error();
        }
    } else { 
        t value{};
        auto result = std::from_chars(value_sv.data(), value_sv.data() + value_sv.size(), value);
        if (result.ec == std::errc() && result.ptr == value_sv.data() + value_sv.size()) {
            return std::optional{value};
        } else {
            return make_error();
        }
    }
}

auto request::get_user() const noexcept -> std::string {
    if (auto claims = jwt::get_claims(get_bearer_token().value_or("")); claims.has_value()) {
        if (auto it = claims->find("user"); it != claims->end()) {
            return it->second;
        }
    }
    return "not available";
}

// --- Explicit template instantiations ---
template auto request::get_value<std::string>(std::string_view) const noexcept -> std::expected<std::optional<std::string>, param_error>;
template auto request::get_value<std::string_view>(std::string_view) const noexcept -> std::expected<std::optional<std::string_view>, param_error>;
template auto request::get_value<int>(std::string_view) const noexcept -> std::expected<std::optional<int>, param_error>;
template auto request::get_value<long>(std::string_view) const noexcept -> std::expected<std::optional<long>, param_error>;
template auto request::get_value<double>(std::string_view) const noexcept -> std::expected<std::optional<double>, param_error>;
template auto request::get_value<std::chrono::system_clock::time_point>(std::string_view) const noexcept -> std::expected<std::optional<std::chrono::system_clock::time_point>, param_error>;
template auto request::get_value<std::chrono::year_month_day>(std::string_view) const noexcept -> std::expected<std::optional<std::chrono::year_month_day>, param_error>;

} // namespace http
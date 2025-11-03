#include "http_request.hpp"
#include "jwt.hpp"
#include <utility>
#include <format>
#include <locale> 
#include <memory> 

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
    return key.find_first_not_of(valid_tchars) == std::string_view::npos;
}

// Per RFC 7230, field-value can be complex, but for security,
// we *must* prohibit bare CR and LF to prevent response splitting.
inline bool is_valid_header_value(std::string_view value) {
    // Finding either \r or \n is sufficient to reject.
    return value.find_first_of("\r\n"sv) == std::string_view::npos;
}

} // namespace

namespace http {

// ===================================================================
//         request_parser: Implementation
// ===================================================================
request_parser::request_parser() = default;

// FIX: Use = default for the destructor
request_parser::~request_parser() noexcept = default;

request_parser::request_parser(request_parser&&) noexcept = default;
request_parser& request_parser::operator=(request_parser&&) noexcept = default;


// Helper function to parse headers from a multipart form part.
auto request_parser::parse_part_headers(std::string_view part_headers_sv) const -> request_parser::multipart_part_headers {
    multipart_part_headers headers;

    for (const auto line_range : part_headers_sv | std::views::split("\r\n"sv)) {
        std::string_view line(line_range.begin(), line_range.end());
        if (line.empty()) {
            continue;
        }
        
        auto parse_param = [&](std::string_view param_name) -> std::optional<std::string_view> {
            auto param_pos = line.find(param_name);
            if (param_pos == std::string_view::npos) {
                return std::nullopt;
            }
            auto value_start = line.find('"', param_pos);
            if (value_start == std::string_view::npos) {
                return std::nullopt;
            }
            value_start++; // Move past the quote
            auto value_end = line.find('"', value_start);
            if (value_end == std::string_view::npos) {
                return std::nullopt;
            }
            return line.substr(value_start, value_end - value_start);
        };

        if (line.starts_with("Content-Disposition:"sv)) {
            headers.field_name = parse_param("name="sv);
            headers.filename = parse_param("filename="sv);
        } else if (line.starts_with("Content-Type:"sv)) {
            auto value = line.substr(line.find(':') + 1);
            value.remove_prefix(std::min(value.find_first_not_of(" \t"sv), value.size()));
            headers.content_type = value;
        }
    }
    return headers;
}

// Helper function to process a single part of a multipart form.
void request_parser::process_multipart_part(std::string_view part_sv) {
    if (part_sv.starts_with("--"sv)) return; // End boundary
    if (part_sv.starts_with("\r\n"sv)) part_sv.remove_prefix(2);
    if (part_sv.ends_with("\r\n"sv)) part_sv.remove_suffix(2);
    
    const auto headers_end_pos = part_sv.find("\r\n\r\n"sv);
    if (headers_end_pos == std::string_view::npos) {
        return;
    }

    const auto part_headers_sv = part_sv.substr(0, headers_end_pos);
    const auto part_content_sv = part_sv.substr(headers_end_pos + 4);
    
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

    // REFACTOR: Use guard clauses to simplify logic.
    if (!find_and_store_header_end()) return false;
    if (!parse_and_store_method()) return false;
    
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
    const auto first_line_end_pos = request_sv.find("\r\n"sv);
    if (first_line_end_pos == std::string_view::npos) {
        return std::unexpected(request_parse_error("Malformed request: request line not found."));
    }

    if (auto err = parse_request_line(request_sv.substr(0, first_line_end_pos))) {
        return std::unexpected(*err);
    }
    m_parsedMethod = m_identifiedMethod.value_or(unknown);

    const auto headers_end_pos_marker = *m_identifiedHeaderSize - 4;
    const auto headers_sv = request_sv.substr(first_line_end_pos + 2, headers_end_pos_marker - (first_line_end_pos + 2));
    if (auto err = parse_headers(headers_sv)) {
        return std::unexpected(*err);
    }
    m_headerSize = *m_identifiedHeaderSize;

    // REFACTOR: Flattened logic for handling POST requests.
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
    const auto headers_end_pos = current_buffer_view.find("\r\n\r\n"sv);
    if (headers_end_pos == std::string_view::npos) {
        return false;
    }
    m_identifiedHeaderSize = headers_end_pos + 4;
    return true;
}

auto request_parser::parse_and_store_method() -> bool {
    using enum http::method;

    if (m_identifiedMethod.has_value()) return true;
    if (!m_identifiedHeaderSize.has_value()) return false;

    const auto current_buffer_view = m_buffer->view();
    if (current_buffer_view.empty() || current_buffer_view.size() < *m_identifiedHeaderSize) return false;

    const auto request_line_end = current_buffer_view.find("\r\n"sv);
    if (request_line_end == std::string_view::npos || request_line_end == 0 || request_line_end >= (*m_identifiedHeaderSize - 4)) {
        return false;
    }

    const std::string_view request_line_sv = current_buffer_view.substr(0, request_line_end);
    const auto method_space_pos = request_line_sv.find(' ');
    if (method_space_pos == std::string_view::npos) {
        m_identifiedMethod = unknown;
        return false;
    }
    
    if (const std::string_view method_sv = request_line_sv.substr(0, method_space_pos); method_sv == "GET"sv) m_identifiedMethod = get;
    else if (method_sv == "POST"sv)    m_identifiedMethod = post;
    else if (method_sv == "OPTIONS"sv) m_identifiedMethod = options;
    else m_identifiedMethod = unknown;
    return m_identifiedMethod != unknown;
}

auto request_parser::parse_and_store_content_length() -> bool {
    if (m_identifiedMethod != method::post) return true;
    if (m_identifiedContentLength.has_value()) return true;
    if (!m_identifiedHeaderSize.has_value()) return false;

    const auto current_buffer_view = m_buffer->view();
    const auto request_line_end = current_buffer_view.find("\r\n"sv);
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
    return false; // Content-Length header not found
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
    parse_uri(uri_sv);
    return std::nullopt;
}

void request_parser::parse_uri(std::string_view uri) {
    auto query_pos = uri.find('?');
    m_path = uri.substr(0, query_pos);
    if (query_pos == std::string_view::npos) {
        return;
    }

    std::string_view query_string = uri.substr(query_pos + 1);
    for (const auto param_range : query_string | std::views::split('&')) {
        std::string_view param_sv(param_range.begin(), param_range.end());
        auto eq_pos = param_sv.find('=');
        m_params.try_emplace(param_sv.substr(0, eq_pos), eq_pos == std::string_view::npos ? ""sv : param_sv.substr(eq_pos + 1));
    }
}

auto request_parser::parse_headers(std::string_view headers_sv) -> std::optional<request_parse_error> {
    for (const auto line_range : headers_sv | std::views::split("\r\n"sv)) {
        std::string_view header_line(line_range.begin(), line_range.end());
        if (header_line.empty()) {
            continue; // Ignore empty lines
        }

        auto pos = header_line.find(':');
        if (pos == std::string_view::npos) {
            // A non-empty line with no colon is a malformed header.
            return request_parse_error(std::format("Malformed header line: {}", header_line));
        }

        auto key = header_line.substr(0, pos);
        
        // --- FIX 1: Validate header key characters ---
        if (!is_valid_header_key(key)) {
            return request_parse_error(std::format("Invalid header key: {}", key));
        }
        
        auto value = header_line.substr(pos + 1);
        value.remove_prefix(std::min(value.find_first_not_of(" \t"sv), value.size()));

        // --- FIX 2 (CRLF Injection): Validate header value ---
        if (!is_valid_header_value(value)) {
            return request_parse_error(std::format("Invalid characters in header value for key: {}", key));
        }

        // --- FIX 3 (HSR): Reject Transfer-Encoding ---
        // This must be case-insensitive, so we use sv_ci_equal.
        if (sv_ci_equal{}(key, "Transfer-Encoding")) {
            return request_parse_error("Transfer-Encoding is not supported.");
        }
        
        // --- FIX 4 (Host Ambiguity): Reject duplicate Host headers ---
        // This must be case-insensitive.
        if (sv_ci_equal{}(key, "Host")) {
            if (m_headers.contains("Host")) { // Check if 'Host' was already emplaced.
                return request_parse_error("Duplicate Host header detected.");
            }
        }
        
        m_headers.try_emplace(std::string(key), value);
    }
    return std::nullopt;
}

auto request_parser::parse_body() -> std::optional<request_parse_error> {
    const auto body_view = m_buffer->view().substr(m_headerSize, m_contentLength);
    auto it = m_headers.find("content-type");

    // REFACTOR: Flattened logic using guard clauses.
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
        auto boundary_pos = content_type.find(boundary_prefix);
        if (boundary_pos == std::string_view::npos) {
            return request_parse_error("Malformed multipart/form-data: boundary not found.");
        }
        auto boundary = content_type.substr(boundary_pos + boundary_prefix.length());
        if (boundary.starts_with('"')) boundary.remove_prefix(1);
        if (boundary.ends_with('"')) boundary.remove_suffix(1);
        return parse_multipart_form_data(boundary);
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
        if (!iss.fail()) return value;
        // Reset and try another common format
        iss.clear();
        iss.seekg(0);
        std::chrono::from_stream(iss, "%Y-%m-%d %H:%M:%S", value);
        if (!iss.fail()) return value;
    } else if constexpr (std::is_same_v<T, std::chrono::year_month_day>) {
        std::chrono::from_stream(iss, "%Y-%m-%d", value);
        if (!iss.fail()) {
            iss >> std::ws;
            if (iss.eof()) return value;
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
    } else { // For numeric types
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
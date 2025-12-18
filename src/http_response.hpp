#ifndef HTTP_RESPONSE_HPP
#define HTTP_RESPONSE_HPP

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <span>
#include <format>
#include <chrono>
#include <cstddef>
#include <stdexcept>

namespace http {

enum class status {
    ok = 200,
    no_content = 204,
    bad_request = 400,
    unauthorized = 401,
    forbidden = 403,
    not_found = 404,
    entity_too_large = 413,
    internal_server_error = 500
};

[[nodiscard]] constexpr std::string_view to_reason_phrase(status s) {
    using enum status;
    switch (s) {
        case ok: return "OK";
        case no_content: return "No Content";
        case bad_request: return "Bad Request";
        case unauthorized: return "Unauthorized";
        case forbidden: return "Forbidden";
        case not_found: return "Not Found";
        case entity_too_large: return "Entity Too Large";
        case internal_server_error: return "Internal Server Error";
    }
    return "Unknown Status";
}

// NOTE: The response_exception has been removed as it's an anti-pattern
// to use exceptions for standard control flow like authentication failures.

class response {
public:
    explicit response(std::optional<std::string_view> origin = std::nullopt);
    void set_body(status s, std::string_view body, std::string_view content_type = "application/json; charset=utf-8");
    void set_blob(std::string_view blob_data, std::string_view content_type, std::string_view content_disposition);
    void set_options();
    [[nodiscard]] std::span<const char> buffer() const noexcept;
    [[nodiscard]] size_t available_size() const noexcept;
    void update_pos(size_t bytes_sent) noexcept;
    [[nodiscard]] std::optional<status> status_code() const noexcept;
private:
    std::vector<char> m_buffer;
    size_t m_readPos{0};
    bool m_finalized{false};
    std::optional<std::string> m_origin;
    std::optional<status> m_status;
};

// ... (rest of the implementation is unchanged) ...
inline response::response(std::optional<std::string_view> origin)
{
    if(origin && !origin->empty()) {
        m_origin = *origin;
    }
    m_buffer.reserve(4096);
}

inline void response::set_body(status s, std::string_view body, std::string_view content_type) {
    if (m_finalized) return;
    // store the status for later retrieval
    m_status = s;    
    constexpr std::string_view format_template =
        "HTTP/1.1 {} {}\r\n"
        "Date: {:%a, %d %b %Y %H:%M:%S GMT}\r\n"
        "{}"
        "Strict-Transport-Security: max-age=31536000; includeSubDomains\r\n"
        "X-Frame-Options: SAMEORIGIN\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Referrer-Policy: no-referrer\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: keep-alive\r\n"
        "Content-Type: {}\r\n"
        "Content-Length: {}\r\n"
        "\r\n"
        "{}";
    const auto cors_header = m_origin ? std::format("Access-Control-Allow-Origin: {}\r\n", *m_origin) : "";
    std::format_to(
        std::back_inserter(m_buffer),
        format_template,
        std::to_underlying(s),
        to_reason_phrase(s),
        std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()),
        cors_header,
        content_type,
        body.size(),
        body
    );
    m_finalized = true;
}

inline void response::set_blob(std::string_view blob_data, std::string_view content_type, std::string_view content_disposition) {
    if (m_finalized) return;
    constexpr std::string_view format_template =
        "HTTP/1.1 200 OK\r\n"
        "Date: {:%a, %d %b %Y %H:%M:%S GMT}\r\n"
        "{}"
        "Access-Control-Expose-Headers: Content-Disposition\r\n"
        "Strict-Transport-Security: max-age=31536000; includeSubDomains\r\n"
        "X-Frame-Options: SAMEORIGIN\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Referrer-Policy: no-referrer\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: keep-alive\r\n"
        "Content-Type: {}\r\n"
        "Content-Disposition: {}\r\n"
        "Content-Length: {}\r\n"
        "\r\n";
    const auto cors_header = m_origin ? std::format("Access-Control-Allow-Origin: {}\r\n", *m_origin) : "";
    std::format_to(
        std::back_inserter(m_buffer),
        format_template,
        std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()),
        cors_header,
        content_type,
        content_disposition,
        blob_data.size()
    );
    m_buffer.insert(m_buffer.end(), blob_data.begin(), blob_data.end());
    m_finalized = true;
}

inline void response::set_options() {
    if (m_finalized) return;
    constexpr std::string_view format_template =
        "HTTP/1.1 204 No Content\r\n"
        "Date: {:%a, %d %b %Y %H:%M:%S GMT}\r\n"
        "{}"
        "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization, x-api-key\r\n"
        "Access-Control-Max-Age: 86400\r\n"
        "Connection: keep-alive\r\n"
        "Content-Length: 0\r\n"
        "\r\n";
    const auto cors_header = m_origin ? std::format("Access-Control-Allow-Origin: {}\r\n", *m_origin) : "";
    std::format_to(
        std::back_inserter(m_buffer),
        format_template,
        std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()),
        cors_header
    );
    m_finalized = true;
}

inline std::span<const char> response::buffer() const noexcept {
    if (m_readPos >= m_buffer.size()) {
        return {};
    }
    return {m_buffer.data() + m_readPos, available_size()};
}

inline size_t response::available_size() const noexcept {
    return m_buffer.size() > m_readPos ? m_buffer.size() - m_readPos : 0;
}

inline void response::update_pos(size_t bytes_sent) noexcept {
    m_readPos += bytes_sent;
}

inline std::optional<status> response::status_code() const noexcept {
    return m_status;
}

} // namespace http

#endif // HTTP_RESPONSE_HPP

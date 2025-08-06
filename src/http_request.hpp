#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP

#include "socket_buffer.hpp"
#include "json_parser.hpp"
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
#include <charconv>
#include <optional>
#include <string>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <ranges>
#include <expected>
#include <chrono>
#include <sstream>
#include <span>
#include <memory>
#include <utility>

namespace http {

// --- Type Definitions (must come before classes that use them) ---

struct sv_ci_hash {
    using is_transparent = void;
    [[nodiscard]] auto operator()(std::string_view sv) const noexcept -> size_t {
        size_t hash = 5381;
        for (const auto c : sv) {
            hash = ((hash << 5) + hash) + static_cast<size_t>(std::tolower(static_cast<unsigned char>(c)));
        }
        return hash;
    }
};

struct sv_ci_equal {
    using is_transparent = void;
    [[nodiscard]] auto operator()(std::string_view lhs, std::string_view rhs) const noexcept -> bool {
        return lhs.size() == rhs.size() &&
               std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](char a, char b) {
                   return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
               });
    }
};

using header_map = std::unordered_map<std::string, std::string_view, sv_ci_hash, sv_ci_equal>;
using param_map = std::unordered_map<std::string_view, std::string_view>;

struct multipart_item {
    std::string_view filename;
    std::string_view content;
    std::string_view content_type;
    std::string_view field_name;
};

using request_body = std::variant<std::monostate, std::string_view>;

class request_parse_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct param_error {
    std::string param_name;
    std::string original_value;
};

enum class method {
    get,
    post,
    options,
    unknown
};

// Forward declaration
class request_parser;

class request {
public:
    explicit request(request_parser&& parser, std::string_view remote_ip);
    ~request();
    request(const request&) = delete;
    request& operator=(const request&) = delete;
    request(request&&) noexcept;
    request& operator=(request&&) noexcept;

    [[nodiscard]] auto get_method() const noexcept -> method;
    [[nodiscard]] auto get_method_str() const noexcept -> std::string_view;
    [[nodiscard]] auto get_remote_ip() const noexcept -> std::string_view;
    [[nodiscard]] auto get_headers() const noexcept -> const header_map&;
    [[nodiscard]] auto get_params() const noexcept -> const param_map&;
    [[nodiscard]] auto get_body() const noexcept -> const request_body&;
    [[nodiscard]] auto get_path() const noexcept -> std::string_view;
    [[nodiscard]] auto get_file_parts() const noexcept -> const std::vector<multipart_item>&;
    [[nodiscard]] auto get_bearer_token() const noexcept -> std::optional<std::string_view>;
    [[nodiscard]] auto get_file_upload(std::string_view field_name) const noexcept -> const multipart_item*;
	// New helper to get an arbitrary header value	
    [[nodiscard]] auto get_header_value(std::string_view key) const noexcept -> std::optional<std::string_view>;

    template <typename t>
    [[nodiscard]] auto get_value(std::string_view param_name) const noexcept -> std::expected<std::optional<t>, param_error>;

    [[nodiscard]] auto get_user() const noexcept -> std::string;
    
private:
    std::unique_ptr<socket_buffer> m_buffer;
    std::unique_ptr<json::json_parser> m_jsonPayload;
    method m_method{method::unknown};
    header_map m_headers;
    param_map m_params;
    request_body m_body;
    std::vector<multipart_item> m_fileParts;
    std::string_view m_path;
    std::string m_remote_ip;
};


class request_parser {
public:
    friend class request;
    request_parser();
    ~request_parser() noexcept;
    request_parser(request_parser&&) noexcept;
    request_parser& operator=(request_parser&&) noexcept;
    request_parser(const request_parser&) = delete;
    request_parser& operator=(const request_parser&) = delete;

    [[nodiscard]] auto get_buffer() noexcept -> std::span<char>;
    void update_pos(ssize_t bytes_read);
    [[nodiscard]] auto eof() -> bool;
    [[nodiscard]] auto finalize() -> std::expected<void, request_parse_error>;

private:
    struct multipart_part_headers {
        std::optional<std::string_view> field_name;
        std::optional<std::string_view> filename;
        std::optional<std::string_view> content_type;
    };

    auto find_and_store_header_end() -> bool;
    auto parse_and_store_method() -> bool;
    auto parse_and_store_content_length() -> bool;
    auto parse_headers(std::string_view headers_sv) -> std::optional<request_parse_error>;
    auto parse_request_line(std::string_view request_line) -> std::optional<request_parse_error>;
    void parse_uri(std::string_view uri);
    auto parse_body() -> std::optional<request_parse_error>;
    auto parse_multipart_form_data(std::string_view boundary) -> std::optional<request_parse_error>;
    void process_multipart_part(std::string_view part_sv);
    [[nodiscard]] auto parse_part_headers(std::string_view part_headers_sv) const -> multipart_part_headers;

    std::unique_ptr<socket_buffer> m_buffer{std::make_unique<socket_buffer>()};
    
    std::unique_ptr<json::json_parser> m_jsonPayload;
    method m_parsedMethod{method::unknown};
    std::optional<method> m_identifiedMethod;
    std::optional<size_t> m_identifiedContentLength;
    std::optional<size_t> m_identifiedHeaderSize;
    header_map m_headers;
    param_map m_params;
    request_body m_body;
    std::vector<multipart_item> m_fileParts;
    std::string_view m_path;
    size_t m_contentLength{0};
    size_t m_headerSize{0};
    bool m_isFinalized{false};
};

} // namespace http

#endif // HTTP_REQUEST_HPP

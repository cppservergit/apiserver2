#include "json_parser.hpp"
#include <format>
#include <iostream>
#include <memory>
#include <utility>

namespace json {

// parsing_error implementation
parsing_error::parsing_error(const std::string& msg)
    : std::runtime_error(msg) {}

// output_error implementation
output_error::output_error(const std::string& msg)
    : std::runtime_error(msg) {}

// json_parser implementation
json_parser::json_parser(std::string_view json_str) {
    auto* tok = json_tokener_new();
    if (!tok) {
        throw std::bad_alloc{};
    }

    // FIX: Create a temporary null-terminated std::string for the C-API.
    // This prevents subtle bugs in libraries that might misbehave with
    // non-null-terminated buffers, even when a length is provided.
    const std::string temp_json_for_c_api(json_str);

    m_obj = json_tokener_parse_ex(
        tok, 
        temp_json_for_c_api.c_str(), 
        static_cast<int>(temp_json_for_c_api.size())
    );

    if (json_tokener_get_error(tok) != json_tokener_success || m_obj == nullptr) {
        std::string err = json_tokener_error_desc(json_tokener_get_error(tok));
        json_tokener_free(tok);
        // Log the original string_view for accurate debugging.
        throw parsing_error(std::format("JSON parsing error: {} payload: {}", err, json_str));
    }

    json_tokener_free(tok);
}

json_parser::~json_parser() noexcept {
    if (m_obj) {
        json_object_put(m_obj);
    }
}

json_parser::json_parser(const json_parser& other)
    : m_obj(json_object_get(other.m_obj)) {}

json_parser& json_parser::operator=(const json_parser& other) {
    if (this != &other) {
        json_object_put(m_obj);
        m_obj = json_object_get(other.m_obj);
    }
    return *this;
}

json_parser::json_parser(json_parser&& other) noexcept
    : m_obj(other.m_obj) {
    other.m_obj = nullptr;
}

json_parser& json_parser::operator=(json_parser&& other) noexcept {
    if (this != &other) {
        json_object_put(m_obj);
        m_obj = other.m_obj;
        other.m_obj = nullptr;
    }
    return *this;
}

std::string json_parser::build(const std::map<std::string, std::string>& data) {
    auto* obj = json_object_new_object();
    if (!obj) {
        throw std::bad_alloc{};
    }
    std::unique_ptr<json_object, decltype(&json_object_put)> obj_ptr(obj, &json_object_put);

    for (const auto& [key, value] : data) {
        auto* j_value = json_object_new_string(value.c_str());
        if (!j_value) {
            throw output_error("json build: failed to create json string for value: " + value);
        }
        if (json_object_object_add(obj_ptr.get(), key.c_str(), j_value) != 0) {
            json_object_put(j_value);
            throw output_error("json build: failed to add key to json object: " + key);
        }
    }

    const char* json_str = json_object_to_json_string_ext(obj_ptr.get(), JSON_C_TO_STRING_PLAIN);
    if (!json_str) {
        throw output_error("json build: failed to convert json object to string");
    }

    return std::string{json_str};
}

std::string_view json_parser::get_string(std::string_view key) const {
    auto* tmp = json_object_object_get(m_obj, std::string(key).c_str());
    return tmp ? std::string_view(json_object_get_string(tmp)) : std::string_view{};
}

bool json_parser::has_key(std::string_view key) const noexcept {
    return json_object_object_get_ex(m_obj, std::string(key).c_str(), nullptr);
}

json_parser json_parser::at(std::string_view key) const {
    if (!m_obj || !json_object_is_type(m_obj, json_type_object)) {
        throw parsing_error("json value is not an object");
    }
    auto* child = json_object_object_get(m_obj, std::string(key).c_str());
    if (!child) {
        throw std::out_of_range("json object missing key: " + std::string(key));
    }
    return json_parser{json_object_get(child)};
}

json_parser json_parser::at(size_t index) const {
    if (!m_obj || !json_object_is_type(m_obj, json_type_array)) {
        throw parsing_error("json value is not an array");
    }
    auto* item = json_object_array_get_idx(m_obj, index);
    if (!item) {
        throw std::out_of_range("json array index out of range");
    }
    return json_parser{json_object_get(item)};
}

size_t json_parser::size() const noexcept {
    if (json_object_is_type(m_obj, json_type_array)) {
        return json_object_array_length(m_obj);
    }
    return 0;
}

std::string json_parser::to_string() const {
    if (!m_obj) {
        return "";
    }
    return json_object_to_json_string_ext(m_obj, JSON_C_TO_STRING_PLAIN);
}

std::map<std::string, std::string, std::less<>> json_parser::get_map() const {
    std::map<std::string, std::string, std::less<>> fields;

    if (!m_obj || !json_object_is_type(m_obj, json_type_object)) {
        return fields;
    }

    json_object_object_foreach(m_obj, key, val) {
        if (!val || json_object_is_type(val, json_type_object) || json_object_is_type(val, json_type_array)) {
            continue;
        }
        if (const char* val_ptr = json_object_get_string(val)) {
            fields.try_emplace(key, val_ptr);
        } else {
            fields.try_emplace(key, "");
        }
    }
    return fields;
}

json_parser::json_parser(struct json_object* obj) noexcept : m_obj(obj) {}

} // namespace json

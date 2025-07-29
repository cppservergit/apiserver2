#ifndef JSON_PARSER_HPP
#define JSON_PARSER_HPP

#include <json-c/json.h>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <functional> // For std::less
#include <memory>     // For std::unique_ptr
#include <new>        // For std::bad_alloc

namespace json {

class parsing_error : public std::runtime_error {
public:
    explicit parsing_error(const std::string& msg);
};

class output_error : public std::runtime_error {
public:
    explicit output_error(const std::string& msg);
};

class json_parser {
public:
    explicit json_parser(std::string_view json_str);
    ~json_parser() noexcept;

    json_parser(const json_parser& other);
    json_parser& operator=(const json_parser& other);
    json_parser(json_parser&& other) noexcept;
    json_parser& operator=(json_parser&& other) noexcept;

    /**
     * @brief Builds a JSON object string from any map-like container of strings.
     * @tparam MapType A type that can be iterated over yielding key-value pairs of strings.
     * @param data The map-like container.
     * @return A JSON object as a std::string.
     */
    template<typename MapType>
    [[nodiscard]] static std::string build(const MapType& data) {
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

    [[nodiscard]] std::string_view get_string(std::string_view key) const;
    [[nodiscard]] bool has_key(std::string_view key) const noexcept;
    [[nodiscard]] json_parser at(std::string_view key) const;
    [[nodiscard]] json_parser at(size_t index) const;
    [[nodiscard]] size_t size() const noexcept;
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::map<std::string, std::string, std::less<>> get_map() const;

private:
    explicit json_parser(struct json_object* obj) noexcept;
    struct json_object* m_obj;
};

} // namespace json

#endif // JSON_PARSER_HPP

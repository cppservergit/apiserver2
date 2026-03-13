#ifndef INPUT_VALIDATOR_HPP
#define INPUT_VALIDATOR_HPP

#include "http_request.hpp"
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <functional>
#include <tuple>
#include <utility>
#include <format>
#include <type_traits> // Required for std::is_same_v

namespace validation {

/// @brief Describes the requirement level for a parameter.
enum class requirement {
    required,
    optional
};

/// @brief Exception thrown when an input validation rule is broken.
class validation_error : public std::runtime_error {
public:
    enum class error_type {
        missing_required_param,
        invalid_format,
        custom_rule_failed
    };

    validation_error(std::string param_name, error_type type, std::string details)
        : std::runtime_error(std::format("Validation failed for parameter '{}': {}", param_name, details)),
          m_paramName{std::move(param_name)},
          m_type{type},
          m_details{std::move(details)}
    {}

    [[nodiscard]] const std::string& get_param_name() const noexcept { return m_paramName; }
    [[nodiscard]] error_type get_type() const noexcept { return m_type; }
    [[nodiscard]] const std::string& get_details() const noexcept { return m_details; }

private:
    std::string m_paramName;
    error_type m_type;
    std::string m_details;
};

/// @brief A rule defining validation criteria for a single input parameter.
template<typename T>
struct rule {
    std::string_view name;
    requirement req{requirement::required};
    std::function<bool(const T&)> predicate{[](const T&) { return true; }};
    std::string_view error_message{"Invalid parameter value"};
};

/// @brief A compile-time tuple-based validator for multiple HTTP request parameters.
template<typename... Rules>
class validator {
public:
    explicit validator(Rules... rules) : m_rulesTuple(std::move(rules)...) {}

    void validate(const http::request& req) const {
        std::apply(
            [&](const auto&... rule_pack) {
                (this->validate_one(req, rule_pack), ...);
            },
            m_rulesTuple
        );
    }

private:
    template<typename T>
    void validate_one(const http::request& req, const rule<T>& r) const {
        auto result = req.get_value<T>(r.name);

        if (!result) {
            const auto& err = result.error();
            throw validation_error(
                std::string(r.name),
                validation_error::error_type::invalid_format,
                std::format("Invalid value: '{}'", err.original_value)
            );
        }

        const auto& maybe_value = *result;
        
        // FIX: Treat empty strings from form-data as conceptually "missing"
        bool is_empty_string = false;
        if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>) {
            if (maybe_value.has_value() && maybe_value->empty()) {
                is_empty_string = true;
            }
        }

        if (!maybe_value.has_value() || is_empty_string) {
            if (r.req == requirement::required) {
                throw validation_error(
                    std::string(r.name),
                    validation_error::error_type::missing_required_param,
                    "Required parameter is missing or empty."
                );
            }
            return; // If it's optional and missing/empty, validation cleanly passes
        }
        
        if (!r.predicate(*maybe_value)) {
            throw validation_error(
                std::string(r.name),
                validation_error::error_type::custom_rule_failed,
                std::string(r.error_message)
            );
        }
    }

    std::tuple<Rules...> m_rulesTuple;
};

} // namespace validation

#endif // INPUT_VALIDATOR_HPP
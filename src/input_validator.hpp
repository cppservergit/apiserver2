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

using invariant_error = std::pair<std::string, std::string>;
using invariant_result = std::expected<void, invariant_error>;

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

/// @brief A rule defining validation criteria for a nested JSON array.
template<typename... SubRules>
struct array_rule {
    std::string_view name;
    requirement req{requirement::required};
    std::tuple<SubRules...> sub_rules;

    explicit array_rule(std::string_view n, requirement r, std::tuple<SubRules...> sub)
        : name(n), req(r), sub_rules(std::move(sub)) {}

    explicit array_rule(std::string_view n, requirement r, SubRules... rules)
        : name(n), req(r), sub_rules(std::make_tuple(std::move(rules)...)) {}
};

/// @brief A compile-time tuple-based validator for multiple HTTP request parameters.
template<typename... Rules>
class validator {
public:
    // 1. Single constructor. CTAD handles standard rules AND trailing lambdas natively.
    explicit validator(Rules... rules) 
        : m_rulesTuple(std::move(rules)...) {}

    void validate(const http::request& req) const {
        // The fold expression evaluates strictly left-to-right.
        // If any validate_one throws, execution halts instantly.
        // Therefore, trailing invariants only run if all preceding rules pass.
        std::apply(
            [&](const auto&... rule_pack) {
                (this->validate_one(req, rule_pack), ...);
            },
            m_rulesTuple
        );
    }

private:
    std::tuple<Rules...> m_rulesTuple;

    // Overload A: Handles standard rule<T> definitions
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
        
        // Treat empty strings from form-data as conceptually "missing"
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
    
    // Overload B: Handles cross-parameter invariants (Lambdas)
    // Constrained so it only matches callables that return invariant_result
    template<typename Callable>
    requires std::is_invocable_r_v<invariant_result, Callable, const http::request&>
    void validate_one(const http::request& req, const Callable& c) const {
        auto result = c(req);
        
        if (!result.has_value()) {
            const auto& [param_name, msg] = result.error();
            throw validation_error(
                param_name,
                validation_error::error_type::custom_rule_failed,
                msg
            );
        }
    }

    // Overload C: Handles array_rule definitions
    template<typename... SubRules>
    void validate_one(const http::request& req, const array_rule<SubRules...>& r) const {
        const auto* json = req.get_json_payload();
        if (!json || !json->has_key(r.name)) {
            if (r.req == requirement::required) {
                throw validation_error(
                    std::string(r.name),
                    validation_error::error_type::missing_required_param,
                    "Required array is missing."
                );
            }
            return;
        }

        try {
            auto array_node = json->at(r.name);
            if (r.req == requirement::required && array_node.size() == 0) {
                throw validation_error(
                    std::string(r.name),
                    validation_error::error_type::missing_required_param,
                    "Required array is empty."
                );
            }
            for (size_t i = 0; i < array_node.size(); ++i) {
                validate_nested(array_node.at(i), r.sub_rules);
            }
        } catch (const json::parsing_error&) {
            throw validation_error(
                std::string(r.name),
                validation_error::error_type::invalid_format,
                "Field is not an array."
            );
        }
    }

    // Helper for nested validation using a JSON node
    template<typename... SubRules>
    void validate_nested(const json::json_parser& node, const std::tuple<SubRules...>& sub_rules) const {
        std::apply(
            [&](const auto&... rule_pack) {
                (this->validate_one_node(node, rule_pack), ...);
            },
            sub_rules
        );
    }

    // Overload D: Handles standard rule<T> within a nested node
    template<typename T>
    void validate_one_node(const json::json_parser& node, const rule<T>& r) const {
        if (!node.has_key(r.name)) {
            if (r.req == requirement::required) {
                throw validation_error(
                    std::string(r.name),
                    validation_error::error_type::missing_required_param,
                    "Required nested parameter is missing."
                );
            }
            return;
        }

        std::string_view value_sv = node.get_string(r.name);
        T value;

        // Unified parsing logic matching http::request::get_value<T>
        if constexpr (std::is_same_v<T, std::string>) {
            value = std::string(value_sv);
        } else if constexpr (std::is_same_v<T, std::string_view>) {
            value = value_sv;
        } else if (value_sv.empty()) {
            if (r.req == requirement::required) {
                throw validation_error(
                    std::string(r.name),
                    validation_error::error_type::missing_required_param,
                    "Required nested parameter is empty."
                );
            }
            return;
        } else if constexpr (http::is_chrono_type<T>) {
            if (auto parsed = http::parse_chrono_type<T>(value_sv)) {
                value = *parsed;
            } else {
                throw validation_error(
                    std::string(r.name),
                    validation_error::error_type::invalid_format,
                    "Invalid nested date/time format."
                );
            }
        } else if constexpr (std::is_arithmetic_v<T>) {
            auto [ptr, ec] = std::from_chars(value_sv.data(), value_sv.data() + value_sv.size(), value);
            if (ec != std::errc() || ptr != value_sv.data() + value_sv.size()) {
                throw validation_error(
                    std::string(r.name),
                    validation_error::error_type::invalid_format,
                    "Invalid nested numeric format."
                );
            }
        } else {
            // Fallback for other types supported by json_parser::get<T>
            try {
                value = node.get<T>(r.name);
            } catch (...) {
                throw validation_error(
                    std::string(r.name),
                    validation_error::error_type::invalid_format,
                    "Invalid nested parameter format."
                );
            }
        }

        // Final check for predicate
        if (!r.predicate(value)) {
            throw validation_error(
                std::string(r.name),
                validation_error::error_type::custom_rule_failed,
                std::string(r.error_message)
            );
        }
    }

    // Overload E: Handles recursive array_rule definitions within a node
    template<typename... SubRules>
    void validate_one_node(const json::json_parser& node, const array_rule<SubRules...>& r) const {
        if (!node.has_key(r.name)) {
            if (r.req == requirement::required) {
                throw validation_error(
                    std::string(r.name),
                    validation_error::error_type::missing_required_param,
                    "Required nested array is missing."
                );
            }
            return;
        }

        try {
            auto array_node = node.at(r.name);
            if (r.req == requirement::required && array_node.size() == 0) {
                throw validation_error(
                    std::string(r.name),
                    validation_error::error_type::missing_required_param,
                    "Required nested array is empty."
                );
            }
            for (size_t i = 0; i < array_node.size(); ++i) {
                validate_nested(array_node.at(i), r.sub_rules);
            }
        } catch (...) {
            throw validation_error(
                std::string(r.name),
                validation_error::error_type::invalid_format,
                "Field is not an array."
            );
        }
    }
};

} // namespace validation

#endif // INPUT_VALIDATOR_HPP
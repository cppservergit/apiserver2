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


/// @brief A validation rule for a parameter of a specific type.
/// @tparam T The expected type of the parameter.
template<typename T>
class rule {
public:
    // Constructor for simple rules (e.g., just checking for presence).
    explicit rule(std::string_view name_sv, requirement r)
        : name(name_sv),
          req(r),
          predicate([](const T&){ return true; }), // Default "always pass" predicate
          error_message("")
    {}

    // Constructor for rules with a custom validation predicate.
    explicit rule(std::string_view name_sv, requirement r, std::function<bool(const T&)> p, std::string_view msg)
        : name(name_sv),
          req(r),
          predicate(std::move(p)),
          error_message(msg)
    {}

    // Public members for the validator to access.
    std::string_view name;
    requirement req;
    std::function<bool(const T&)> predicate;
    std::string_view error_message;
};


/// @class validator
/// @brief A compile-time class to build and execute a set of validation rules.
template<typename... Rules>
class validator {
public:
    /// @brief Constructs a validator with a set of rules.
    explicit constexpr validator(Rules... rules) : m_rulesTuple{std::move(rules)...} {}

    /// @brief Validates an HTTP request against all rules.
    /// @param req The http::request object to validate.
    /// @throws validation::validation_error on the first rule that fails.
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
        if (!maybe_value.has_value()) {
            if (r.req == requirement::required) {
                throw validation_error(
                    std::string(r.name),
                    validation_error::error_type::missing_required_param,
                    "Required parameter is missing."
                );
            }
            return;
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

#ifndef SQL_TPP
#define SQL_TPP

#include <iostream>
#include <tuple>
#include <array>
#include <type_traits>
#include <string>
#include <vector>
#include <chrono>
#include <format>

namespace sql {
namespace detail {

// --- New, more robust parameter processing ---

// Helper to convert any string-like type into a std::string for safe binding.
// Also converts size_t to long long and year_month_day to a formatted string.
// Other types are passed through unchanged.
template<typename T>
auto convert_for_binding(T&& value) {
    using DecayedT = std::decay_t<T>;
    if constexpr (std::is_convertible_v<DecayedT, std::string_view>) {
        // This handles const char*, std::string, and std::string_view
        return std::string(std::forward<T>(value)); // Create a std::string copy
    } else if constexpr (std::is_same_v<DecayedT, size_t>) {
        // Convert size_t to long long, as the binding logic handles long long (as BIGINT).
        return static_cast<long long>(value);
    } else if constexpr (std::is_same_v<DecayedT, std::chrono::year_month_day>) {
        // Convert year_month_day to "YYYY-MM-DD" string format for the database.
        return std::format("{:%F}", value);
    }
    else {
        return std::forward<T>(value); // Pass others (int, long, double) through
    }
}

// Creates a tuple that owns std::string copies of all string-like parameters.
template<typename... Args>
auto make_binding_tuple(Args&&... args) {
    return std::make_tuple(convert_for_binding(std::forward<Args>(args))...);
}


// --- Parameter Binding Implementation (shared by get and query) ---

template<typename TupleType>
void bind_all_params(StmtHandle& stmt, const TupleType& params_tuple, std::array<SQLLEN, std::tuple_size_v<TupleType>>& indicators) {

    auto bind_one = [&stmt, &indicators](int index, const auto& value) {
        using T = std::decay_t<decltype(value)>;
        
        if constexpr (std::is_same_v<T, std::string>) {
            indicators[index - 1] = SQL_NTS; // Use Null-Terminated String indicator
            SQLRETURN r = SQLBindParameter(stmt.get(), index, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 
                                           value.length(), 0, (SQLPOINTER)value.c_str(), 0, &indicators[index - 1]);
            check_odbc_error(r, stmt.get(), SQL_HANDLE_STMT, "SQLBindParameter (string)");
        } else if constexpr (std::is_same_v<T, int>) {
            // Pass by address for correctness and portability
            SQLRETURN r = SQLBindParameter(stmt.get(), index, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, (SQLPOINTER)&value, 0, nullptr);
            check_odbc_error(r, stmt.get(), SQL_HANDLE_STMT, "SQLBindParameter (int)");
        } else if constexpr (std::is_same_v<T, long> || std::is_same_v<T, long long>) {
            // Pass by address for correctness and portability
            SQLRETURN r = SQLBindParameter(stmt.get(), index, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, (SQLPOINTER)&value, 0, nullptr);
            check_odbc_error(r, stmt.get(), SQL_HANDLE_STMT, "SQLBindParameter (long)");
        } else if constexpr (std::is_same_v<T, double>) {
            SQLRETURN r = SQLBindParameter(stmt.get(), index, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_DOUBLE, 0, 0, (SQLPOINTER)&value, 0, nullptr);
            check_odbc_error(r, stmt.get(), SQL_HANDLE_STMT, "SQLBindParameter (double)");
        } else {
            static_assert(sizeof(T) == 0, "Unsupported type for SQL parameter binding");
        }
    };

    int param_index = 1;
    std::apply([&](const auto&... param) {
        (bind_one(param_index++, param), ...);
    }, params_tuple);
}

} // namespace detail


// --- Public `get` Function Implementation ---

template<typename... Args>
[[nodiscard]] std::optional<std::string> get(std::string_view db_key, std::string_view sql_query, Args&&... args) {
    try {
        detail::Connection& conn = detail::ConnectionManager::get_connection(db_key);
        detail::StmtHandle& stmt = conn.get_or_create_statement(sql_query);

        auto params_tuple = detail::make_binding_tuple(std::forward<Args>(args)...);
        std::array<SQLLEN, sizeof...(args)> indicators;

        if constexpr (sizeof...(args) > 0) {
            SQLFreeStmt(stmt.get(), SQL_RESET_PARAMS);
            detail::bind_all_params(stmt, params_tuple, indicators);
        }

        SQLRETURN ret = SQLExecute(stmt.get());
        detail::check_odbc_error(ret, stmt.get(), SQL_HANDLE_STMT, "SQLExecute");

        std::string json_result;
        constexpr size_t buffer_size = 4096;
        std::vector<char> buffer(buffer_size);
        SQLLEN indicator;
        bool has_data = false;

        while ((ret = SQLFetch(stmt.get())) != SQL_NO_DATA) {
            detail::check_odbc_error(ret, stmt.get(), SQL_HANDLE_STMT, "SQLFetch");
            has_data = true;
            while (true) {
                ret = SQLGetData(stmt.get(), 1, SQL_C_CHAR, buffer.data(), buffer.size(), &indicator);
                if (ret == SQL_NO_DATA) break;
                detail::check_odbc_error(ret, stmt.get(), SQL_HANDLE_STMT, "SQLGetData");
                if (indicator > 0 && indicator != SQL_NULL_DATA) {
                    json_result.append(buffer.data(), static_cast<size_t>(indicator));
                }
                if (ret != SQL_SUCCESS_WITH_INFO) break;
            }
        }
        
        SQLFreeStmt(stmt.get(), SQL_CLOSE);
        return has_data ? std::optional{json_result} : std::nullopt;

    } catch (const sql::error& e) {
        throw;
    } catch (const std::exception& e) {
        throw sql::error(std::format("Generic exception in sql::get: {}", e.what()));
    }
}

// --- Public `query` Function Implementation ---

template<typename... Args>
[[nodiscard]] resultset query(std::string_view db_key, std::string_view sql_query, Args&&... args) {
    try {
        detail::Connection& conn = detail::ConnectionManager::get_connection(db_key);
        detail::StmtHandle& stmt = conn.get_or_create_statement(sql_query);

        auto params_tuple = detail::make_binding_tuple(std::forward<Args>(args)...);
        std::array<SQLLEN, sizeof...(args)> indicators;

        if constexpr (sizeof...(args) > 0) {
            SQLFreeStmt(stmt.get(), SQL_RESET_PARAMS);
            detail::bind_all_params(stmt, params_tuple, indicators);
        }
        
        SQLRETURN ret = SQLExecute(stmt.get());
        detail::check_odbc_error(ret, stmt.get(), SQL_HANDLE_STMT, "SQLExecute");
        
        resultset rs = stmt.fetch_all();
        SQLFreeStmt(stmt.get(), SQL_CLOSE);
        return rs;

    } catch (const sql::error& e) {
        throw;
    } catch (const std::exception& e) {
        throw sql::error(std::format("Generic exception in sql::query: {}", e.what()));
    }
}

// --- Public `exec` Function Implementation ---

template<typename... Args>
void exec(std::string_view db_key, std::string_view sql_query, Args&&... args) {
    try {
        detail::Connection& conn = detail::ConnectionManager::get_connection(db_key);
        detail::StmtHandle& stmt = conn.get_or_create_statement(sql_query);

        auto params_tuple = detail::make_binding_tuple(std::forward<Args>(args)...);
        std::array<SQLLEN, sizeof...(args)> indicators;

        if constexpr (sizeof...(args) > 0) {
            SQLFreeStmt(stmt.get(), SQL_RESET_PARAMS);
            detail::bind_all_params(stmt, params_tuple, indicators);
        }
        
        SQLRETURN ret = SQLExecute(stmt.get());
        detail::check_odbc_error(ret, stmt.get(), SQL_HANDLE_STMT, "SQLExecute");
        
        SQLFreeStmt(stmt.get(), SQL_CLOSE);
    } catch (const sql::error& e) {
        throw;
    } catch (const std::exception& e) {
        throw sql::error(std::format("Generic exception in sql::exec: {}", e.what()));
    }
}


} // namespace sql

#endif // SQL_TPP

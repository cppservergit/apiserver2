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

/**
 * @brief Fetches a single-column result from all rows, handling chunked data.
 * @param stmt The statement handle to fetch from.
 * @return An optional containing the complete concatenated string result, or nullopt if no data.
 */
[[nodiscard]] inline std::optional<std::string> fetch_json_result(StmtHandle& stmt) {
    std::string result_string;
    constexpr size_t buffer_size = 4096;
    std::vector<char> buffer(buffer_size);
    SQLLEN indicator;
    SQLRETURN ret;
    bool has_data = false;

    // Loop through all rows returned by the query
    while ((ret = SQLFetch(stmt.get())) != SQL_NO_DATA) {
        check_odbc_error(ret, stmt.get(), SQL_HANDLE_STMT, "SQLFetch");
        has_data = true;

        // Inner loop to get all data for the first column of the current row,
        // which may be sent in chunks.
        while (true) {
            ret = SQLGetData(stmt.get(), 1, SQL_C_CHAR, buffer.data(), buffer.size(), &indicator);
            if (ret == SQL_NO_DATA) {
                break; // Finished fetching all chunks for this column
            }
            check_odbc_error(ret, stmt.get(), SQL_HANDLE_STMT, "SQLGetData");
            
            if (indicator > 0 && indicator != SQL_NULL_DATA) {
                result_string.append(buffer.data(), static_cast<size_t>(indicator));
            }
            
            // If SQLGetData returns SQL_SUCCESS_WITH_INFO, it means the buffer was too small
            // and there is more data to fetch. The loop will continue. Otherwise, we break.
            if (ret != SQL_SUCCESS_WITH_INFO) {
                break;
            }
        }
    }

    return has_data ? std::optional{result_string} : std::nullopt;
}


} // namespace detail


// --- Public `get` Function Implementation (Restored Behavior) ---
template<typename... Args>
[[nodiscard]] std::optional<std::string> get(std::string_view db_key, std::string_view sql_query, Args&&... args) {
    // Add a retry loop that will run at most twice.
    for (int attempt = 1; attempt <= 2; ++attempt) {
        try {
            detail::Connection& conn = detail::ConnectionManager::get_connection(db_key);
            detail::StmtHandle& stmt = conn.get_or_create_statement(sql_query);

            auto params_tuple = detail::make_binding_tuple(std::forward<Args>(args)...);
            std::array<SQLLEN, sizeof...(args)> indicators;
            if constexpr (sizeof...(args) > 0) {
                SQLFreeStmt(stmt.get(), SQL_RESET_PARAMS);
                detail::bind_all_params(stmt, params_tuple, indicators);
            }

            const auto start_time = std::chrono::high_resolution_clock::now();
            
            SQLRETURN ret = SQLExecute(stmt.get());
            const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start_time);
            util::log::perf("SQL on '{}' took {} microseconds. Query: {}", db_key, duration.count(), sql_query);
            detail::check_odbc_error(ret, stmt.get(), SQL_HANDLE_STMT, "SQLExecute");

            // Use the helper function to fetch the single-column result.
            auto result = detail::fetch_json_result(stmt);
            SQLFreeStmt(stmt.get(), SQL_CLOSE);
            return result; // Success, exit the loop and return.

        } catch (const sql::error& e) {
            // Check if this is a retryable connection error and it's the first attempt.
            if (attempt == 1 && (e.sqlstate == "HY000" || e.sqlstate == "01000" || e.sqlstate == "08S01")) {
                util::log::warn("SQL connection error on '{}' (SQLSTATE: {}). Attempting reconnect. Error: {}", db_key, e.sqlstate, e.what());
                detail::ConnectionManager::invalidate_connection(db_key);
                continue; // Go to the next loop iteration to retry.
            } else {
                // Not a retryable error or we already failed a retry, so re-throw.
                throw;
            }
        } catch (const std::exception& e) {
            throw sql::error(std::format("Generic exception in sql::get: {}", e.what()));
        }
    }
    // This line should not be reachable, but it prevents compiler warnings.
    throw sql::error("SQL get failed after multiple attempts.");
}


// --- Public `query` Function Implementation ---
template<typename... Args>
[[nodiscard]] resultset query(std::string_view db_key, std::string_view sql_query, Args&&... args) {
    // Add a retry loop that will run at most twice.
    for (int attempt = 1; attempt <= 2; ++attempt) {
        try {
            detail::Connection& conn = detail::ConnectionManager::get_connection(db_key);
            detail::StmtHandle& stmt = conn.get_or_create_statement(sql_query);

            auto params_tuple = detail::make_binding_tuple(std::forward<Args>(args)...);
            std::array<SQLLEN, sizeof...(args)> indicators;
            if constexpr (sizeof...(args) > 0) {
                SQLFreeStmt(stmt.get(), SQL_RESET_PARAMS);
                detail::bind_all_params(stmt, params_tuple, indicators);
            }
            
            const auto start_time = std::chrono::high_resolution_clock::now();
            SQLRETURN ret = SQLExecute(stmt.get());

            const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start_time);
            util::log::perf("SQL on '{}' took {} microseconds. Query: {}", db_key, duration.count(), sql_query);

            detail::check_odbc_error(ret, stmt.get(), SQL_HANDLE_STMT, "SQLExecute");
            
            resultset rs = stmt.fetch_all();
            SQLFreeStmt(stmt.get(), SQL_CLOSE);
            return rs; // Success, exit the loop and return.

        } catch (const sql::error& e) {
            // Check if this is a retryable connection error and it's the first attempt.
            if (attempt == 1 && (e.sqlstate == "HY000" || e.sqlstate == "01000" || e.sqlstate == "08S01")) {
                util::log::warn("SQL connection error on '{}' (SQLSTATE: {}). Attempting reconnect. Error: {}", db_key, e.sqlstate, e.what());
                detail::ConnectionManager::invalidate_connection(db_key);
                continue; // Go to the next loop iteration to retry.
            } else {
                // Not a retryable error or we already failed a retry, so re-throw.
                throw;
            }
        } catch (const std::exception& e) {
            throw sql::error(std::format("Generic exception in sql::query: {}", e.what()));
        }
    }
    // This line should not be reachable, but it prevents compiler warnings.
    throw sql::error("SQL query failed after multiple attempts.");
}

// --- Public `exec` Function Implementation ---
template<typename... Args>
void exec(std::string_view db_key, std::string_view sql_query, Args&&... args) {
    // Add a retry loop that will run at most twice.
    for (int attempt = 1; attempt <= 2; ++attempt) {
        try {
            detail::Connection& conn = detail::ConnectionManager::get_connection(db_key);
            detail::StmtHandle& stmt = conn.get_or_create_statement(sql_query);

            auto params_tuple = detail::make_binding_tuple(std::forward<Args>(args)...);
            std::array<SQLLEN, sizeof...(args)> indicators;
            if constexpr (sizeof...(args) > 0) {
                SQLFreeStmt(stmt.get(), SQL_RESET_PARAMS);
                detail::bind_all_params(stmt, params_tuple, indicators);
            }
            
            const auto start_time = std::chrono::high_resolution_clock::now();
            SQLRETURN ret = SQLExecute(stmt.get());
            
            const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start_time);
            util::log::perf("SQL on '{}' took {} microseconds. Query: {}", db_key, duration.count(), sql_query);

            detail::check_odbc_error(ret, stmt.get(), SQL_HANDLE_STMT, "SQLExecute");
            
            SQLFreeStmt(stmt.get(), SQL_CLOSE);
            return; // Success, exit the function.

        } catch (const sql::error& e) {
            // Check if this is a retryable connection error and it's the first attempt.
            if (attempt == 1 && (e.sqlstate == "HY000" || e.sqlstate == "01000" || e.sqlstate == "08S01")) {
                util::log::warn("SQL connection error on '{}' (SQLSTATE: {}). Attempting reconnect. Error: {}", db_key, e.sqlstate, e.what());
                detail::ConnectionManager::invalidate_connection(db_key);
                continue; // Go to the next loop iteration to retry.
            } else {
                // Not a retryable error or we already failed a retry, so re-throw.
                throw;
            }
        } catch (const std::exception& e) {
            throw sql::error(std::format("Generic exception in sql::exec: {}", e.what()));
        }
    }
    // This line should not be reachable, but it prevents compiler warnings.
    throw sql::error("SQL exec failed after multiple attempts.");
}

namespace detail {
    // Helper to escape characters for a JSON string literal.
    inline void append_escaped_json_string(std::string& builder, std::string_view sv) {
        builder.push_back('"');
        for (char c : sv) {
            switch (c) {
                case '"':  builder.append("\\\""); break;
                case '\\': builder.append("\\\\"); break;
                case '\b': builder.append("\\b"); break;
                case '\f': builder.append("\\f"); break;
                case '\n': builder.append("\\n"); break;
                case '\r': builder.append("\\r"); break;
                case '\t': builder.append("\\t"); break;
                default:
                    if ('\x00' <= c && c <= '\x1f') {
                        // Append control characters as unicode escapes, though this is rare.
                        builder.append(std::format("\\u{:04x}", static_cast<unsigned char>(c)));
                    } else {
                        builder.push_back(c);
                    }
                    break;
            }
        }
        builder.push_back('"');
    }

    [[nodiscard]] inline std::optional<std::string> fetch_and_build_json(StmtHandle& stmt) {
        SQLSMALLINT num_cols = 0;
        check_odbc_error(SQLNumResultCols(stmt.get(), &num_cols), stmt.get(), SQL_HANDLE_STMT, "SQLNumResultCols");

        if (num_cols == 0) {
            return std::optional{std::string("[]")};
        }

        // 1. Get column metadata once
        std::vector<std::string> col_names;
        std::vector<SQLSMALLINT> col_types;
        col_names.reserve(num_cols);
        col_types.reserve(num_cols);

        for (SQLUSMALLINT i = 1; i <= num_cols; ++i) {
            std::vector<SQLCHAR> col_name_buffer(256);
            SQLSMALLINT name_len = 0, data_type = 0;
            check_odbc_error(SQLDescribeCol(stmt.get(), i, col_name_buffer.data(), col_name_buffer.size(), &name_len, &data_type, nullptr, nullptr, nullptr),
                            stmt.get(), SQL_HANDLE_STMT, "SQLDescribeCol");
            col_names.emplace_back(reinterpret_cast<char*>(col_name_buffer.data()), name_len);
            col_types.push_back(data_type);
        }

        // 2. Build the JSON string
        std::string json_builder;
        json_builder.reserve(4096);
        json_builder.push_back('[');

        bool first_row = true;
        while (SQLFetch(stmt.get()) == SQL_SUCCESS) {
            if (!first_row) {
                json_builder.push_back(',');
            }
            first_row = false;
            
            json_builder.push_back('{');

            for (SQLUSMALLINT i = 1; i <= num_cols; ++i) {
                SQLLEN indicator;
                std::vector<char> buffer(4096);
                SQLGetData(stmt.get(), i, SQL_C_CHAR, buffer.data(), buffer.size(), &indicator);
                
                // Append key: "column_name":
                append_escaped_json_string(json_builder, col_names[i - 1]);
                json_builder.push_back(':');

                if (indicator == SQL_NULL_DATA) {
                    json_builder.append("null");
                } else {
                    std::string_view value_sv(buffer.data());
                    // Check the SQL data type to decide on quoting
                    switch (col_types[i - 1]) {
                        case SQL_BIT:
                        case SQL_TINYINT:
                        case SQL_SMALLINT:
                        case SQL_INTEGER:
                        case SQL_BIGINT:
                        case SQL_REAL:
                        case SQL_FLOAT:
                        case SQL_DOUBLE:
                        case SQL_DECIMAL:
                        case SQL_NUMERIC:
                            json_builder.append(value_sv); // Numeric types are not quoted
                            break;
                        default:
                            // String, date, time, and other types are quoted
                            append_escaped_json_string(json_builder, value_sv);
                            break;
                    }
                }

                if (i < num_cols) {
                    json_builder.push_back(',');
                }
            }
            json_builder.push_back('}');
        }

        json_builder.push_back(']');
        return std::optional{json_builder};
    }

}

template<typename... Args>
[[nodiscard]] std::optional<std::string> get_json(std::string_view db_key, std::string_view sql_query, Args&&... args) {
    for (int attempt = 1; attempt <= 2; ++attempt) {
        try {
            detail::Connection& conn = detail::ConnectionManager::get_connection(db_key);
            detail::StmtHandle& stmt = conn.get_or_create_statement(sql_query);

            auto params_tuple = detail::make_binding_tuple(std::forward<Args>(args)...);
            std::array<SQLLEN, sizeof...(args)> indicators;
            if constexpr (sizeof...(args) > 0) {
                SQLFreeStmt(stmt.get(), SQL_RESET_PARAMS);
                detail::bind_all_params(stmt, params_tuple, indicators);
            }
            
            const auto start_time = std::chrono::high_resolution_clock::now();
            SQLRETURN ret = SQLExecute(stmt.get());
            const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start_time);
            util::log::perf("SQL on '{}' took {} microseconds. Query: {}", db_key, duration.count(), sql_query);
            detail::check_odbc_error(ret, stmt.get(), SQL_HANDLE_STMT, "SQLExecute");
            
            // The only change is here: call the new JSON builder
            auto result = detail::fetch_and_build_json(stmt);
            
            SQLFreeStmt(stmt.get(), SQL_CLOSE);
            return result;

        } catch (const sql::error& e) {
            if (attempt == 1 && (e.sqlstate == "HY000" || e.sqlstate == "01000" || e.sqlstate == "08S01")) {
                util::log::warn("SQL connection error on '{}' (SQLSTATE: {}). Attempting reconnect. Error: {}", db_key, e.sqlstate, e.what());
                detail::ConnectionManager::invalidate_connection(db_key);
                continue;
            } else {
                throw;
            }
        } catch (const std::exception& e) {
            throw sql::error(std::format("Generic exception in sql::get_json: {}", e.what()));
        }
    }
    throw sql::error("SQL get_json failed after multiple attempts.");
}

} // namespace sql

#endif // SQL_TPP

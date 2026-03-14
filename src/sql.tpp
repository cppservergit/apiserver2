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
#include <optional> // Required for std::optional logic
#include <ranges>   // Required for std::from_range and std::views

namespace sql {
namespace detail {

// --- Type Traits Helper ---
template <typename T>
struct is_optional : std::false_type {};

template <typename T>
struct is_optional<std::optional<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_optional_v = is_optional<T>::value;

// --- New, more robust parameter processing ---

// Helper to extract common conversion logic for primitive types
inline auto convert_base_value(/* NOSONAR */ auto&& value) {
    using DecayedT = std::decay_t<decltype(value)>;
    if constexpr (std::is_convertible_v<DecayedT, std::string_view>) {
        return std::string(std::forward<decltype(value)>(value)); 
    } else if constexpr (std::is_same_v<DecayedT, size_t>) {
        return static_cast<long long>(value);
    } else if constexpr (std::is_same_v<DecayedT, std::chrono::year_month_day>) {
        return std::format("{:%F}", value);
    } else {
        return std::forward<decltype(value)>(value);
    }
}

// Helper to convert any string-like type into a std::string for safe binding.
// Also converts size_t to long long and year_month_day to a formatted string.
// Passes std::optional through safely owning the converted inner value.
auto convert_for_binding(/* NOSONAR */ auto&& value) {
    using DecayedT = std::decay_t<decltype(value)>;
    
    // FIX: Must convert the inner value of the optional so it safely owns the string memory inside the tuple!
    if constexpr (is_optional_v<DecayedT>) {
        // C++23 monadic operations elegantly handle optional unwrapping, converting, and re-wrapping
        return value.transform([](/* NOSONAR */ auto&& v) {
            return convert_base_value(std::forward<decltype(v)>(v));
        });
    } else {
        return convert_base_value(std::forward<decltype(value)>(value));
    }
}

// Creates a tuple that owns std::string copies of all string-like parameters.
auto make_binding_tuple(/* NOSONAR */ auto&&... args) {
    return std::make_tuple(convert_for_binding(std::forward<decltype(args)>(args))...);
}


// --- Parameter Binding Implementation (shared by get and query) ---

template<typename TupleType>
void bind_all_params(StmtHandle& stmt, const TupleType& params_tuple, std::array<SQLLEN, std::tuple_size_v<TupleType>>& indicators) {

    auto bind_concrete_value = [&stmt, &indicators]<typename T>(int index, const T& val) {
        using V = std::decay_t<T>;
        if constexpr (std::is_same_v<V, std::string>) {
            indicators[index - 1] = SQL_NTS; 
            SQLRETURN r = SQLBindParameter(stmt.get(), index, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 
                                        val.length(), 0, (SQLPOINTER)val.c_str(), 0, &indicators[index - 1]);
            check_odbc_error(r, stmt.get(), SQL_HANDLE_STMT, "SQLBindParameter (string)");
        } else if constexpr (std::is_same_v<V, int>) {
            SQLRETURN r = SQLBindParameter(stmt.get(), index, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, (SQLPOINTER)&val, 0, nullptr);
            check_odbc_error(r, stmt.get(), SQL_HANDLE_STMT, "SQLBindParameter (int)");
        } else if constexpr (std::is_same_v<V, long> || std::is_same_v<V, long long>) {
            SQLRETURN r = SQLBindParameter(stmt.get(), index, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, (SQLPOINTER)&val, 0, nullptr);
            check_odbc_error(r, stmt.get(), SQL_HANDLE_STMT, "SQLBindParameter (long)");
        } else if constexpr (std::is_same_v<V, double>) {
            SQLRETURN r = SQLBindParameter(stmt.get(), index, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_DOUBLE, 0, 0, (SQLPOINTER)&val, 0, nullptr);
            check_odbc_error(r, stmt.get(), SQL_HANDLE_STMT, "SQLBindParameter (double)");
        }
    };

    auto bind_one = [&stmt, &indicators, &bind_concrete_value]<typename P>(int index, const P& param_value) {
        using T = std::decay_t<P>;
        
        if constexpr (is_optional_v<T>) {
             if (!param_value.has_value()) {
                indicators[index - 1] = SQL_NULL_DATA;
                SQLRETURN r = SQLBindParameter(stmt.get(), index, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 1, 0, nullptr, 0, &indicators[index - 1]);
                check_odbc_error(r, stmt.get(), SQL_HANDLE_STMT, "SQLBindParameter (NULL)");
             } else {
                 // FIX: Do not call convert_for_binding here. param_value.value() is already 
                 // securely owned and converted inside the params_tuple.
                 bind_concrete_value(index, param_value.value());
             }
        } else {
             bind_concrete_value(index, param_value);
        }
    };

    // Using C++20/23 generic lambdas and index sequences to eliminate mutable states in fold expressions
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (bind_one(static_cast<int>(Is + 1), std::get<Is>(params_tuple)), ...);
    }(std::make_index_sequence<std::tuple_size_v<TupleType>>{});
}

/**
 * @brief Helper to fetch chunked data for a single column and append to a string.
 */
inline void fetch_column_chunks(StmtHandle& stmt, std::string& result_string) {
    SQLLEN indicator;
    SQLRETURN ret;
    bool has_more_chunks = true;

    while (has_more_chunks) {
        size_t current_size = result_string.size();
        
        // C++23: Direct API write into the string's capacity buffer to avoid intermediate allocations
        // FIX: Restored /* NOSONAR */ so SonarQube stops complaining about the [&] default capture
        result_string.resize_and_overwrite(current_size + 8192, [&] /* NOSONAR */ (char* buf, size_t /* capacity */) {
            ret = SQLGetData(stmt.get(), 1, SQL_C_CHAR, buf + current_size, 8192, &indicator);
            
            if (ret == SQL_NO_DATA) return current_size; // String size unchanged
            
            check_odbc_error(ret, stmt.get(), SQL_HANDLE_STMT, "SQLGetData");
            
            if (indicator > 0 && indicator != SQL_NULL_DATA) {
                has_more_chunks = (ret == SQL_SUCCESS_WITH_INFO);
                return current_size + static_cast<size_t>(indicator); // Set new exact size
            }
            
            has_more_chunks = false;
            return current_size;
        });
    }
}

/**
 * @brief Fetches a single-column result from all rows, handling chunked data.
 * @param stmt The statement handle to fetch from.
 * @return An optional containing the complete concatenated string result, or nullopt if no data.
 */
[[nodiscard]] inline std::optional<std::string> fetch_json_result(StmtHandle& stmt) {
    std::string result_string;
    SQLRETURN ret;
    bool has_data = false;

    // Loop through all rows returned by the query
    while ((ret = SQLFetch(stmt.get())) != SQL_NO_DATA) {
        check_odbc_error(ret, stmt.get(), SQL_HANDLE_STMT, "SQLFetch");
        has_data = true;

        // Fetch all chunks for the first column of the current row
        fetch_column_chunks(stmt, result_string);
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

            auto params_tuple = detail::make_binding_tuple(std::forward<decltype(args)>(args)...);
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

            auto params_tuple = detail::make_binding_tuple(std::forward<decltype(args)>(args)...);
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

            auto params_tuple = detail::make_binding_tuple(std::forward<decltype(args)>(args)...);
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
                case '"':  builder.append(R"(\")"); break;
                case '\\': builder.append(R"(\\)"); break;
                case '\b': builder.append("\\b"); break;
                case '\f': builder.append("\\f"); break;
                case '\n': builder.append("\\n"); break;
                case '\r': builder.append("\\r"); break;
                case '\t': builder.append("\\t"); break;
                default:
                    if ('\x{00}' <= c && c <= '\x{1f}') {
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

    // --- JSON Building Helpers ---

    struct ColumnMeta {
        std::string name;
        SQLSMALLINT type;
    };

    inline std::vector<ColumnMeta> get_result_metadata(StmtHandle& stmt, SQLSMALLINT num_cols) {
        std::vector<ColumnMeta> meta;
        meta.reserve(num_cols);
        for (SQLUSMALLINT i = 1; i <= num_cols; ++i) {
            std::array<SQLCHAR, 256> col_name_buffer;
            SQLSMALLINT name_len = 0;
            SQLSMALLINT data_type = 0;
            check_odbc_error(SQLDescribeCol(stmt.get(), i, col_name_buffer.data(), static_cast<SQLSMALLINT>(col_name_buffer.size()), &name_len, &data_type, nullptr, nullptr, nullptr),
                            stmt.get(), SQL_HANDLE_STMT, "SQLDescribeCol");
                            
            // FIX: Revert to iterator construction. std::from_range fails deduction because SQLCHAR (unsigned char) 
            // does not strictly match std::string's char type requirement in GCC 14.
            meta.push_back({
                std::string(col_name_buffer.begin(), col_name_buffer.begin() + name_len), 
                data_type
            });
        }
        return meta;
    }

    // FIX: Changed parameter to std::string_view to support robust chunking of large columns
    inline void append_json_value(std::string& builder, SQLSMALLINT col_type, SQLLEN indicator, std::string_view value_sv) {
        if (indicator == SQL_NULL_DATA) {
            builder.append("null");
            return;
        }
        
        switch (col_type) {
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
                builder.append(value_sv); // Numeric types are not quoted
                break;
            default:
                // String, date, time, and other types are quoted
                append_escaped_json_string(builder, value_sv);
                break;
        }
    }

    inline void append_json_row(std::string& json_builder, StmtHandle& stmt, const std::vector<ColumnMeta>& meta) {
        json_builder.push_back('{');
        SQLUSMALLINT num_cols = static_cast<SQLUSMALLINT>(meta.size());
        
        for (SQLUSMALLINT i = 1; i <= num_cols; ++i) {
            std::string cell_data;
            SQLLEN indicator;
            SQLRETURN ret;
            bool has_more_chunks = true;

            // ENHANCEMENT: Ported C++23 chunking to prevent large JSON column truncation.
            while (has_more_chunks) {
                size_t current_size = cell_data.size();
                cell_data.resize_and_overwrite(current_size + 8192, [&] /* NOSONAR */ (char* buf, size_t /* capacity */) {
                    ret = SQLGetData(stmt.get(), i, SQL_C_CHAR, buf + current_size, 8192, &indicator);
                    
                    if (ret == SQL_NO_DATA) return current_size;
                    
                    if (indicator > 0 && indicator != SQL_NULL_DATA) {
                        has_more_chunks = (ret == SQL_SUCCESS_WITH_INFO);
                        return current_size + static_cast<size_t>(indicator);
                    }
                    
                    has_more_chunks = false;
                    return current_size;
                });
            }
            
            // Append key: "column_name":
            append_escaped_json_string(json_builder, meta[i - 1].name);
            json_builder.push_back(':');

            // Append value
            append_json_value(json_builder, meta[i - 1].type, indicator, cell_data);

            if (i < num_cols) {
                json_builder.push_back(',');
            }
        }
        json_builder.push_back('}');
    }

    [[nodiscard]] inline std::optional<std::string> fetch_and_build_json(StmtHandle& stmt) {
        SQLSMALLINT num_cols = 0;
        check_odbc_error(SQLNumResultCols(stmt.get(), &num_cols), stmt.get(), SQL_HANDLE_STMT, "SQLNumResultCols");

        if (num_cols == 0) {
            return std::optional{std::string("[]")};
        }

        auto meta = get_result_metadata(stmt, num_cols);

        std::string json_builder;
        json_builder.reserve(8192);
        json_builder.push_back('[');

        bool first_row = true;
        while (SQLFetch(stmt.get()) == SQL_SUCCESS) {
            if (!first_row) {
                json_builder.push_back(',');
            }
            first_row = false;
            append_json_row(json_builder, stmt, meta);
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

            auto params_tuple = detail::make_binding_tuple(std::forward<decltype(args)>(args)...);
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
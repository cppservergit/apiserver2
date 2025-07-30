#include "sql.hpp"
#include <vector>

namespace sql {

template<typename T>
T row::get_value(std::string_view col_name) const {
    // FIX: Use "if with initializer" to scope the iterator `it` to the conditional.
    if (auto it = m_data.find(col_name); it != m_data.end()) {
        try {
            // Now that we have a valid iterator, get the value and perform the cast.
            const auto& value_any = it->second;
            return std::any_cast<T>(value_any);
        } catch (const std::bad_any_cast&) {
            // This catch block now only handles type casting errors.
            throw sql::error(std::format("Invalid type requested for column '{}'.", col_name));
        }
    } else {
        // Manually throw an exception to mimic the behavior of .at() when the key is not found.
        throw sql::error(std::format("Column '{}' not found in result set.", col_name));
    }
}

// Explicit template instantiations for common types
template std::string row::get_value<std::string>(std::string_view) const;
template int row::get_value<int>(std::string_view) const;
template long row::get_value<long>(std::string_view) const;
template double row::get_value<double>(std::string_view) const;
template bool row::get_value<bool>(std::string_view) const;


namespace detail {

// --- StmtHandle Helper Functions ---

/**
 * @brief Retrieves all column names for the current result set.
 * @param stmt_handle The ODBC statement handle.
 * @param num_cols The number of columns in the result set.
 * @return A vector of column names.
 */
std::vector<std::string> StmtHandle::get_column_names(SQLHSTMT stmt_handle, SQLSMALLINT num_cols) {
    std::vector<std::string> col_names;
    col_names.reserve(num_cols);
    for (SQLUSMALLINT i = 1; i <= num_cols; ++i) {
        std::vector<SQLCHAR> col_name_buffer(256);
        SQLSMALLINT name_len = 0;
        check_odbc_error(SQLDescribeCol(stmt_handle, i, col_name_buffer.data(), col_name_buffer.size(), &name_len, nullptr, nullptr, nullptr, nullptr),
                         stmt_handle, SQL_HANDLE_STMT, "SQLDescribeCol");
        col_names.emplace_back(reinterpret_cast<char*>(col_name_buffer.data()), name_len);
    }
    return col_names;
}

/**
 * @brief Fetches the data for a single row from the result set.
 * @param stmt_handle The ODBC statement handle.
 * @param num_cols The number of columns in the row.
 * @param col_names The names of the columns.
 * @return A sql::row object containing the row data.
 */
row StmtHandle::fetch_single_row(SQLHSTMT stmt_handle, SQLSMALLINT num_cols, const std::vector<std::string>& col_names) {
    row current_row;
    for (SQLUSMALLINT i = 1; i <= num_cols; ++i) {
        SQLLEN indicator;
        std::vector<char> buffer(1024); // Buffer for column data
        
        SQLRETURN ret = SQLGetData(stmt_handle, i, SQL_C_CHAR, buffer.data(), buffer.size(), &indicator);
        if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
            continue; // Skip column on error
        }

        if (indicator == SQL_NULL_DATA) {
            current_row.m_data[col_names[i - 1]] = std::any{}; // Store null as empty std::any
        } else {
            // This simple implementation treats all data as strings for now.
            // A more advanced version could check column types and convert.
            current_row.m_data[col_names[i - 1]] = std::string(buffer.data());
        }
    }
    return current_row;
}

resultset StmtHandle::fetch_all() const {
    resultset rs;
    
    // Get number of columns
    SQLSMALLINT num_cols = 0;
    check_odbc_error(SQLNumResultCols(get(), &num_cols), get(), SQL_HANDLE_STMT, "SQLNumResultCols");

    if (num_cols == 0) {
        return rs; // No columns, return empty result set
    }

    // Get column names using the helper function
    const auto col_names = StmtHandle::get_column_names(get(), num_cols);

    // Fetch rows using the helper function
    while (SQLFetch(get()) == SQL_SUCCESS) {
        rs.m_rows.push_back(StmtHandle::fetch_single_row(get(), num_cols, col_names));
    }
    
    return rs;
}


// --- Error Handling Implementation ---

void check_odbc_error(SQLRETURN retcode, SQLHANDLE handle, SQLSMALLINT handle_type, std::string_view context) {
    if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
        return;
    }

    std::vector<SQLCHAR> sql_state(6);
    SQLINTEGER native_error;
    std::vector<SQLCHAR> message_text(SQL_MAX_MESSAGE_LENGTH);
    SQLSMALLINT text_length = 0;
    std::string error_msg = std::format("ODBC Error on '{}': ", context);

    SQLSMALLINT i = 1;
    while (SQLGetDiagRec(handle_type, handle, i, sql_state.data(), &native_error, message_text.data(), message_text.size(), &text_length) == SQL_SUCCESS) {
        error_msg += std::format("[SQLState: {}] [Native Error: {}] {}",
                                 reinterpret_cast<char*>(sql_state.data()),
                                 native_error,
                                 reinterpret_cast<char*>(message_text.data()));
        i++;
    }

    if (retcode == SQL_NO_DATA) {
        return;
    }
    
    throw sql::error(error_msg);
}

// --- RAII Handle Implementation ---

EnvHandle::EnvHandle() {
    check_odbc_error(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, get_ptr()),
                     SQL_NULL_HANDLE, SQL_HANDLE_ENV, "SQLAllocHandle (ENV)");
    check_odbc_error(SQLSetEnvAttr(get(), SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0),
                     get(), SQL_HANDLE_ENV, "SQLSetEnvAttr (ODBC_VERSION)");
}

DbcHandle::DbcHandle(const EnvHandle& env) {
    check_odbc_error(SQLAllocHandle(SQL_HANDLE_DBC, env.get(), get_ptr()),
                     env.get(), SQL_HANDLE_ENV, "SQLAllocHandle (DBC)");
}

StmtHandle::StmtHandle(const DbcHandle& dbc) {
    check_odbc_error(SQLAllocHandle(SQL_HANDLE_STMT, dbc.get(), get_ptr()),
                     dbc.get(), SQL_HANDLE_DBC, "SQLAllocHandle (STMT)");
}

// --- Connection Implementation ---

Connection::Connection(std::string_view conn_str) : m_dbc(m_env) {
    std::vector<SQLCHAR> connection_string_buffer(conn_str.begin(), conn_str.end());
    connection_string_buffer.push_back('\0');

    check_odbc_error(SQLDriverConnect(m_dbc.get(), nullptr, connection_string_buffer.data(),
                                      SQL_NTS, nullptr, 0, nullptr, SQL_DRIVER_NOPROMPT),
                     m_dbc.get(), SQL_HANDLE_DBC, "SQLDriverConnect");
}

StmtHandle& Connection::get_or_create_statement(std::string_view sql_query) {
    if (auto it = m_statement_cache.find(sql_query); it != m_statement_cache.end()) {
        return *it->second;
    }
    // Statement not in cache, create and prepare it.
    auto new_stmt = std::make_unique<StmtHandle>(m_dbc);
    SQLRETURN ret = SQLPrepare(new_stmt->get(), /* NOSONAR */ (SQLCHAR*)sql_query.data(), SQL_NTS);
    check_odbc_error(ret, new_stmt->get(), SQL_HANDLE_STMT, "SQLPrepare (cached)");

    // Store it in the cache and return a reference.
    auto& stmt_ref = *new_stmt;
    m_statement_cache[std::string(sql_query)] = std::move(new_stmt);
    
    util::log::debug("Cached new prepared statement for {}", sql_query);
    return stmt_ref;
}

// --- Connection Manager Implementation ---
Connection& ConnectionManager::get_connection(std::string_view db_key) {
    // FIX: Use string_view directly for lookup
    if (auto it = m_connections.find(db_key); it != m_connections.end()) {
        return *it->second;
    }

    std::string conn_str = env::get<std::string>(std::string(db_key));
    auto new_conn = std::make_unique<Connection>(conn_str);
    auto& conn_ref = *new_conn;
    // FIX: Insertion requires a std::string key
    m_connections[std::string(db_key)] = std::move(new_conn);
    
    util::log::debug("Created new ODBC connection for '{}' on thread {}", db_key, std::this_thread::get_id());
    return conn_ref;
}

} // namespace sql::detail
} // namespace sql

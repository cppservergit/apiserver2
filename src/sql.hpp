#ifndef SQL_HPP
#define SQL_HPP

#include "env.hpp"
#include "logger.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
#include <memory>
#include <unordered_map>
#include <optional>
#include <format>
#include <any>

// Include ODBC headers
#include <sql.h>
#include <sqlext.h>

namespace sql {

// --- Forward Declarations for Internal Types ---
namespace detail {
    class StmtHandle;
}

// --- Public Interface ---

/// @class error
/// @brief Exception thrown for ODBC-related errors.
class error : public std::runtime_error {
public:
    // Keep the existing constructor for backward compatibility
    using std::runtime_error::runtime_error;

    // Add a new constructor that takes the SQLSTATE
    error(std::string_view message, std::string state)
        : std::runtime_error(std::string(message)), sqlstate(std::move(state)) {}

    // Public member to access the SQLSTATE
    std::string sqlstate;
};

/// @class row
/// @brief Represents a single row in a result set.
class row {
public:
    template<typename T>
    [[nodiscard]] T get_value(std::string_view col_name) const;

private:
    // Grant friendship to StmtHandle so it can construct row objects.
    friend class detail::StmtHandle;
    std::unordered_map<std::string, std::any, util::string_hash, util::string_equal> m_data;
};

/// @class resultset
/// @brief Represents a collection of rows returned from a query.
class resultset {
public:
    [[nodiscard]] std::vector<row>::const_iterator begin() const { return m_rows.begin(); }
    [[nodiscard]] std::vector<row>::const_iterator end() const { return m_rows.end(); }
    [[nodiscard]] bool empty() const { return m_rows.empty(); }
    [[nodiscard]] size_t size() const { return m_rows.size(); }
    [[nodiscard]] const row& at(size_t index) const { return m_rows.at(index); }

private:
    friend class detail::StmtHandle;
    std::vector<row> m_rows;
};


/**
 * @brief Executes a SQL query that returns a single column containing a JSON string.
 */
template<typename... Args>
[[nodiscard]] std::optional<std::string> get(std::string_view db_key, std::string_view sql_query, Args&&... args);

/**
 * @brief Executes a general SQL query and returns a structured result set.
 */
template<typename... Args>
[[nodiscard]] resultset query(std::string_view db_key, std::string_view sql_query, Args&&... args);

/**
 * @brief Executes a SQL statement that does not return a result set (e.g., INSERT, UPDATE, DELETE).
 */
template<typename... Args>
void exec(std::string_view db_key, std::string_view sql_query, Args&&... args);


// --- Internal Implementation Details ---
namespace detail {

// --- RAII Handle Wrappers ---
template<SQLSMALLINT HandleType>
class ODBCHandle {
public:
    ODBCHandle() = default;
    ~ODBCHandle() {
        if (m_handle != SQL_NULL_HANDLE) {
            SQLFreeHandle(HandleType, m_handle);
        }
    }
    ODBCHandle(const ODBCHandle&) = delete;
    ODBCHandle& operator=(const ODBCHandle&) = delete;
    ODBCHandle(ODBCHandle&& other) noexcept : m_handle(other.m_handle) {
        other.m_handle = SQL_NULL_HANDLE;
    }
    ODBCHandle& operator=(ODBCHandle&& other) noexcept {
        if (this != &other) {
            if (m_handle != SQL_NULL_HANDLE) {
                SQLFreeHandle(HandleType, m_handle);
            }
            m_handle = other.m_handle;
            other.m_handle = SQL_NULL_HANDLE;
        }
        return *this;
    }

    SQLHANDLE get() const { return m_handle; }
    SQLHANDLE* get_ptr() { return &m_handle; }

private:
    // Changed from protected to private to improve encapsulation.
    // Derived classes use the public get() and get_ptr() accessors.
    SQLHANDLE m_handle = SQL_NULL_HANDLE;
};

class EnvHandle : public ODBCHandle<SQL_HANDLE_ENV> {
public:
    EnvHandle();
};

class DbcHandle : public ODBCHandle<SQL_HANDLE_DBC> {
public:
    explicit DbcHandle(const EnvHandle& env);
};

class StmtHandle : public ODBCHandle<SQL_HANDLE_STMT> {
public:
    explicit StmtHandle(const DbcHandle& dbc);
    [[nodiscard]] resultset fetch_all() const;
private:
    // Helper functions for fetch_all, made static members to access row's private data
    // via the friend declaration in the row class.
    static std::vector<std::string> get_column_names(SQLHSTMT stmt_handle, SQLSMALLINT num_cols);
    static row fetch_single_row(SQLHSTMT stmt_handle, SQLSMALLINT num_cols, const std::vector<std::string>& col_names);
};

// --- Shared Environment Handle to avoid data race in multithreading mode ---
class SharedEnvHandle {
public:
    // The get() method now simply returns a reference to the inline member.
    static EnvHandle& get() {
        return s_env_handle;
    }

private:
    // By declaring the static member 'inline', you can define it directly
    // in the header. The C++ linker guarantees there will be only one
    // instance of s_env_handle across your entire program.
    static inline EnvHandle s_env_handle;
};


// --- Error Handling ---
void check_odbc_error(SQLRETURN retcode, SQLHANDLE handle, SQLSMALLINT handle_type, std::string_view context);

// --- Fetch Helpers ---
// Marked inline to prevent "multiple definition" linker errors.
[[nodiscard]] inline std::optional<std::string> fetch_json_result(StmtHandle& stmt);


// --- Connection Class ---
class Connection {
public:
    explicit Connection(std::string_view conn_str);
    DbcHandle& get_dbc() { return m_dbc; }
    StmtHandle& get_or_create_statement(std::string_view sql_query);

private:
    DbcHandle m_dbc;
    std::unordered_map<std::string, std::unique_ptr<StmtHandle>, util::string_hash, util::string_equal> m_statement_cache;
    std::mutex m_creation_mutex;
};

// --- Thread-Local Connection Manager ---
class ConnectionManager {
public:
    static Connection& get_connection(std::string_view db_key);
    static void invalidate_connection(std::string_view db_key);
private:
    static inline thread_local std::unordered_map<std::string, std::unique_ptr<Connection>, util::string_hash, util::string_equal> m_connections;
};

} // namespace detail
} // namespace sql

// Template implementation must be in the header
#include "sql.tpp"

#endif // SQL_HPP

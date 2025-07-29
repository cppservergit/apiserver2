#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <iostream>
#include <string_view>
#include <format>
#include <syncstream>
#include <thread>
#include <string>

#ifdef USE_STACKTRACE
#include <stacktrace>
#endif

namespace util::log {

// --- Thread-Local Request ID for Traceability ---

// Each thread will have its own copy of this variable.
// It's a string_view for efficiency, pointing to a string whose lifetime is
// managed by the server's dispatch logic.
inline thread_local std::string_view g_request_id;

/**
 * @class request_id_scope
 * @brief A RAII helper to set and clear the thread-local request ID.
 *
 * Create an instance of this on the stack at the beginning of a request's
 * lifecycle. When it goes out of scope, the request ID will be cleared.
 */
class request_id_scope {
public:
    explicit request_id_scope(std::string_view id) noexcept {
        g_request_id = id;
    }
    ~request_id_scope() {
        g_request_id = {}; // Clear the ID
    }
    // Non-copyable and non-movable
    request_id_scope(const request_id_scope&) = delete;
    request_id_scope& operator=(const request_id_scope&) = delete;
    request_id_scope(request_id_scope&&) = delete;
    request_id_scope& operator=(request_id_scope&&) = delete;
};


// --- Compile-time configuration for debug logging ---
#ifdef ENABLE_DEBUG_LOGS
constexpr bool debug_logging_enabled = true;
#else
constexpr bool debug_logging_enabled = false;
#endif


// Defines the severity level of a log message.
enum class Level {
    Debug,
    Info,
    Warning,
    Error,
    Critical
};

namespace detail {
    inline void vprint(
        const Level level,
        const std::string_view fmt,
        std::format_args args) 
    {
        // The log prefix now includes the request ID if it has been set.
        const auto log_prefix = std::format(
            "[{:^8}] [Thread: {}] [{}] ",
            level == Level::Debug ? "DEBUG" : (level == Level::Info ? "INFO" : (level == Level::Warning ? "WARN" : (level == Level::Error ? "ERROR" : "CRITICAL"))),
            std::this_thread::get_id(),
            g_request_id.empty() ? "--------" : g_request_id
        );
        std::osyncstream synced_out((level == Level::Error || level == Level::Critical) ? std::cerr : std::cout);
        synced_out << log_prefix;
        synced_out << std::vformat(fmt, args);
        synced_out << '\n';
        #ifdef USE_STACKTRACE
        if (level == Level::Error || level == Level::Critical) {
            synced_out << "--- Stack Trace ---\n" << std::stacktrace::current() << "-------------------\n";
        }
        #endif
    }
} // namespace detail

// --- Public-facing convenience functions ---

template<typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    if constexpr (debug_logging_enabled) {
        detail::vprint(Level::Debug, fmt.get(), std::make_format_args(args...));
    }
}

template<typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    detail::vprint(Level::Info, fmt.get(), std::make_format_args(args...));
}

template<typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    detail::vprint(Level::Warning, fmt.get(), std::make_format_args(args...));
}

template<typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    detail::vprint(Level::Error, fmt.get(), std::make_format_args(args...));
}

template<typename... Args>
void critical(std::format_string<Args...> fmt, Args&&... args) {
    detail::vprint(Level::Critical, fmt.get(), std::make_format_args(args...));
}
} // namespace util::log
#endif // LOGGER_HPP

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

// --- Compile-time configuration for logging ---
#ifdef ENABLE_DEBUG_LOGS
constexpr bool debug_logging_enabled = true;
#else
constexpr bool debug_logging_enabled = false;
#endif

// FIX: Add compile-time flag for performance logging
#ifdef ENABLE_PERF_LOGS
constexpr bool perf_logging_enabled = true;
#else
constexpr bool perf_logging_enabled = false;
#endif


// Defines the severity level of a log message.
enum class Level {
    Debug,
    Info,
    Warning,
    Error,
    Critical,
    Perf // FIX: Add new Perf level
};

namespace detail {
    /* NOSONAR */ inline thread_local std::string_view g_request_id;
    inline void vprint(
        const Level level,
        const std::string_view fmt,
        std::format_args args) 
    {
        std::string_view level_str;
        using enum Level;
        switch (level) {
            case Debug:   level_str = "DEBUG";   break;
            case Info:    level_str = "INFO";    break;
            case Warning: level_str = "WARN";    break;
            case Error:   level_str = "ERROR";   break;
            case Critical:level_str = "CRITICAL";break;
            // FIX: Handle new Perf level
            case Perf:    level_str = "PERF";    break; 
            default:      level_str = "UNKNOWN"; break;
        }
        const auto log_prefix = std::format(
            "[{:^8}] [Thread: {}] [{}] ",
            level_str,
            std::this_thread::get_id(),
            g_request_id.empty() ? "--------" : g_request_id
        );
        std::osyncstream synced_out((level == Error || level == Critical || level == Warning) ? std::cerr : std::cout);
        synced_out << log_prefix;
        synced_out << std::vformat(fmt, args);
        synced_out << '\n';
        #ifdef USE_STACKTRACE
        if (level == Error || level == Critical) {
            synced_out << "--- Stack Trace ---\n" << std::stacktrace::current() << "-------------------\n";
        }
        #endif
    }
} // namespace detail

/**
 * @class request_id_scope
 * @brief A RAII helper to set and clear the thread-local request ID.
 */
class request_id_scope {
public:
    explicit request_id_scope(std::string_view id) noexcept {
        detail::g_request_id = id;
    }
    ~request_id_scope() noexcept {
        detail::g_request_id = {}; // Clear the ID
    }
    // Non-copyable and non-movable
    request_id_scope(const request_id_scope&) = delete;
    request_id_scope& operator=(const request_id_scope&) = delete;
    request_id_scope(request_id_scope&&) = delete;
    request_id_scope& operator=(request_id_scope&&) = delete;
};


// --- Public-facing convenience functions ---

template<typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    if constexpr (debug_logging_enabled) {
        detail::vprint(Level::Debug, fmt.get(), std::make_format_args(args...));
    }
}

// FIX: Add new perf() log function
template<typename... Args>
void perf(std::format_string<Args...> fmt, Args&&... args) {
    if constexpr (perf_logging_enabled) {
        detail::vprint(Level::Perf, fmt.get(), std::make_format_args(args...));
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

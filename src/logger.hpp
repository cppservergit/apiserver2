#ifndef LOGGER_HPP
#define LOGGER_HPP

// C++23 Modernization: Use <print> instead of <iostream>/<syncstream>
// <print> provides atomic writes to C streams and is more efficient.
#include <print>
#include <string_view>
#include <format>
#include <thread>
#include <string>
#include <cstdio> // For stdout, stderr

#ifdef USE_STACKTRACE
#include <stacktrace>
#endif

// --- Configuration ---

// Compile-time flag for JSON formatting
// usage: g++ -DLOG_USE_JSON ...
#ifdef LOG_USE_JSON
constexpr bool use_json_format = true;
#else
constexpr bool use_json_format = false;
#endif

// Compile-time configuration for logging
#ifdef ENABLE_DEBUG_LOGS
constexpr bool debug_logging_enabled = true;
#else
constexpr bool debug_logging_enabled = false;
#endif

#ifdef ENABLE_PERF_LOGS
constexpr bool perf_logging_enabled = true;
#else
constexpr bool perf_logging_enabled = false;
#endif

namespace util::log {

enum class Level {
    Debug,
    Info,
    Warning,
    Error,
    Critical,
    Perf
};

namespace detail {
    // NOSONAR: Thread-local global is required for context-aware logging without passing context everywhere.
    /* NOSONAR */ inline thread_local std::string_view g_request_id;

    // Helper to escape JSON strings
    inline std::string json_escape(std::string_view s) {
        constexpr size_t RESERVE_PADDING = 10; // Avoid magic number for SonarCloud
        std::string res;
        res.reserve(s.size() + RESERVE_PADDING);
        
        for (char c : s) {
            switch (c) {
                case '"': res += R"(\")"; break;
                case '\\': res += R"(\\)"; break;
                case '\b': res += R"(\b)"; break;
                case '\f': res += R"(\f)"; break;
                case '\n': res += R"(\n)"; break;
                case '\r': res += R"(\r)"; break;
                case '\t': res += R"(\t)"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        res += std::format("\\u{:04x}", static_cast<unsigned char>(c));
                    } else {
                        res += c;
                    }
            }
        }
        return res;
    }

    inline void vprint(
        const Level level,
        std::string_view fmt_str, // Pass string_view by value (S3656)
        std::format_args args) 
    {
        // 1. Select Stream (Info -> stdout, others -> stderr)
        std::FILE* output_stream = (level == Level::Info) ? stdout : stderr;

        // 2. Determine Level String
        std::string_view level_str;
        using enum Level;
        switch (level) {
            case Debug:   level_str = "DEBUG";   break;
            case Info:    level_str = "INFO";    break;
            case Warning: level_str = "WARN";    break;
            case Error:   level_str = "ERROR";   break;
            case Critical:level_str = "CRITICAL";break;
            case Perf:    level_str = "PERF";    break;
            default:      level_str = "UNKNOWN"; break;
        }

        // 3. Prepare Data
        // Handle Request ID (using view, no allocation)
        std::string_view request_id = g_request_id.empty() ? "--------" : g_request_id;

        // Format the user message
        std::string message = std::vformat(fmt_str, args);

        // 4. Print
        if constexpr (use_json_format) {
            // JSON Format
            // Optimization: Assuming request_id is a valid UUID (safe chars), we skip escaping.
            // SonarCloud: Using Raw String Literal to avoid escaped quotes complexity.
            // Note: {{ and }} are escaped braces for std::format/std::print
            std::string json_msg = json_escape(message);
            
            std::println(output_stream, 
                R"({{"level": "{}", "thread": "{}", "req_id": "{}", "msg": "{}"}})",
                level_str,
                std::this_thread::get_id(),
                request_id,
                json_msg
            );
        } else {
            // Plain Text Format
            std::println(output_stream, 
                "[{:^8}] [Thread: {}] [{}] {}",
                level_str,
                std::this_thread::get_id(),
                request_id,
                message
            );
        }

        // 5. Stacktrace (Optional)
        #ifdef USE_STACKTRACE
        if (level == Error || level == Critical) {
             std::println(output_stream, "--- Stack Trace ---\n{}\n-------------------", 
                std::to_string(std::stacktrace::current()));
        }
        #endif
    }
} // namespace detail

/**
 * @class request_id_scope
 * @brief A RAII helper to set and clear the thread-local request ID.
 * @note The string provided to the constructor MUST outlive this scope.
 */
class request_id_scope {
public:
    explicit request_id_scope(std::string_view id) noexcept {
        detail::g_request_id = id;
    }
    ~request_id_scope() noexcept {
        detail::g_request_id = {}; // Clear the ID view
    }
    // Non-copyable and non-movable
    request_id_scope(const request_id_scope&) = delete;
    request_id_scope& operator=(const request_id_scope&) = delete;
    request_id_scope(request_id_scope&&) = delete;
    request_id_scope& operator=(request_id_scope&&) = delete;
};

// --- Public API ---

template<typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    if constexpr (debug_logging_enabled) {
        detail::vprint(Level::Debug, fmt.get(), std::make_format_args(args...));
    }
}

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
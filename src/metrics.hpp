/**
 * @file metrics.hpp
 * @brief Application metrics collection and exposition module.
 *
 * This file defines the `metrics` class, which handles the collection of runtime
 * statistics such as request counts, processing times, connection counts, and
 * resource usage. It supports exporting these metrics in both JSON and Prometheus formats.
 */

#ifndef METRICS_HPP
#define METRICS_HPP

#include "util.hpp"
#include "thread_pool.hpp"
#include "logger.hpp"
#include "env.hpp"

#include <string>
#include <chrono>
#include <atomic>
#include <vector>
#include <memory>
#include <numeric>
#include <format>
#include <mutex>

/**
 * @class metrics
 * @brief Centralized metrics collector for the application.
 *
 * The metrics class aggregates various runtime statistics in a thread-safe manner using atomic operations.
 * It tracks:
 * - Total requests processed
 * - Total processing time
 * - Active TCP connections
 * - Active worker threads
 * - Pending tasks in registered thread pools
 * - System memory usage
 *
 * It provides methods to export this data for monitoring purposes.
 */
class metrics {
public:
    /**
     * @brief Constructs a new metrics object.
     * * Initializes static application info (Pod name, start time, total RAM) and
     * formats the start timestamp based on the environment configuration.
     *
     * @param pool_size The total capacity of the thread pool (used for capacity reporting).
     */
    explicit metrics(int pool_size)
        : m_pod_name(util::get_pod_name()),
          m_start_time(std::chrono::system_clock::now()),
          m_pool_size(pool_size),
          m_total_ram_kb(util::get_total_memory()),
          m_start_date_str(format_timestamp())
    {}

    /**
     * @brief Increments the count of currently active connections.
     * @note Uses std::memory_order_relaxed for minimal overhead.
     */
    void increment_connections() noexcept { m_connections.fetch_add(1, std::memory_order_relaxed); }

    /**
     * @brief Decrements the count of currently active connections.
     * @note Uses std::memory_order_relaxed for minimal overhead.
     */
    void decrement_connections() noexcept { m_connections.fetch_sub(1, std::memory_order_relaxed); }

    /**
     * @brief Increments the count of currently active worker threads.
     * @note Uses std::memory_order_relaxed for minimal overhead.
     */
    void increment_active_threads() noexcept { m_active_threads.fetch_add(1, std::memory_order_relaxed); }

    /**
     * @brief Decrements the count of currently active worker threads.
     * @note Uses std::memory_order_relaxed for minimal overhead.
     */
    void decrement_active_threads() noexcept { m_active_threads.fetch_sub(1, std::memory_order_relaxed); }
    
    /**
     * @brief Records the duration of a processed request.
     * * Updates both the total request count and the total accumulated processing time.
     *
     * @param duration The time taken to process the request in microseconds.
     */
    void record_request_time(std::chrono::microseconds duration) noexcept {
        m_total_requests.fetch_add(1, std::memory_order_relaxed);
        m_total_processing_time_us.fetch_add(duration.count(), std::memory_order_relaxed);
    }

    /**
     * @brief Registers a thread pool for monitoring.
     * * The metrics collector will poll registered pools for pending task counts
     * when generating reports (snapshotting).
     *
     * @param pool Pointer to the thread pool instance.
     */
    void register_thread_pool(const thread_pool* pool) {
        std::scoped_lock lock(m_pools_mutex);
        m_thread_pools.push_back(pool);
    }

    /**
     * @brief Generates a JSON representation of the current metrics.
     * * Captures a consistent snapshot of the metrics and formats them into a JSON string.
     * * @return std::string A JSON formatted string containing current statistics.
     */
    [[nodiscard]] std::string to_json() const {
        const auto s = collect_metrics_snapshot();

        constexpr auto json_tpl = R"({{
            "pod_name": "{}",
            "start_time": "{}",
            "total_requests": {},
            "average_processing_time_seconds": {:.6f},
            "current_connections": {},
            "current_active_threads": {},
            "pending_tasks": {},
            "thread_pool_size": {},
            "total_ram_kb": {},
            "memory_usage_kb": {},
            "memory_usage_percentage": {:.2f}
            }})";
        
        return std::format(
            json_tpl,
            s.pod_name, s.start_time, s.total_reqs, s.avg_time_s, 
            s.current_connections, s.active_threads, s.pending_tasks, 
            s.pool_size, s.total_ram_kb, s.memory_usage_kb, s.memory_usage_pct
        );
    }

    /**
     * @brief Generates a Prometheus-compatible metrics exposition.
     * * Formats the metrics using standard Prometheus text format, including
     * HELP and TYPE headers. All metrics are tagged with the pod name.
     *
     * @return std::string A string compatible with the Prometheus scrape endpoint.
     */
    [[nodiscard]] std::string to_prometheus() const {
        const auto s = collect_metrics_snapshot();

        constexpr auto prom_tpl = 
            "# HELP app_info Static information about the application\n"
            "# TYPE app_info gauge\n"
            "app_info{{pod=\"{}\", start_time=\"{}\"}} 1\n\n"
            "# HELP http_requests_total Total number of HTTP requests processed\n"
            "# TYPE http_requests_total counter\n"
            "http_requests_total{{pod=\"{}\"}} {}\n\n"
            "# HELP http_request_duration_seconds_sum Total time spent processing requests in seconds\n"
            "# TYPE http_request_duration_seconds_sum counter\n"
            "http_request_duration_seconds_sum{{pod=\"{}\"}} {:.6f}\n\n"
            "# HELP http_request_avg_duration_seconds Average processing time\n"
            "# TYPE http_request_avg_duration_seconds gauge\n"
            "http_request_avg_duration_seconds{{pod=\"{}\"}} {:.6f}\n\n"
            "# HELP tcp_connections_current Current number of active TCP connections\n"
            "# TYPE tcp_connections_current gauge\n"
            "tcp_connections_current{{pod=\"{}\"}} {}\n\n"
            "# HELP thread_pool_active_threads Number of threads currently processing a task\n"
            "# TYPE thread_pool_active_threads gauge\n"
            "thread_pool_active_threads{{pod=\"{}\"}} {}\n\n"
            "# HELP thread_pool_pending_tasks Number of tasks waiting in the queue\n"
            "# TYPE thread_pool_pending_tasks gauge\n"
            "thread_pool_pending_tasks{{pod=\"{}\"}} {}\n\n"
            "# HELP thread_pool_capacity Total number of threads in the pool\n"
            "# TYPE thread_pool_capacity gauge\n"
            "thread_pool_capacity{{pod=\"{}\"}} {}\n\n"
            "# HELP system_memory_usage_kilobytes Current resident memory usage\n"
            "# TYPE system_memory_usage_kilobytes gauge\n"
            "system_memory_usage_kilobytes{{pod=\"{}\"}} {}\n\n"
            "# HELP system_memory_limit_kilobytes Total available RAM\n"
            "# TYPE system_memory_limit_kilobytes gauge\n"
            "system_memory_limit_kilobytes{{pod=\"{}\"}} {}\n\n"
            "# HELP system_memory_usage_percent Percentage of RAM used\n"
            "# TYPE system_memory_usage_percent gauge\n"
            "system_memory_usage_percent{{pod=\"{}\"}} {:.2f}\n";

        return std::format(
            prom_tpl,
            s.pod_name, s.start_time,
            s.pod_name, s.total_reqs,
            s.pod_name, s.total_time_s,
            s.pod_name, s.avg_time_s,
            s.pod_name, s.current_connections,
            s.pod_name, s.active_threads,
            s.pod_name, s.pending_tasks,
            s.pod_name, s.pool_size,
            s.pod_name, s.memory_usage_kb,
            s.pod_name, s.total_ram_kb,
            s.pod_name, s.memory_usage_pct
        );
    }

    /**
     * @brief Retrieves the pod name.
     * @return std::string The name of the current pod/instance.
     */
    [[nodiscard]] std::string get_pod_name() const {
        return m_pod_name;
    }

private:
    const std::string m_pod_name;
    const std::chrono::system_clock::time_point m_start_time;
    const int m_pool_size;
    const size_t m_total_ram_kb;
    const std::string m_start_date_str;

    std::atomic<long long> m_total_requests{0};
    std::atomic<long long> m_total_processing_time_us{0};
    std::atomic<int> m_connections{0};
    std::atomic<int> m_active_threads{0};

    mutable std::mutex m_pools_mutex;
    std::vector<const thread_pool*> m_thread_pools;

    /**
     * @brief Structure to hold a point-in-time snapshot of all metrics.
     * * Used to separate data collection logic from data formatting logic.
     */
    struct MetricsSnapshot {
        std::string pod_name;
        std::string start_time;
        long long total_reqs;
        double total_time_s; 
        double avg_time_s;
        int current_connections;
        int active_threads;
        size_t pending_tasks;
        size_t pool_size;
        size_t memory_usage_kb;
        size_t total_ram_kb;
        double memory_usage_pct;
    };

    /**
     * @brief Collects all current metric values into a snapshot structure.
     * * Performs atomic loads, memory usage checks, and arithmetic calculations
     * (averages, percentages) to prepare data for export. This isolates
     * calculation complexity from the view/formatting logic.
     * * @return MetricsSnapshot A struct containing calculated values.
     */
    MetricsSnapshot collect_metrics_snapshot() const {
        MetricsSnapshot s;
        
        // 1. Atomic Loads
        s.total_reqs = m_total_requests.load(std::memory_order_relaxed);
        long long total_time_us = m_total_processing_time_us.load(std::memory_order_relaxed);
        s.current_connections = m_connections.load(std::memory_order_relaxed);
        s.active_threads = m_active_threads.load(std::memory_order_relaxed);
        s.pool_size = m_pool_size;
        
        // 2. Static/Member Data
        s.pod_name = m_pod_name;
        s.start_time = m_start_date_str;
        s.total_ram_kb = m_total_ram_kb;

        // 3. System Calls
        s.memory_usage_kb = util::get_memory_usage();

        // 4. Calculations
        s.total_time_s = static_cast<double>(total_time_us) / 1'000'000.0;
        
        s.avg_time_s = (s.total_reqs > 0) 
            ? (s.total_time_s / static_cast<double>(s.total_reqs)) 
            : 0.0;
            
        s.memory_usage_pct = (s.total_ram_kb > 0) 
            ? ((static_cast<double>(s.memory_usage_kb) / static_cast<double>(s.total_ram_kb)) * 100.0) 
            : 0.0;

        // 5. Locking Logic
        s.pending_tasks = 0;
        {
            std::scoped_lock lock(m_pools_mutex);
            for (const auto* pool : m_thread_pools) {
                if (pool) {
                    s.pending_tasks += pool->get_total_pending_tasks();
                }
            }
        }
        
        return s;
    }

    /**
     * @brief Formats the start time respecting the TZ environment variable via env::get.
     * * Handles C++20/23 timezone database lookups gracefully.
     * Priority:
     * 1. TZ Environment Variable (e.g., "America/Caracas")
     * 2. System Timezone (/etc/localtime)
     * 3. UTC Fallback
     * * @return std::string Formatted timestamp string (ISO 8601-like).
     */
    [[nodiscard]] std::string format_timestamp() const {
        const std::chrono::time_zone* tz = nullptr;

        // 1. Try to load from TZ environment variable using env::get
        // Returns empty string if missing (the fallback logic is inside env.hpp wrapper)
        //
        if (const std::string tz_env = env::get<std::string>("TZ", ""); !tz_env.empty()) {
            try {
                tz = std::chrono::locate_zone(tz_env);
            } catch (const std::runtime_error& e) {
               util::log::warn("metrics", "Failed to locate timezone from TZ env {}: Falling back to system timezone: {}.", tz_env, e.what());
            }
        }

        // 2. Fallback to System Timezone
        if (!tz) {
            try {
                tz = std::chrono::current_zone();
            } catch (const std::runtime_error& e) {
                util::log::warn("metrics", "Failed to get system timezone: Falling back to UTC: {}.", e.what());
            }
        }

        // 3. Format
        if (tz) {
            return std::format("{:%FT%T}", 
                std::chrono::zoned_time{tz, 
                std::chrono::floor<std::chrono::seconds>(m_start_time)});
        }

        // 4. Ultimate Fallback: Raw system time (effectively UTC)
        return std::format("{:%FT%T}", std::chrono::floor<std::chrono::seconds>(m_start_time));
    }    
};

#endif // METRICS_HPP
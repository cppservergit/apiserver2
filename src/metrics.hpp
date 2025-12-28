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

using dispatch_task = std::function<void()>;

class metrics {
public:
    explicit metrics(int pool_size)
        : m_pod_name(util::get_pod_name()),
          m_start_time(std::chrono::system_clock::now()),
          m_pool_size(pool_size),
          m_total_ram_kb(util::get_total_memory()),
          m_start_date_str(format_timestamp())
    {}

    void increment_connections() noexcept { m_connections.fetch_add(1, std::memory_order_relaxed); }
    void decrement_connections() noexcept { m_connections.fetch_sub(1, std::memory_order_relaxed); }
    void increment_active_threads() noexcept { m_active_threads.fetch_add(1, std::memory_order_relaxed); }
    void decrement_active_threads() noexcept { m_active_threads.fetch_sub(1, std::memory_order_relaxed); }
    
    void record_request_time(std::chrono::microseconds duration) noexcept {
        m_total_requests.fetch_add(1, std::memory_order_relaxed);
        m_total_processing_time_us.fetch_add(duration.count(), std::memory_order_relaxed);
    }

    void register_thread_pool(const thread_pool* pool) {
        std::scoped_lock lock(m_pools_mutex);
        m_thread_pools.push_back(pool);
    }

    [[nodiscard]] std::string to_json() const {
        const long long total_reqs = m_total_requests.load(std::memory_order_relaxed);
        const long long total_time_us = m_total_processing_time_us.load(std::memory_order_relaxed);
        const int current_connections = m_connections.load(std::memory_order_relaxed);
        const int current_active_threads = m_active_threads.load(std::memory_order_relaxed);
        
        const size_t memory_usage_kb = util::get_memory_usage();
        double avg_time_s = (total_reqs > 0) ? (static_cast<double>(total_time_us) / static_cast<double>(total_reqs) / 1'000'000.0) : 0.0;
        double memory_usage_percentage = (m_total_ram_kb > 0) ? ((static_cast<double>(memory_usage_kb) / static_cast<double>(m_total_ram_kb)) * 100.0) : 0.0;

        size_t pending_tasks = 0;
        {
            std::scoped_lock lock(m_pools_mutex);
            for (const auto* pool : m_thread_pools) {
                if (pool) {
                    pending_tasks += pool->get_total_pending_tasks();
                }
            }
        }

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
            m_pod_name, m_start_date_str, total_reqs, avg_time_s, current_connections,
            current_active_threads, pending_tasks, m_pool_size,
            m_total_ram_kb, memory_usage_kb, memory_usage_percentage
        );
    }

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
     * @brief Formats the start time respecting the TZ environment variable via env::get.
     * * Handles C++20/23 timezone database lookups gracefully.
     * Priority:
     * 1. TZ Environment Variable (e.g., "America/Caracas")
     * 2. System Timezone (/etc/localtime)
     * 3. UTC Fallback
     */
    [[nodiscard]] std::string format_timestamp() const {
        const std::chrono::time_zone* tz = nullptr;

        // 1. Try to load from TZ environment variable using env::get
        // Returns empty string if missing (the fallback logic is inside env.hpp wrapper)
        //
        const std::string tz_env = env::get<std::string>("TZ", "");

        if (!tz_env.empty()) {
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

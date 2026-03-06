#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include "shared_queue.hpp"
#include "logger.hpp"
#include <vector>
#include <thread>
#include <functional>
#include <atomic>
#include <stdexcept>

using dispatch_task = std::function<void()>;

class thread_pool {
public:
    /**
     * @brief Constructs the thread pool with a single global MPMC queue.
     * @param num_threads The number of worker threads to spawn.
     * @param queue_capacity The maximum number of tasks in the global queue. 0 = unbounded.
     */
    explicit thread_pool(size_t num_threads, size_t queue_capacity = 0)
        : m_num_threads(num_threads), 
          m_global_queue(queue_capacity) { // Initialize the single shared queue directly
    }

    // Rule of 5: Concurrency managers must not be copyable or movable
    thread_pool(const thread_pool&) = delete;
    thread_pool& operator=(const thread_pool&) = delete;
    thread_pool(thread_pool&&) = delete;
    thread_pool& operator=(thread_pool&&) = delete;

    ~thread_pool() noexcept {
        stop();
    }

    void start() {
        m_threads.reserve(m_num_threads);
        for (size_t i = 0; i < m_num_threads; ++i) {
            // Spawn C++20 jthreads which automatically join on destruction
            m_threads.emplace_back([this, i] { worker_loop(i); });
        }
        util::log::info("Thread pool started with {} workers.", m_num_threads);
    }

    void stop() {
        // Prevent multiple stop calls
        if (m_stopped.exchange(true)) {
            return;
        }
        
        m_global_queue.stop();
        
        // Clearing the vector destructs the jthreads, which safely blocks until they finish
        m_threads.clear();
        try {
            util::log::info("Thread pool stopped.");
        } catch (/* NOSONAR */ const std::exception& e) {
        }
    }

    /**
     * @brief Pushes a task to the single global queue.
     * @throws queue_full_error if the queue has reached its maximum capacity.
     */
    void push_task(dispatch_task task) {
        // FIX 1: Increment unfinished tasks *before* pushing to prevent shutdown race conditions
        m_unfinished_tasks.fetch_add(1, std::memory_order_release);
        try {
            m_global_queue.push(std::move(task));
        } catch (...) {
            m_unfinished_tasks.fetch_sub(1, std::memory_order_relaxed);
            throw;
        }
    }

    /**
     * @brief Returns the number of tasks currently waiting in the queue.
     * Useful for metrics reporting.
     */
    [[nodiscard]] size_t get_total_pending_tasks() const {
        return m_global_queue.size();
    }

    /**
     * @brief Returns the number of tasks both waiting in the queue AND currently executing.
     * Critical for safe thread pool shutdown without dropping requests.
     */
    [[nodiscard]] size_t get_unfinished_tasks() const {
        return m_unfinished_tasks.load(std::memory_order_acquire);
    }

private:
    void worker_loop(size_t worker_id) {
        util::log::debug("Worker thread {} started.", worker_id);

        while (true) {
            // Blocks efficiently until the single global queue has a task or is stopped
            auto task_opt = m_global_queue.wait_and_pop();

            if (!task_opt) {
                break; // Queue was stopped and is fully drained
            }
            
            try {
                if (*task_opt) {
                    (*task_opt)(); // Execute the API endpoint handler
                }
            } catch (/* NOSONAR */ const std::exception& e) {
                util::log::error("Exception caught in worker thread {}: {}", worker_id, e.what());
            }

            // FIX 1: Decrement only *after* the task is fully processed
            m_unfinished_tasks.fetch_sub(1, std::memory_order_release);
        }
        util::log::debug("Worker thread {} finished.", worker_id);
    }

    const size_t m_num_threads;
    std::atomic<bool> m_stopped{false};
    std::atomic<size_t> m_unfinished_tasks{0}; // Tracks tasks both queued AND executing
    
    shared_queue<dispatch_task> m_global_queue; 
    std::vector<std::jthread> m_threads;
};

#endif // THREAD_POOL_HPP
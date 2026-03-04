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
        try {
            stop();
        } catch (...) {
            /* NOSONAR - Intentionally ignore exceptions during destruction to prevent std::terminate */
        }
    }

    void start() {
        m_threads.reserve(m_num_threads);
        for (size_t i = 0; i < m_num_threads; ++i) {
            // Spawn C++20 jthreads which automatically join on destruction
            m_threads.emplace_back([this, i] { worker_loop(i); });
        }
    }

    void stop() {
        // Prevent multiple stop calls
        if (m_stopped.exchange(true)) {
            return;
        }
        
        m_global_queue.stop();
        
        // Clearing the vector destructs the jthreads, which safely blocks until they finish
        m_threads.clear();
        util::log::info("Thread pool stopped.");
    }

    /**
     * @brief Pushes a task to the single global queue.
     * @throws queue_full_error if the queue has reached its maximum capacity.
     */
    void push_task(dispatch_task task) {
        m_global_queue.push(std::move(task));
    }

    [[nodiscard]] size_t get_total_pending_tasks() const {
        return m_global_queue.size();
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
        }
        util::log::debug("Worker thread {} finished.", worker_id);
    }

    const size_t m_num_threads;
    std::atomic<bool> m_stopped{false};
    
    shared_queue<dispatch_task> m_global_queue; 
    std::vector<std::jthread> m_threads;
};

#endif // THREAD_POOL_HPP
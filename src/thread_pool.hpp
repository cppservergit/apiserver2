#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include "shared_queue.hpp"
#include "logger.hpp"
#include <vector>
#include <thread>
#include <functional>
#include <atomic>
#include <memory>
#include <numeric>

using dispatch_task = std::function<void()>;

class thread_pool {
public:
    explicit thread_pool(size_t num_threads)
        : m_num_threads(num_threads) {
        for (size_t i = 0; i < m_num_threads; ++i) {
            m_task_queues.push_back(std::make_shared<shared_queue<dispatch_task>>());
        }
    }

    ~thread_pool() noexcept {
        try {
            stop();
        } catch (const std::exception& e) {
            /* NOSONAR */
            // avoid program crash, nothing else to do here
        }
    }

    void start() {
        for (size_t i = 0; i < m_num_threads; ++i) {
            m_threads.emplace_back([this, i] { worker_loop(i); });
        }
        util::log::debug("Thread pool started with {} threads.", m_num_threads);
    }

    void stop() {
        if (m_stopped.exchange(true)) {
            return;
        }
        
        for(const auto& queue : m_task_queues) {
            queue->stop();
        }
        m_threads.clear();
        util::log::debug("Thread pool stopped.");
    }

    // The I/O thread calls this to dispatch a task in a round-robin fashion.
    void push_task(dispatch_task task) {
        size_t queue_index = m_next_queue.fetch_add(1, std::memory_order_relaxed) % m_num_threads;
        m_task_queues[queue_index]->push(std::move(task));
    }

    // Used by the metrics class to get a total count of all pending tasks.
    [[nodiscard]] size_t get_total_pending_tasks() const {
        size_t total_tasks = 0;
        for (const auto& queue : m_task_queues) {
            total_tasks += queue->size();
        }
        return total_tasks;
    }

private:
    // Each worker thread runs this loop, consuming from its own dedicated queue.
    void worker_loop(size_t queue_index) {
        util::log::debug("Worker thread {} started, consuming from queue {}.", std::this_thread::get_id(), queue_index);
        auto& my_queue = *m_task_queues[queue_index];

        while (true) {
            auto task_opt = my_queue.wait_and_pop();

            if (!task_opt) {
                break; // Queue was stopped and is empty
            }
            
            try {
                if (*task_opt) {
                    (*task_opt)(); // Execute the task
                }
            } catch (/* NOSONAR */ const std::exception& e) {
                util::log::error("Exception caught in worker thread: {}", e.what());
            }
        }
        util::log::debug("Worker thread {} finished.", std::this_thread::get_id());
    }

    const size_t m_num_threads;
    std::atomic<bool> m_stopped{false};
    std::atomic<size_t> m_next_queue{0};
    
    std::vector<std::shared_ptr<shared_queue<dispatch_task>>> m_task_queues;
    std::vector<std::jthread> m_threads;
};

#endif // THREAD_POOL_HPP

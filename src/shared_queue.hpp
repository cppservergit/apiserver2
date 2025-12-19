#ifndef SHARED_QUEUE_HPP
#define SHARED_QUEUE_HPP

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <vector>
#include <atomic>
#include <stdexcept>

// Specific exception for backpressure handling
class queue_full_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

template<typename T>
class shared_queue {
public:
    /**
     * @brief Constructor accepting a capacity limit.
     * @param capacity The maximum number of items allowed in the queue. 
     * 0 (default) implies unbounded.
     */
    explicit shared_queue(size_t capacity = 0) : m_capacity(capacity) {}

    /**
     * @brief Pushes a new item onto the queue and notifies a waiting thread.
     * @throws queue_full_error if the queue has reached its capacity.
     */
    void push(T item) {
        {
            std::scoped_lock lock(m_mutex);
            // Check capacity if limit is set (non-zero)
            if (m_capacity > 0 && m_queue.size() >= m_capacity) {
                throw queue_full_error("Queue is full");
            }
            m_queue.push(std::move(item));
        }
        m_cond.notify_one();
    }

    /**
     * @brief Waits for an item to be available and pops it from the queue.
     * @return An optional containing the item, or std::nullopt if the queue was stopped.
     */
    std::optional<T> wait_and_pop() {
        std::unique_lock lock(m_mutex);
        m_cond.wait(lock, [this] { return !m_queue.empty() || m_stopped; });
        
        if (m_stopped && m_queue.empty()) {
            return std::nullopt;
        }
        
        T item = std::move(m_queue.front());
        m_queue.pop();
        return item;
    }

    /**
     * @brief Moves all items from this queue into a target vector.
     */
    void drain_to(std::vector<T>& target) {
        std::scoped_lock lock(m_mutex);
        while (!m_queue.empty()) {
            target.push_back(std::move(m_queue.front()));
            m_queue.pop();
        }
    }

    /**
     * @brief Signals the queue to stop, waking up all waiting threads.
     */
    void stop() {
        {
            std::scoped_lock lock(m_mutex);
            m_stopped = true;
        }
        m_cond.notify_all();
    }

    /**
     * @brief Gets the current number of items in the queue.
     * @return The number of items.
     */
    [[nodiscard]] size_t size() const {
        std::scoped_lock lock(m_mutex);
        return m_queue.size();
    }

private:
    std::queue<T> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
    std::atomic<bool> m_stopped{false};
    
    // Capacity limit (0 = unbounded)
    size_t m_capacity; 
};

#endif // SHARED_QUEUE_HPP
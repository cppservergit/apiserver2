#ifndef SHARED_QUEUE_HPP
#define SHARED_QUEUE_HPP

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <vector>
#include <atomic>
#include <stdexcept>
#include <unistd.h> 

/**
 * @brief Specific exception for backpressure handling.
 */
class queue_full_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/**
 * @brief Thread-safe queue with optional eventfd signaling.
 *
 * @tparam T The type of items in the queue.
 * @tparam UseEventFD If true, uses eventfd for signaling (suitable for epoll loops).
 * If false, uses condition variables (suitable for worker threads).
 */
template<typename T, bool UseEventFD = false>
class shared_queue {
public:
    /**
     * @brief Constructor accepting a capacity limit.
     */
    explicit shared_queue(size_t capacity = 0)
        : m_capacity(capacity) {}

    /**
     * @brief Links this queue to an eventfd for signaling.
     * Uses atomic store to prevent data races during initialization.
     */
    void set_event_fd(int fd) noexcept {
        if constexpr (UseEventFD) {
            m_event_fd.store(fd, std::memory_order_release);
        }
    }

    /**
     * @brief Pushes a new item onto the queue and notifies the consumer.
     */
    void push(T item) {
        {
            std::scoped_lock lock(m_mutex);
            if (m_capacity > 0 && m_queue.size() >= m_capacity) {
                throw queue_full_error("Queue is full");
            }
            m_queue.push(std::move(item));
        }

        if constexpr (UseEventFD) {
            int fd = m_event_fd.load(std::memory_order_acquire);
            if (fd != -1) {
                uint64_t u = 1;
                [[maybe_unused]] ssize_t s = write(fd, &u, sizeof(uint64_t));
            }
        } else {
            m_cond.notify_one();
        }
    }

    /**
     * @brief Waits for an item to be available and pops it from the queue.
     */
    std::optional<T> wait_and_pop() {
        std::unique_lock lock(m_mutex);
        m_cond.wait(lock, [this] { return !m_queue.empty() || m_stopped; });

        if (m_queue.empty()) {
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
     * @brief Signals the queue to stop, waking up all waiting consumers.
     */
    void stop() noexcept {
        {
            std::scoped_lock lock(m_mutex);
            m_stopped = true;
        }

        if constexpr (UseEventFD) {
            int fd = m_event_fd.load(std::memory_order_acquire);
            if (fd != -1) {
                uint64_t u = 1;
                [[maybe_unused]] ssize_t s = write(fd, &u, sizeof(uint64_t));
                return;
            }
        }

        m_cond.notify_all();
    }

    [[nodiscard]] size_t size() const {
        std::scoped_lock lock(m_mutex);
        return m_queue.size();
    }

private:
    std::queue<T> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
    
    // Use in-class initializers to satisfy SonarCloud rules for constant defaults
    std::atomic<bool> m_stopped{false};
    size_t m_capacity{0};
    std::atomic<int> m_event_fd{-1}; // Atomic to prevent TSAN data races
};

#endif // SHARED_QUEUE_HPP
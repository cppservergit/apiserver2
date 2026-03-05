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

class queue_full_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/**
 * @brief Thread-safe MPMC queue with optional eventfd signaling.
 *
 * @tparam T The type of items in the queue.
 * @tparam UseEventFD If true, uses eventfd for epoll signaling. Otherwise uses condition variables.
 */
template<typename T, bool UseEventFD = false>
class shared_queue {
public:
    explicit shared_queue(size_t capacity = 0)
        : m_capacity(capacity) {}

    // Rule of 5: Synchronization primitives must not be copied or moved
    shared_queue(const shared_queue&) = delete;
    shared_queue& operator=(const shared_queue&) = delete;
    shared_queue(shared_queue&&) = delete;
    shared_queue& operator=(shared_queue&&) = delete;
    ~shared_queue() = default;

    void set_event_fd(int fd) noexcept {
        if constexpr (UseEventFD) {
            m_event_fd.store(fd, std::memory_order_release);
        }
    }

    void push(T item) {
        {
            std::scoped_lock lock(m_mutex);
            if (m_capacity > 0 && m_queue.size() >= m_capacity) {
                throw queue_full_error("Queue is full");
            }
            m_queue.push(std::move(item));
        }

        if constexpr (UseEventFD) {
            notify_event_fd();
        } else {
            // Wakes up exactly ONE sleeping worker thread
            m_cond.notify_one();
        }
    }

    [[nodiscard]] std::optional<T> wait_and_pop() {
        std::unique_lock lock(m_mutex);
        m_cond.wait(lock, [this] { return !m_queue.empty() || m_stopped; });

        if (m_queue.empty()) {
            return std::nullopt; 
        }

        T item = std::move(m_queue.front());
        m_queue.pop();
        return item;
    }

    void drain_to(std::vector<T>& target) {
        std::scoped_lock lock(m_mutex);
        while (!m_queue.empty()) {
            target.push_back(std::move(m_queue.front()));
            m_queue.pop();
        }
    }

    void stop() noexcept {
        {
            std::scoped_lock lock(m_mutex);
            m_stopped = true;
        }

        if constexpr (UseEventFD) {
            notify_event_fd();
        }

        // Wakes up ALL sleeping worker threads so they can observe the stop flag and exit
        m_cond.notify_all();
    }

    [[nodiscard]] size_t size() const {
        std::scoped_lock lock(m_mutex);
        return m_queue.size();
    }

private:
    // Helper to reliably notify the eventfd, safely handling POSIX interruptions
    void notify_event_fd() noexcept {
        int fd = m_event_fd.load(std::memory_order_acquire);
        if (fd != -1) {
            uint64_t u = 1;
            while (true) {
                ssize_t s = write(fd, &u, sizeof(uint64_t));
                if (s == sizeof(uint64_t)) break; // Success
                
                // If interrupted by a system signal, retry immediately
                if (s == -1 && errno == EINTR) continue; 
                
                // Break on other fatal errors (EAGAIN is mathematically impossible 
                // here without EFD_SEMAPHORE unless 18 quintillion pushes occur unread).
                break;
            }
        }
    }

    std::queue<T> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
    
    std::atomic<bool> m_stopped{false};
    size_t m_capacity{0};
    std::atomic<int> m_event_fd{-1};
};

#endif // SHARED_QUEUE_HPP
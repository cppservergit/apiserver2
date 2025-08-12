#ifndef SOCKET_BUFFER_HPP
#define SOCKET_BUFFER_HPP

#include <stdexcept>
#include <string>
#include <vector>
#include <string_view>
#include <sys/types.h> // For ssize_t
#include <algorithm>   // For std::min
#include <span>        // For std::span

class socket_buffer_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class socket_buffer {
public:
    socket_buffer() = default;
    
    void update_pos(ssize_t n) {
        if (n <= 0) return;

        m_pos += static_cast<size_t>(n);

        if (m_pos * 4 > m_buffer.size() * 3) {
            if (m_buffer.size() >= k_max_size) {
                throw socket_buffer_error("Maximum buffer size reached.");
            }
            size_t new_size = m_buffer.size() + k_chunk_size;
            m_buffer.resize(std::min(new_size, k_max_size));
        }
    }
    
    [[nodiscard]] std::span<char> buffer() noexcept {
        return {m_buffer.data() + m_pos, available_size()};
    }

    [[nodiscard]] size_t available_size() const noexcept {
        return m_buffer.size() - m_pos;
    }

    [[nodiscard]] bool empty() const noexcept {
        return m_pos == 0;
    }

    [[nodiscard]] size_t buffer_size() const noexcept {
        return m_buffer.size();
    }

    [[nodiscard]] size_t size() const noexcept {
        return m_pos;
    }
    
    [[nodiscard]] std::string_view view() const noexcept {
        return {m_buffer.data(), m_pos};
    }

private:
    constexpr static size_t k_chunk_size{4096};
    constexpr static size_t k_max_size{4 * 1024 * 1024};
    
    std::vector<char> m_buffer = std::vector<char>(k_chunk_size, 0);
    size_t m_pos{0};
};

#endif // SOCKET_BUFFER_HPP
#ifndef SOCKET_BUFFER_HPP
#define SOCKET_BUFFER_HPP

#include <stdexcept>
#include <string>
#include <vector>
#include <string_view>
#include <sys/types.h> // For ssize_t
#include <algorithm>   // For std::min
#include <span>        // For std::span

class socket_buffer_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class socket_buffer {
public:
    socket_buffer() = default;
    
    void update_pos(ssize_t n) {
        if (n <= 0) return;

        m_pos += static_cast<size_t>(n);

        if (m_pos * 4 > m_buffer.size() * 3) {
            if (m_buffer.size() >= k_max_size) {
                throw socket_buffer_error("Maximum buffer size reached.");
            }
            size_t new_size = m_buffer.size() + k_chunk_size;
            m_buffer.resize(std::min(new_size, k_max_size));
        }
    }
    
    [[nodiscard]] std::span<char> buffer() noexcept {
        return {m_buffer.data() + m_pos, available_size()};
    }

    [[nodiscard]] size_t available_size() const noexcept {
        return m_buffer.size() - m_pos;
    }

    [[nodiscard]] bool empty() const noexcept {
        return m_pos == 0;
    }

    [[nodiscard]] size_t buffer_size() const noexcept {
        return m_buffer.size();
    }

    [[nodiscard]] size_t size() const noexcept {
        return m_pos;
    }
    
    [[nodiscard]] std::string_view view() const noexcept {
        return {m_buffer.data(), m_pos};
    }

private:
    constexpr static size_t k_chunk_size{4096};
    constexpr static size_t k_max_size{4 * 1024 * 1024};
    
    std::vector<char> m_buffer = std::vector<char>(k_chunk_size, 0);
    size_t m_pos{0};
};

#endif // SOCKET_BUFFER_HPP

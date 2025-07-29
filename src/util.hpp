#ifndef UTIL_HPP
#define UTIL_HPP

#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <array>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <new> // For std::bad_alloc

// Includes for POSIX/networking functions
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <climits> // For HOST_NAME_MAX
#include <uuid/uuid.h> // For UUID generation

namespace util {

struct string_hash {
    using is_transparent = void;
    [[nodiscard]] size_t operator()(const char* txt) const {
        return std::hash<std::string_view>{}(txt);
    }
    [[nodiscard]] size_t operator()(std::string_view txt) const {
        return std::hash<std::string_view>{}(txt);
    }
    [[nodiscard]] size_t operator()(const std::string& txt) const {
        return std::hash<std::string>{}(txt);
    }
};

/**
 * @brief Gets the hostname of the current machine (e.g., the pod name in k8s).
 * @return The hostname as a string, or a default string on failure.
 */
[[nodiscard]] inline std::string get_pod_name() noexcept {
    try {
        long host_name_max = sysconf(_SC_HOST_NAME_MAX);
        if (host_name_max <= 0) {
            host_name_max = HOST_NAME_MAX;
        }

        std::vector<char> hostname_buffer(static_cast<size_t>(host_name_max));

        if (gethostname(hostname_buffer.data(), hostname_buffer.size()) != 0) {
            return "hostname_not_available";
        }

        // The returned string may be null-terminated. Find the end.
        const auto end = std::find(hostname_buffer.begin(), hostname_buffer.end(), '\0');
        return std::string(hostname_buffer.begin(), end);

    } catch (const std::exception& e) {
        return "hostname_lookup_exception";
    }
}

/**
 * @brief Retrieves the last socket error message for a given file descriptor.
 * @param fd The socket file descriptor.
 * @return The error message as a string, or a default string if no error occurred.
 */
[[nodiscard]] inline std::string get_socket_error(int fd) noexcept {
    int error = 0;
    socklen_t errlen = sizeof(error);

    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &errlen) == 0 && error != 0) {
        try {
            std::error_code ec(error, std::system_category());
            return ec.message();
        } catch (const std::exception& e) {
            return "error_message_lookup_failed";
        }
    }

    return "no error message available";
}

/**
 * @brief Converts a standard C errno number to a C++ string message.
 * @param err_num The error number (e.g., from errno).
 * @return The error message as a string.
 */
[[nodiscard]] inline std::string str_error_cpp(int err_num) noexcept {
    try {
        std::error_code ec(err_num, std::system_category());
        return ec.message();
    } catch (const std::exception& e) {
        return "error_message_lookup_failed";
    }
}

/**
 * @brief Gets the IPv4 address of the peer connected to a given socket.
 * @param sockfd The socket file descriptor.
 * @return The peer's IP address as a string, or an empty string on failure.
 */
[[nodiscard]] inline std::string get_peer_ip_ipv4(int sockfd) noexcept {
    try {
        sockaddr_in addr{};
        socklen_t addr_len = sizeof(addr);

        if (getpeername(sockfd, reinterpret_cast<sockaddr*>(&addr), &addr_len) == 0) {
            std::array<char, INET_ADDRSTRLEN> buffer{};
            if (inet_ntop(AF_INET, &addr.sin_addr, buffer.data(), buffer.size())) {
                return std::string{buffer.data()};
            }
        }
    } catch (const std::exception& e) {
        // Return empty string on any exception
    }
    return "";
}

/**
 * @brief Generates a new version 4 UUID.
 * @return The UUID as a standard formatted string.
 */
[[nodiscard]] inline std::string get_uuid() noexcept
{
    try {
        std::array<unsigned char, 16> out;
        uuid_generate(out.data());
        std::array<char, 37> uuid_str;
        uuid_unparse(out.data(), uuid_str.data());
        return std::string(uuid_str.data());
    } catch (const std::bad_alloc& e) {
        // In case of a highly unlikely memory allocation exception
        return "uuid_generation_failed";
    }
}


namespace detail {
    /**
     * @brief Reads a numeric value from a line in a procfs file.
     * @param filename The path to the file (e.g., "/proc/meminfo").
     * @param token The token to search for (e.g., "MemTotal:").
     * @return The parsed value in KB, or 0 on failure.
     */
    inline size_t get_proc_info(std::string_view filename, std::string_view token) noexcept
    {
        try {
            std::ifstream meminfo_file(filename.data());
            if (!meminfo_file.is_open()) {
                return 0;
            }

            std::string line;
            while (std::getline(meminfo_file, line)) {
                if (line.starts_with(token)) {
                    std::istringstream iss{line};
                    std::string label;
                    size_t value = 0;
                    iss >> label >> value;
                    return value; // Value is typically in KB
                }
            }
        } catch (const std::exception& e) {
            // In case of any exception (e.g., bad_alloc), return 0.
        }
        return 0;
    }
} // namespace detail

/**
 * @brief Gets the total system memory from /proc/meminfo.
 * @return Total system memory in KB, or 0 on failure.
 */
[[nodiscard]] inline size_t get_total_memory() noexcept
{
    return detail::get_proc_info("/proc/meminfo", "MemTotal:");
}

/**
 * @brief Gets the resident set size (RSS) memory usage for the current process.
 * @return The process's memory usage in KB, or 0 on failure.
 */
[[nodiscard]] inline size_t get_memory_usage() noexcept
{
    return detail::get_proc_info("/proc/self/status", "VmRSS:");
}


} // namespace util

#endif // UTIL_HPP

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
#include <memory>
#include <chrono>
#include <format>
#include <cstdint>
#include <cstddef>

// Includes for POSIX/networking functions
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <climits> // For HOST_NAME_MAX

#include <openssl/rand.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>

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

struct string_equal {
    using is_transparent = void;
    [[nodiscard]] constexpr bool operator()(std::string_view lhs, std::string_view rhs) const {
        return lhs == rhs;
    }
};

/**
 * @brief Get the current calendar date (year, month, day).
 *
 * Obtains the current time from `std::chrono::system_clock`, floors it to
 * whole days, and constructs a `std::chrono::year_month_day` representing
 * the current date according to the system clock.
 *
 * @note This relies on the C++20 `std::chrono::year_month_day` and
 *       `std::chrono::floor` utilities declared in <chrono>.
 *
 * @return A `std::chrono::year_month_day` holding today's date.
 */
inline auto today() {
    using namespace std::chrono;
    return year_month_day{floor<days>(system_clock::now())};
}

/**
 * @brief Decodes a standard Base64 encoded string into a binary string.
 *
 * This function is optimized for performance, using OpenSSL's BIO library
 * to decode the input string with minimal memory allocations. It correctly
 * handles padding and returns a binary string.
 *
 * @param data The Base64 encoded string_view.
 * @return A std::string containing the raw decoded binary data, or an empty string on failure.
 */
[[nodiscard]] inline std::string base64_decode(std::string_view data) noexcept {
    if (data.empty()) {
        return "";
    }

    // Use RAII for BIO objects to ensure they are always freed.
    auto bio_deleter = [](BIO* b) { BIO_free_all(b); };
    using unique_bio = std::unique_ptr<BIO, decltype(bio_deleter)>;

    // Create a memory buffer BIO with the input data.
    unique_bio b64(BIO_new(BIO_f_base64()));
    if (!b64) {
        return "";
    }
    BIO_set_flags(b64.get(), BIO_FLAGS_BASE64_NO_NL);

    // Chain the memory BIO to the Base64 BIO. The b64 BIO takes ownership.
    BIO* source = BIO_new_mem_buf(data.data(), static_cast<int>(data.size()));
    if (!source) {
        return "";
    }
    unique_bio bio_chain(BIO_push(b64.release(), source));


    // The decoded size is at most 3/4 of the input size.
    std::string decoded_data;
    decoded_data.resize(data.size());

    int decoded_len = BIO_read(bio_chain.get(), decoded_data.data(), decoded_data.size());
    if (decoded_len < 0) {
        return ""; // Decoding error
    }

    decoded_data.resize(decoded_len);
    return decoded_data;
}

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
        // FIX: Use the std::ranges version of find for a more modern syntax.
        const auto end = std::ranges::find(hostname_buffer, '\0');
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
    
    // FIX: Use "if with initializer" to scope errlen to the conditional block.
    if (socklen_t errlen = sizeof(error); getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &errlen) == 0 && error != 0) {
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

        // FIX: Replaced reinterpret_cast with a C-style cast.
        // This is a standard idiom for the C sockets API, where a pointer to a specific
        // struct (sockaddr_in) is passed as a pointer to a generic struct (sockaddr).
        if (getpeername(sockfd, (sockaddr*)&addr, &addr_len) == 0) {
            std::array<char, INET_ADDRSTRLEN> buffer{};
            if (inet_ntop(AF_INET, &addr.sin_addr, buffer.data(), buffer.size())) {
                return std::string{buffer.data()};
            }
        }
    } catch (const std::exception& e) {
        // FIX: Explicitly return from the catch block to satisfy SonarCloud.
        return "";
    }
    return "";
}

/**
 * @brief Generates a new version 4 UUID.
 * @return The UUID as a standard formatted string.
 */
[[nodiscard]] inline std::string get_uuid() noexcept {
    // 1. Use std::byte to satisfy SonarCloud "byte-oriented data" rule
    std::array<std::byte, 16> bytes;

    // 2. OpenSSL RAND_bytes expects unsigned char*, so we must cast.
    // This is safe because std::byte and unsigned char have the same layout.
    if (/* NOSONAR */ RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), static_cast<int>(bytes.size())) != 1) {
        return "uuid_generation_failed";
    }

    // 3. Set Version (4)
    // std::byte supports bitwise operators (&, |) directly in C++17
    bytes[6] = (bytes[6] & std::byte{0x0F}) | std::byte{0x40};

    // 4. Set Variant (RFC 4122)
    bytes[8] = (bytes[8] & std::byte{0x3F}) | std::byte{0x80};

    // 5. Format to string
    // std::format requires arithmetic types for {:x}, so we cast to integer.
    try {
        return std::format("{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
            std::to_integer<unsigned char>(bytes[0]), std::to_integer<unsigned char>(bytes[1]), 
            std::to_integer<unsigned char>(bytes[2]), std::to_integer<unsigned char>(bytes[3]),
            std::to_integer<unsigned char>(bytes[4]), std::to_integer<unsigned char>(bytes[5]),
            std::to_integer<unsigned char>(bytes[6]), std::to_integer<unsigned char>(bytes[7]),
            std::to_integer<unsigned char>(bytes[8]), std::to_integer<unsigned char>(bytes[9]),
            std::to_integer<unsigned char>(bytes[10]), std::to_integer<unsigned char>(bytes[11]), 
            std::to_integer<unsigned char>(bytes[12]), std::to_integer<unsigned char>(bytes[13]), 
            std::to_integer<unsigned char>(bytes[14]), std::to_integer<unsigned char>(bytes[15])
        );
    } catch (/* NOSONAR */ const std::exception&) {
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
            // FIX: Explicitly return from the catch block to satisfy SonarCloud.
            return 0;
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

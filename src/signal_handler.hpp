#ifndef SIGNAL_HANDLER_HPP
#define SIGNAL_HANDLER_HPP

#include <csignal>
#include <stdexcept>
#include <sys/signalfd.h>
#include <unistd.h>
#include <vector>

namespace util {

/// @class signal_error
/// @brief Exception thrown for errors related to signal handling setup.
class signal_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/**
 * @class signal_handler
 * @brief A RAII wrapper for handling POSIX signals gracefully.
 */
class signal_handler {
public:
    signal_handler() {
        // 1. Ignore SIGPIPE. Writes to a closed socket will return an error (EPIPE)
        //    instead of terminating the process. This is standard practice for network servers.
        signal(SIGPIPE, SIG_IGN);

        // 2. Block shutdown signals so they can be handled via signalfd.
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTERM);
        sigaddset(&mask, SIGQUIT);

        if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1) {
            throw signal_error("Failed to set sigprocmask");
        }

        // 3. Create a file descriptor for reading signal events.
        //    This is now a BLOCKING descriptor for the main thread to wait on.
        m_fd = signalfd(-1, &mask, 0); // REMOVED SFD_NONBLOCK
        if (m_fd == -1) {
            throw signal_error("Failed to create signalfd");
        }
    }

    ~signal_handler() {
        if (m_fd != -1) {
            close(m_fd);
        }
    }

    // --- Non-copyable and non-movable ---
    signal_handler(const signal_handler&) = delete;
    signal_handler& operator=(const signal_handler&) = delete;
    signal_handler(signal_handler&&) = delete;
    signal_handler& operator=(signal_handler&&) = delete;

    /**
     * @brief Gets the file descriptor for monitoring signal events.
     * @return The signal file descriptor.
     */
    [[nodiscard]] int get_fd() const noexcept {
        return m_fd;
    }

private:
    int m_fd{-1};
};

} // namespace util

#endif // SIGNAL_HANDLER_HPP

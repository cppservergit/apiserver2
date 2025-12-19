#ifndef SERVER_HPP
#define SERVER_HPP

#include "http_request.hpp"
#include "http_response.hpp"
#include "logger.hpp"
#include "env.hpp"
#include "signal_handler.hpp"
#include "metrics.hpp"
#include "api_router.hpp"
#include "thread_pool.hpp"
#include "shared_queue.hpp"
#include "util.hpp"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <cstdint>
#include <chrono> // Added for timeouts

inline constexpr auto g_version = "1.1.1";

using dispatch_task = std::function<void()>;

class server_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct connection_state {
    explicit connection_state(std::string ip) 
        : remote_ip(std::move(ip)), last_activity(std::chrono::steady_clock::now()) {}
    connection_state() = default;
    
    http::request_parser parser;
    std::optional<http::response> response;
    std::string remote_ip;
    std::chrono::steady_clock::time_point last_activity; // Track last read/write

    void reset() {
        parser = http::request_parser{};
        response.reset();
        update_activity(); // Reset timer on new request cycle
    }    

    void update_activity() {
        last_activity = std::chrono::steady_clock::now();
    }
};

struct response_item {
    int client_fd;
    http::response res;
};

class server {
public:
    server();
    ~server() noexcept;

    server(const server&) = delete;
    server& operator=(const server&) = delete;
    server(server&&) = delete;
    server& operator=(server&&) = delete;

    template<typename Validator>
    void register_api(webapi_path path, http::method method, const Validator& v, api_handler_func handler, bool is_secure = true) {
        m_router.register_api(path, method, v, std::move(handler), is_secure);
    }

    void register_api(webapi_path path, http::method method, api_handler_func handler, bool is_secure = true) {
        m_router.register_api(path, method, std::move(handler), is_secure);
    }

    void start();

private:
    class io_worker {
    public:
        io_worker(uint16_t port,
                    std::shared_ptr<metrics> metrics, 
                    const api_router& router,
                    const std::unordered_set<std::string, util::string_hash, util::string_equal>& allowed_origins,
                    int worker_thread_count,
                    size_t queue_capacity, // <--- NEW PARAMETER
                    std::atomic<bool>& running_flag);
        
        ~io_worker() noexcept;
        void run();

        [[nodiscard]] const thread_pool* get_thread_pool() const {
            return m_thread_pool.get();
        }

    private:
        void setup_listening_socket();
        void add_to_epoll(int fd, uint32_t events);
        void remove_from_epoll(int fd);
        void modify_epoll(int fd, uint32_t events);

        void on_connect();
        void on_read(int fd);
        void on_write(int fd);
        void close_connection(int fd);
        void check_timeouts(); // Helper to clean up idle connections

        bool handle_socket_read(connection_state& conn, int fd);
        void process_request(int fd);
        void dispatch_to_worker(int fd, http::request req, const api_endpoint* endpoint);
        void process_response_queue();
        bool handle_internal_api(const http::request& req, http::response& res) const;
        
        void execute_handler(const http::request& req, http::response& res, const api_endpoint* endpoint) const;
        [[nodiscard]] bool validate_token(const http::request& req) const;

        int m_listening_fd{-1};
        uint16_t m_port;
        int m_epoll_fd{-1};
        
        std::shared_ptr<metrics> m_metrics;
        const api_router& m_router;
        // *** BUG FIX *** Use the correct set type with the transparent hasher
        const std::unordered_set<std::string, util::string_hash, util::string_equal>& m_allowed_origins;
        std::atomic<bool>& m_running;
        
        std::unique_ptr<thread_pool> m_thread_pool;
        std::unique_ptr<shared_queue<response_item>> m_response_queue;
        std::unordered_map<int, connection_state> m_connections;

        // API Key for internal endpoints
        std::string m_api_key;
    };

    static inline constexpr int MAX_EVENTS = 8192;
    static inline constexpr int LISTEN_BACKLOG = 65536;
	static inline constexpr int EPOLL_WAIT_MS = 5;
    // Timeout for idle connections (Slowloris protection)
    static inline constexpr std::chrono::seconds READ_TIMEOUT{60}; 

    uint16_t m_port;
    int m_io_threads;
    int m_worker_threads;
    
    std::unique_ptr<util::signal_handler> m_signals;
    std::shared_ptr<metrics> m_metrics;
    api_router m_router;
    // *** BUG FIX *** Use the correct set type with the transparent hasher
    std::unordered_set<std::string, util::string_hash, util::string_equal> m_allowed_origins;

    std::vector<std::unique_ptr<io_worker>> m_workers;
    std::atomic<bool> m_running{true};

    //task queue back pressure control
    size_t m_queue_capacity{0};
};

#endif // SERVER_HPP
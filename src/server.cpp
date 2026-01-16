#include "server.hpp"
#include "shared_queue.hpp"
#include "util.hpp"
#include "sql.hpp"
#include "jwt.hpp"
#include "cors.hpp"
#include "http_client.hpp"
#include <system_error>
#include <format>
#include <cstring>
#include <functional>
#include <chrono>
#include <memory>
#include <sstream>
#include <algorithm>
#include <numeric>

using namespace std::chrono_literals;
using namespace std::string_view_literals;

// ===================================================================
//         server::io_worker Implementation
// ===================================================================
server::io_worker::io_worker(uint16_t port,
                             std::shared_ptr<metrics> metrics_ptr, 
                             const api_router& router,
                             const std::unordered_set<std::string, util::string_hash, util::string_equal>& allowed_origins,
                             int worker_thread_count,
                             size_t queue_capacity, // <--- NEW PARAMETER
                             std::atomic<bool>& running_flag)
    : m_port(port),
      m_metrics(metrics_ptr), 
      m_router(router),
      m_allowed_origins(allowed_origins),
      m_running(running_flag) {
          
    // Pass queue_capacity to thread_pool
    m_thread_pool = std::make_unique<thread_pool>(worker_thread_count, queue_capacity);
    
    // Use a larger limit (e.g., 2x) for responses to ensure completed work isn't dropped
    m_response_queue = std::make_unique<shared_queue<response_item>>(queue_capacity * 2); 
    
    m_api_key = env::get<std::string>("API_KEY", "");
}

server::io_worker::~io_worker() noexcept {
    try {
        if (m_thread_pool) {
            m_thread_pool->stop();
        }
        if (m_listening_fd != -1) {
            close(m_listening_fd);
        }
    } catch (const std::exception& e) { /* NOSONAR */ }
}

void server::io_worker::run() {
    try {
        setup_listening_socket();
    } catch (const server_error& e) {
        util::log::critical("I/O worker failed to start: {}", e.what());
        return;
    }

    util::log::debug("I/O worker thread {} started and listening on port {}.", std::this_thread::get_id(), m_port);
    m_thread_pool->start();

    std::vector<epoll_event> events(MAX_EVENTS);
    
    // Track last timeout check locally to avoid checking every 5ms
    auto last_timeout_check = std::chrono::steady_clock::now();

    while (m_running) {
        const int num_events = epoll_wait(m_epoll_fd, events.data(), events.size(), EPOLL_WAIT_MS);
        if (num_events == -1) {
            if (errno == EINTR) continue;
            util::log::error("epoll_wait failed in worker {}: {}", std::this_thread::get_id(), util::str_error_cpp(errno));
            return;
        }
        for (int i = 0; i < num_events; ++i) {
            const auto& event = events[i];
            const int fd = event.data.fd;

            if (fd == m_listening_fd) {
                on_connect();
            } else if (event.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                close_connection(fd);
            } else if (event.events & EPOLLIN) {
                on_read(fd);
            } else if (event.events & EPOLLOUT) {
                on_write(fd);
            }
        }
        process_response_queue();
        
        // Timeout check logic (~1Hz)
        auto now = std::chrono::steady_clock::now();
        if (now - last_timeout_check >= 1s) {
            check_timeouts();
            last_timeout_check = now;
        }
    }
    drain_pending_responses();
    util::log::debug("I/O worker thread {} finished.", std::this_thread::get_id());
}

void server::io_worker::check_timeouts() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = m_connections.begin(); it != m_connections.end(); ) {
        if (now - it->second.last_activity > server::READ_TIMEOUT) {
            util::log::debug("Connection timed out for fd {}", it->first);
            close(it->first);
            m_metrics->decrement_connections();
            it = m_connections.erase(it);
        } else {
            ++it;
        }
    }
}

void server::io_worker::drain_pending_responses() {
    // This loop ensures that when Kubernetes stops the pod, we finish processing 
    // in-flight requests and send their responses before exiting.
    util::log::debug("I/O worker thread {} shutting down. Draining pending responses...", std::this_thread::get_id());

    std::vector<epoll_event> events(MAX_EVENTS);

    while (m_thread_pool->get_total_pending_tasks() > 0 || m_response_queue->size() > 0) {
        process_response_queue();
        
        // Short timeout (10ms) to spin quickly while draining
        const int num_events = epoll_wait(m_epoll_fd, events.data(), events.size(), 10);
        util::log::debug("Detected {} EPOLL events on thread {}.", num_events, std::this_thread::get_id());
        for (int i = 0; i < num_events; ++i) {
            if (events[i].events & EPOLLOUT) {
                on_write(events[i].data.fd);
            }
        }
    }
    util::log::debug("I/O worker thread {} drain complete.", std::this_thread::get_id());
}

// FIX: Implement the new private helper function for token validation.
bool server::io_worker::validate_token(const http::request& req) const {
    auto token_opt = req.get_bearer_token();
    if (!token_opt) {
        util::log::warn("Missing JWT token on request {} from {}",
                      req.get_path(),
                      req.get_remote_ip());
        return false;
    }
        
    if (auto validation_result = jwt::is_valid(*token_opt); !validation_result) {
        util::log::warn("JWT validation failed for user '{}' on request {} from {}: {}",
                      req.get_user(),
                      req.get_path(),
                      req.get_remote_ip(),
                      jwt::to_string(validation_result.error()));
        return false;
    }

    return true;
}

void server::io_worker::execute_handler(const http::request& request_ref, http::response& res, const api_endpoint* endpoint) const {
    using enum http::status;
    try {
        if (endpoint->method != request_ref.get_method()) {
            res.set_body(bad_request, R"({"error":"Method Not Allowed"})");
            return;
        }

        if (endpoint->is_secure && !validate_token(request_ref)) {
            res.set_body(http::status::unauthorized, R"({"error":"Invalid or missing token"})");
            return;
        }

        endpoint->validator(request_ref);
        endpoint->handler(request_ref, res);

    } catch (const validation::validation_error& e) {
        res.set_body(bad_request, std::format(R"({{"error":"{}"}})", e.what()));
    } catch (const sql::error& e) {
        util::log::error("SQL error in handler for path '{}': {}", request_ref.get_path(), e.what());
        res.set_body(internal_server_error, R"({"error":"Database operation failed"})");
    } catch (const json::parsing_error& e) {
        util::log::error("JSON parsing error in handler for path '{}': {}", request_ref.get_path(), e.what());
        res.set_body(bad_request, R"({"error":"Invalid JSON format in request"})");
    } catch (const json::output_error& e) {
        util::log::error("JSON output error in handler for path '{}': {}", request_ref.get_path(), e.what());
        res.set_body(internal_server_error, R"({"error":"Failed to generate JSON response"})");
    } catch (const curl_exception& e) {
        util::log::error("HTTP client error in handler for path '{}': {}", request_ref.get_path(), e.what());
        res.set_body(internal_server_error, R"({"error":"Internal communication failed"})");
    } catch (/* NOSONAR */ const std::exception& e) {
        util::log::error("Unhandled exception in handler for path '{}': {}", request_ref.get_path(), e.what());
        res.set_body(internal_server_error, R"({"error":"Internal Server Error"})");
    }
}

void server::io_worker::dispatch_to_worker(int fd, http::request req, const api_endpoint* endpoint) {
    auto req_ptr = std::make_shared<http::request>(std::move(req));

    try {
        m_thread_pool->push_task([this, fd, req_ptr, endpoint]() {
            const std::string request_id_str(req_ptr->get_header_value("x-request-id").value_or(""));
            const util::log::request_id_scope rid_scope(request_id_str);

            util::log::debug("Dispatching request to worker thread {} for fd {}", req_ptr->get_path(), fd);
            
            const auto start_time = std::chrono::high_resolution_clock::now();
            m_metrics->increment_active_threads();
            
            http::response res(req_ptr->get_header_value("Origin"));
            
            execute_handler(*req_ptr, res, endpoint);
            
            const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start_time);
            
            m_response_queue->push({fd, std::move(res)});
            m_metrics->record_request_time(duration);
            m_metrics->decrement_active_threads();

            util::log::perf("API handler for '{}' executed in {} microseconds.", req_ptr->get_path(), duration.count());
        });
    } catch (const queue_full_error&) {
        using enum http::status;
        util::log::warn("Worker queue full. Dropping request for '{}' from {}", 
                        req_ptr->get_path(), req_ptr->get_remote_ip());
        
        http::response res(req_ptr->get_header_value("Origin"));
        res.set_body(service_unavailable, R"({"error":"Service Unavailable: Server Overloaded"})");
        
        try {
            m_response_queue->push({fd, std::move(res)});
            add_to_epoll(fd, EPOLLOUT); 
        } catch (const queue_full_error&) {
            util::log::error("Critical: Response queue full for fd {}. Closing connection.", fd);
            close_connection(fd); 
        } catch (const server_error& ex) {
            util::log::error("Critical: Epoll failure for fd {}: {}. Closing connection.", fd, ex.what());
            close_connection(fd);
        }
    }
}

// ... (rest of server.cpp is unchanged) ...
void server::io_worker::setup_listening_socket() {
    m_listening_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listening_fd == -1) throw server_error("Failed to create socket");

    int opt = 1;
    if (setsockopt(m_listening_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) throw server_error("Failed to set SO_REUSEADDR");
    if (setsockopt(m_listening_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) == -1) throw server_error("Failed to set SO_REUSEPORT");

    if (fcntl(m_listening_fd, F_SETFL, O_NONBLOCK) == -1) throw server_error("Failed to set socket to non-blocking");

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(m_port);
    if (bind(m_listening_fd, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) throw server_error(std::format("Failed to bind to port {}", m_port));
    if (listen(m_listening_fd, LISTEN_BACKLOG) == -1) throw server_error("Failed to listen on socket");

    m_epoll_fd = epoll_create1(0);
    if (m_epoll_fd == -1) throw server_error("Failed to create epoll instance for worker");
    add_to_epoll(m_listening_fd, EPOLLIN);
}

void server::io_worker::add_to_epoll(int fd, uint32_t events) {
    epoll_event event{};
    event.events = events | EPOLLET | EPOLLRDHUP;
    event.data.fd = fd;
    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1 && errno != EEXIST) {
        throw server_error(std::format("Failed to add fd {} to epoll", fd));
    }
}

void server::io_worker::remove_from_epoll(int fd) {
    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        util::log::error("Failed to remove fd {} from epoll: {}", fd, util::str_error_cpp(errno));
    }
}

void server::io_worker::modify_epoll(int fd, uint32_t events) {
    epoll_event event{};
    event.events = events | EPOLLET | EPOLLRDHUP;
    event.data.fd = fd;
    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, fd, &event) == -1) {
        util::log::error("Failed to modify fd {} in epoll: {}", fd, util::str_error_cpp(errno));
    }
}

void server::io_worker::on_connect() {
    while (true) {
        int client_fd = accept4(m_listening_fd, nullptr, nullptr, SOCK_NONBLOCK);
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            util::log::error("accept4 failed: {}", util::str_error_cpp(errno));
            continue;
        }
        std::string client_ip = util::get_peer_ip_ipv4(client_fd);
        util::log::debug("Thread {} accepted new connection from {} on fd {}", std::this_thread::get_id(), client_ip, client_fd);
        
        m_connections.try_emplace(client_fd, std::move(client_ip));
        
        add_to_epoll(client_fd, EPOLLIN);
        m_metrics->increment_connections();
    }
}

void server::io_worker::on_read(int fd) {
    auto it = m_connections.find(fd);
    if (it == m_connections.end()) return;

    if (!handle_socket_read(it->second, fd)) return;

    if (it->second.parser.eof()) {
        process_request(fd);
    }
}

void server::io_worker::on_write(int fd) {
    auto it = m_connections.find(fd);
    if (it == m_connections.end() || !it->second.response.has_value()) return;
    
    http::response& res = *it->second.response;
    
    // Update activity on write too
    it->second.update_activity();

    while (res.available_size() > 0) {
        ssize_t bytes_sent = write(fd, res.buffer().data(), res.buffer().size());
        if (bytes_sent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            util::log::error("write error on fd {}: {}", fd, util::str_error_cpp(errno));
            close_connection(fd);
            return;
        }
        res.update_pos(bytes_sent);
    }

    if (res.available_size() == 0) {
        it->second.reset();
        modify_epoll(fd, EPOLLIN);
        util::log::debug("Response fully sent on fd {}", fd);
    }
}

void server::io_worker::close_connection(int fd) {
    util::log::debug("Closing connection on fd {}.", fd);
    close(fd);
    if (m_connections.erase(fd) > 0) {
        m_metrics->decrement_connections();
    }
}

bool server::io_worker::handle_socket_read(connection_state& conn, int fd) {
    // Update last activity timestamp on read
    conn.update_activity();
    
    while (true) {
        // Wrap buffer access and read operations in a try-catch block
        try {
            auto buffer = conn.parser.get_buffer();
            if (buffer.empty()) {
                util::log::error("Parser buffer full for fd {}", fd);
                close_connection(fd);
                return false;
            }
            ssize_t bytes_read = read(fd, buffer.data(), buffer.size());
            if (bytes_read == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                util::log::error("read error on fd {}: {}", fd, util::str_error_cpp(errno));
                close_connection(fd);
                return false;
            }
            if (bytes_read == 0) {
                close_connection(fd);
                return false;
            }
            conn.parser.update_pos(bytes_read);
        } catch (const socket_buffer_error& e) {
            using enum http::status;
            close_connection(fd);
            util::log::warn("Socket buffer error on fd {} from IP {}: {}", fd, conn.remote_ip, e.what());
            return false; 
        } catch (const std::exception& e) { // NOSONAR: Generic catch required to prevent worker thread crash
            util::log::error("Unexpected exception during socket read on fd {}: {}", fd, e.what());
            close_connection(fd);
            return false;
        }
    }
    return true;
}

void server::io_worker::process_request(int fd) {
    auto it = m_connections.find(fd);
    if (it == m_connections.end()) return;
    
    remove_from_epoll(fd);
    connection_state& conn = it->second;

    if (auto res = conn.parser.finalize(); !res.has_value()) {
        util::log::error("Failed to parse request on fd {} from IP {}: {}", fd, conn.remote_ip, res.error().what());
        http::response err_res;
        err_res.set_body(http::status::bad_request, R"({"error":"Bad Request"})");
        m_response_queue->push({fd, std::move(err_res)});
        return;
    }

    http::request req(std::move(conn.parser), conn.remote_ip);
    
    if (!cors::is_origin_allowed(req.get_header_value("Origin"), m_allowed_origins)) {
        util::log::warn("CORS check failed for origin: {}", req.get_header_value("Origin").value_or("N/A"));
        http::response err_res;
        err_res.set_body(http::status::forbidden, R"({"error":"CORS origin not allowed"})");
        m_response_queue->push({fd, std::move(err_res)});
        return;
    }

    http::response res(req.get_header_value("Origin"));

    if (req.get_method() == http::method::options) {
        res.set_options();
        m_response_queue->push({fd, std::move(res)});
    } else if (handle_internal_api(req, res)) {
        m_response_queue->push({fd, std::move(res)});
    } else {
        const auto* endpoint = m_router.find_handler(req.get_path());
        if (!endpoint) {
            res.set_body(http::status::not_found, R"({"error":"Not Found"})");
            m_response_queue->push({fd, std::move(res)});
        } else {
            dispatch_to_worker(fd, std::move(req), endpoint);
        }
    }
}

bool server::io_worker::handle_internal_api(const http::request& req, http::response& res) const {
    using enum http::status;

    // Helper lambda to check for Authorization: Bearer <token>
    auto check_bearer_auth = [&](std::string_view path) {
        if (m_api_key.empty()) return true;

        auto header_val = req.get_header_value("Authorization");

        // 1. Check existence and minimum length ("bearer " is 7 chars)
        if (!header_val || header_val->size() < 7) {
            util::log::warn("Unauthorized (missing Bearer) to {} from {}", path, req.get_remote_ip());
            return false;
        }

        // 2. Case-insensitive prefix check using std::ranges
        auto prefix = header_val->substr(0, 7);
        bool is_bearer = std::ranges::equal(prefix, "bearer "sv, 
            [](unsigned char a, unsigned char b) { 
                return std::tolower(a) == std::tolower(b); 
            }
        );

        if (!is_bearer) {
            util::log::warn("Unauthorized (invalid scheme: {}) to {} from {}", prefix, path, req.get_remote_ip());
            return false;
        }

        // 3. Extract token (substring from index 7) and compare
        std::string_view token = std::string_view(*header_val).substr(7);
        if (token != m_api_key) {
            util::log::warn("Unauthorized (invalid token) to {} from {}", path, req.get_remote_ip());
            return false;
        }
        return true;
    };

    if (req.get_path() == "/metrics") {
        if (!check_bearer_auth("/metrics")) {
            res.set_body(bad_request, R"({"error":"Bad Request"})");
            return true;
        }
        res.set_body(ok, m_metrics->to_json());
        return true;
    }
    if (req.get_path() == "/metricsp") {
        if (!check_bearer_auth("/metricsp")) {
            res.set_body(bad_request, R"({"error":"Bad Request"})");
            return true;
        }        
        res.set_body(ok, m_metrics->to_prometheus(), "text/plain");
        return true;
    }
    if (req.get_path() == "/ping") {
        res.set_body(ok, R"({"status":"OK"})");
        return true;
    }
    if (req.get_path() == "/version") {
        if (!check_bearer_auth("/version")) {
            res.set_body(bad_request, R"({"error":"Bad Request"})");
            return true;
        }
        auto constexpr json_tpl = R"({{"pod_name":"{}","version":"{}"}})";
        res.set_body(ok, std::format(json_tpl, m_metrics->get_pod_name(), g_version));
        return true;
    }
    return false;
}

void server::io_worker::process_response_queue() {
    std::vector<response_item> response_batch;
    response_batch.reserve(64); 

    m_response_queue->drain_to(response_batch);

    for (auto& item : response_batch) {
        if (auto it = m_connections.find(item.client_fd); it != m_connections.end()) {
            it->second.response = std::move(item.res);
           add_to_epoll(it->first, EPOLLOUT);
        }
    }
}

// ===================================================================
//         server Implementation
// ===================================================================

server::server() {
    m_port = env::get<int>("PORT", 8080);
    m_io_threads = env::get<int>("IO_THREADS", std::thread::hardware_concurrency());
    m_worker_threads = env::get<int>("POOL_SIZE", 16);
    // NEW: Get queue capacity configuration (Default 1000 tasks per worker queue)
    m_queue_capacity = env::get<size_t>("QUEUE_CAPACITY", 1000uz);
    
    m_signals = std::make_unique<util::signal_handler>();
    m_metrics = std::make_shared<metrics>(m_worker_threads);
    
    const std::string origins_str = env::get<std::string>("CORS_ORIGINS", "");
    if (!origins_str.empty()) {
        std::stringstream ss(origins_str);
        std::string origin;
        while (std::getline(ss, origin, ',')) {
            m_allowed_origins.insert(origin);
        }
        util::log::info("CORS enabled for {} origin(s).", m_allowed_origins.size());
    }
}

server::~server() noexcept = default;

void server::start() {
    util::log::info("APIServer2 version {} starting on port {} with {} I/O threads and {} total worker threads.", 
        g_version, m_port, m_io_threads, m_worker_threads);

    const int worker_threads_per_io = std::max(1, m_worker_threads / m_io_threads);
    util::log::info("Assigning {} worker threads per I/O worker.", worker_threads_per_io);

    std::vector<std::jthread> io_worker_threads;
    io_worker_threads.reserve(m_io_threads);

    // Stage 1: Create all the worker objects and populate the vector.
    for (int i = 0; i < m_io_threads; ++i) {
        // Pass m_queue_capacity to the worker
        auto worker = std::make_unique<io_worker>(
            m_port, m_metrics, m_router, m_allowed_origins, 
            worker_threads_per_io, m_queue_capacity, m_running // <--- UPDATED ARGUMENTS
        );
        m_metrics->register_thread_pool(worker->get_thread_pool());
        m_workers.push_back(std::move(worker));
    }

    // Stage 2: Now that the vector is stable, create the threads.
    for (int i = 0; i < m_io_threads; ++i) {
        io_worker_threads.emplace_back([this, i] { m_workers[i]->run(); });
    }

	signalfd_siginfo ssi;
    if (ssize_t bytes_read = read(m_signals->get_fd(), &ssi, sizeof(ssi)); bytes_read == sizeof(ssi)) {
        const char* signal_name = strsignal(ssi.ssi_signo);
        util::log::info("Received signal {} ({}), shutting down.", ssi.ssi_signo, signal_name ? signal_name : "Unknown");
    }
    
    m_running = false;
}
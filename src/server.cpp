#include "server.hpp"
#include "shared_queue.hpp"
#include "util.hpp"
#include "sql.hpp"
#include "jwt.hpp"
#include "cors.hpp"
#include "http_client.hpp"
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <system_error>
#include <format>
#include <cstring>
#include <functional>
#include <chrono>
#include <memory>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <ranges>

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
                             size_t queue_capacity,
                             std::atomic<bool>& running_flag)
    : m_port(port),
      m_metrics(metrics_ptr), 
      m_router(router),
      m_allowed_origins(allowed_origins),
      m_running(running_flag) {
          
    m_thread_pool = std::make_unique<thread_pool>(worker_thread_count, queue_capacity);
    m_response_queue = std::make_unique<shared_queue<response_item, true>>(queue_capacity * 2); 
    
    m_api_key = env::get<std::string>("API_KEY", "");
    m_mfa_uri = env::get<std::string>("MFA_URI", "/validate/totp");    
}

server::io_worker::~io_worker() noexcept {
    if (m_timer_fd != -1) close(m_timer_fd);
    if (m_event_fd != -1) close(m_event_fd);
    if (m_listening_fd != -1) close(m_listening_fd);
    if (m_thread_pool) m_thread_pool->stop();
}

void server::io_worker::setup_timerfd() {
    m_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (m_timer_fd == -1) throw server_error("Failed to create timerfd");

    struct itimerspec ts{};
    ts.it_value.tv_sec = 1;      
    ts.it_interval.tv_sec = 1;   
    
    if (timerfd_settime(m_timer_fd, 0, &ts, nullptr) == -1) {
        throw server_error("Failed to set timerfd interval");
    }
    add_to_epoll(m_timer_fd, EPOLLIN);
}

void server::io_worker::setup_eventfd() {
    m_event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_event_fd == -1) throw server_error("Failed to create eventfd");

    m_response_queue->set_event_fd(m_event_fd);
    add_to_epoll(m_event_fd, EPOLLIN);
}

void server::io_worker::run() {
    try {
        setup_listening_socket();
        setup_timerfd();
        setup_eventfd();
    } catch (const server_error& e) {
        util::log::critical("I/O worker startup failed: {}", e.what());
        return;
    }

    util::log::debug("I/O worker thread {} started and listening on port {}.", std::this_thread::get_id(), m_port);
    m_thread_pool->start();
    
    std::vector<epoll_event> events(MAX_EVENTS);

    while (m_running) {
        const int num_events = epoll_wait(m_epoll_fd, events.data(), events.size(), -1);
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
            } else if (fd == m_timer_fd) {
                uint64_t expirations;
                if (read(m_timer_fd, &expirations, sizeof(expirations)) > 0) {
                    check_timeouts();
                }
            } else if (fd == m_event_fd) {
                on_response_ready();
            } else if (event.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                close_connection(fd);
            } else if (event.events & EPOLLIN) {
                on_read(fd);
            } else if (event.events & EPOLLOUT) {
                on_write(fd);
            }
        }
    }
    drain_pending_responses();
    util::log::debug("I/O worker thread {} finished.", std::this_thread::get_id());
}

void server::io_worker::check_timeouts() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = m_connections.begin(); it != m_connections.end(); ) {
        if (now - it->second.last_activity > server::READ_TIMEOUT) {
            int fd = it->first;
            close(fd);
            m_metrics->decrement_connections();
            it = m_connections.erase(it);
        } else {
            ++it;
        }
    }
}

void server::io_worker::on_response_ready() {
    uint64_t val;
    [[maybe_unused]] ssize_t s = read(m_event_fd, &val, sizeof(val));
    process_response_queue();
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

void server::io_worker::drain_pending_responses() {
    util::log::debug("I/O worker thread {} shutting down. Draining pending responses...", std::this_thread::get_id());
    std::vector<epoll_event> events(MAX_EVENTS);

    while (m_thread_pool->get_total_pending_tasks() > 0 || m_response_queue->size() > 0) {
        process_response_queue();
        
        const int num_events = epoll_wait(m_epoll_fd, events.data(), events.size(), 10);
        for (int i = 0; i < num_events; ++i) {
            if (events[i].events & EPOLLOUT) {
                on_write(events[i].data.fd);
            }
        }
    }
    util::log::debug("I/O worker thread {} drain complete.", std::this_thread::get_id());
}

bool server::io_worker::validate_token(const http::request& req) const {
    auto token_opt = req.get_bearer_token();
    if (!token_opt) {
        util::log::warn("Missing JWT token on request {} from {}", req.get_path(), req.get_remote_ip());
        return false;
    }
    
    auto validation_result = jwt::is_valid(*token_opt);
    if (!validation_result) {
        util::log::warn("JWT validation failed for user '{}' on request {} from {}: {}",
                      req.get_user(), req.get_path(), req.get_remote_ip(),
                      jwt::to_string(validation_result.error()));
        return false;
    }

    const auto& claims = *validation_result;
    bool is_preauth = false;
    std::string user = "unknown";
    if (auto it = claims.find("user"); it != claims.end()) user = it->second;
    if (auto it = claims.find("preauth"); it != claims.end() && it->second == "true") is_preauth = true;

    bool is_target_mfa = (req.get_path() == m_mfa_uri);

    if (is_preauth && !is_target_mfa) {
        util::log::warn("Security Alert: Attempt to use pre-auth token for user '{}' on '{}' from {}. Access Denied.", 
            user, req.get_path(), req.get_remote_ip());
        return false;
    }

    if (!is_preauth && is_target_mfa) {
        util::log::warn("Security Alert: Fully authenticated token for user '{}' attempting to re-access MFA URI from {}. Access Denied.", 
            user, req.get_remote_ip());
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
            res.set_body(unauthorized, R"({"error":"Invalid or missing token"})");
            return;
        }

        if(endpoint->is_secure)
            util::log::debug("Authenticated request by user '{}' with sessionId '{}' for path '{}' from {}",
                request_ref.get_user(), request_ref.get_sessionId(), request_ref.get_path(), request_ref.get_remote_ip());

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
    } catch (const std::exception& e) {
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
        util::log::warn("Worker queue full. Dropping request for '{}' from {}", req_ptr->get_path(), req_ptr->get_remote_ip());
        http::response res(req_ptr->get_header_value("Origin"));
        res.set_body(http::status::service_unavailable, R"({"error":"Service Unavailable: Server Overloaded"})");
        
        try {
            m_response_queue->push({fd, std::move(res)});
            add_to_epoll(fd, EPOLLOUT); 
        } catch (...) {
            util::log::error("Critical: Response queue full for fd {}. Closing connection.", fd);
            close_connection(fd); 
        }
    }
}

void server::io_worker::setup_listening_socket() {
    m_listening_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listening_fd == -1) throw server_error("Failed to create socket");

    int opt = 1;
    setsockopt(m_listening_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(m_listening_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    fcntl(m_listening_fd, F_SETFL, O_NONBLOCK);

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(m_port);
    if (bind(m_listening_fd, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) throw server_error("Bind failed");
    if (listen(m_listening_fd, server::LISTEN_BACKLOG) == -1) throw server_error("Listen failed");

    m_epoll_fd = epoll_create1(0);
    add_to_epoll(m_listening_fd, EPOLLIN);
}

void server::io_worker::add_to_epoll(int fd, uint32_t events) {
    epoll_event event{};
    event.events = events | EPOLLET | EPOLLRDHUP;
    event.data.fd = fd;
    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1 && errno != EEXIST) {
        throw server_error("epoll_ctl ADD failed");
    }
}

void server::io_worker::modify_epoll(int fd, uint32_t events) {
    epoll_event event{};
    event.events = events | EPOLLET | EPOLLRDHUP;
    event.data.fd = fd;
    epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, fd, &event);
}

void server::io_worker::on_connect() {
    while (true) {
        int client_fd = accept4(m_listening_fd, nullptr, nullptr, SOCK_NONBLOCK);
        if (client_fd == -1) break;
        m_connections.try_emplace(client_fd, util::get_peer_ip_ipv4(client_fd));
        add_to_epoll(client_fd, EPOLLIN);
        m_metrics->increment_connections();
    }
}

void server::io_worker::on_read(int fd) {
    auto it = m_connections.find(fd);
    if (it != m_connections.end() && handle_socket_read(it->second, fd)) {
        if (it->second.parser.eof()) process_request(fd);
    }
}

void server::io_worker::on_write(int fd) {
    auto it = m_connections.find(fd);
    if (it == m_connections.end() || !it->second.response.has_value()) return;
    
    http::response& res = *it->second.response;
    it->second.update_activity();

    while (res.available_size() > 0) {
        ssize_t s = write(fd, res.buffer().data(), res.buffer().size());
        if (s == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            close_connection(fd);
            return;
        }
        res.update_pos(s);
    }

    it->second.reset();
    modify_epoll(fd, EPOLLIN);
}

void server::io_worker::close_connection(int fd) {
    close(fd);
    if (m_connections.erase(fd) > 0) m_metrics->decrement_connections();
}

bool server::io_worker::handle_socket_read(connection_state& conn, int fd) {
    conn.update_activity();
    while (true) {
        try {
            auto buffer = conn.parser.get_buffer();
            if (buffer.empty()) return false;
            ssize_t s = read(fd, buffer.data(), buffer.size());
            if (s == -1) return (errno == EAGAIN || errno == EWOULDBLOCK);
            if (s == 0) { close_connection(fd); return false; }
            conn.parser.update_pos(s);
        } catch (...) {
            close_connection(fd);
            return false;
        }
    }
}

void server::io_worker::process_request(int fd) {
    auto it = m_connections.find(fd);
    if (it == m_connections.end()) return;
    
    epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr); 

    if (auto res_opt = it->second.parser.finalize(); !res_opt.has_value()) {
        util::log::error("Failed to parse request on fd {} from IP {}: {}", fd, it->second.remote_ip, res_opt.error().what());
        http::response err;
        err.set_body(http::status::bad_request, R"({"error":"Bad Request"})");
        m_response_queue->push({fd, std::move(err)});
        return;
    }

    http::request req(std::move(it->second.parser), it->second.remote_ip);
    const std::string request_id_str(req.get_header_value("x-request-id").value_or(""));
    const util::log::request_id_scope rid_scope(request_id_str);    

    if (!cors::is_origin_allowed(req.get_header_value("Origin"), m_allowed_origins)) {
        util::log::warn("CORS check failed for origin: {} for path '{}' from {}", 
            req.get_header_value("Origin").value_or("N/A"), req.get_path(), req.get_remote_ip());
        http::response err;
        err.set_body(http::status::forbidden, R"({"error":"CORS origin not allowed"})");
        m_response_queue->push({fd, std::move(err)});
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
            util::log::warn("BOT-ALERT No handler found for path '{}' from {}", req.get_path(), req.get_remote_ip());
            res.set_body(http::status::not_found, R"({"error":"Not Found"})");
            m_response_queue->push({fd, std::move(res)});
        } else {
            dispatch_to_worker(fd, std::move(req), endpoint);
        }
    }
}

bool server::io_worker::handle_internal_api(const http::request& req, http::response& res) const {
    using enum http::status;
    if (req.get_path() == "/metrics") {
        if (!validate_bearer_token(req, "/metrics")) { res.set_body(bad_request, R"({"error":"Bad Request"})"); return true; }
        res.set_body(ok, m_metrics->to_json()); return true;
    }
    if (req.get_path() == "/metricsp") {
        if (!validate_bearer_token(req, "/metricsp")) { res.set_body(bad_request, R"({"error":"Bad Request"})"); return true; }
        res.set_body(ok, m_metrics->to_prometheus(), "text/plain"); return true;
    }
    if (req.get_path() == "/ping") { res.set_body(ok, R"({"status":"OK"})"); return true; }
    if (req.get_path() == "/version") {
        if (!validate_bearer_token(req, "/version")) { res.set_body(bad_request, R"({"error":"Bad Request"})"); return true; }
        res.set_body(ok, std::format(R"({{"pod_name":"{}","version":"{}"}})", m_metrics->get_pod_name(), g_version));
        return true;
    }
    return false;
}

bool server::io_worker::validate_bearer_token(const http::request& req, std::string_view path) const {
    if (m_api_key.empty()) return true;
    const auto token_opt = req.get_bearer_token();
    if (!token_opt) { util::log::warn("Unauthorized (missing or malformed Bearer header) to {} from {}", path, req.get_remote_ip()); return false; }
    if (*token_opt != m_api_key) { util::log::warn("Unauthorized (token mismatch) to {} from {}", path, req.get_remote_ip()); return false; }
    return true;
}

// ===================================================================
//         server Implementation
// ===================================================================
server::server() {
    m_port = env::get<int>("PORT", 8080);
    m_io_threads = env::get<int>("IO_THREADS", std::thread::hardware_concurrency());
    m_worker_threads = env::get<int>("POOL_SIZE", 16);
    m_queue_capacity = env::get<size_t>("QUEUE_CAPACITY", 1000uz);
    
    m_signals = std::make_unique<util::signal_handler>();
    m_metrics = std::make_shared<metrics>(m_worker_threads);
    
    const std::string origins_str = env::get<std::string>("CORS_ORIGINS", "");
    if (!origins_str.empty()) {
        std::stringstream ss(origins_str);
        std::string origin;
        while (std::getline(ss, origin, ',')) m_allowed_origins.insert(origin);
        util::log::info("CORS enabled for {} origin(s).", m_allowed_origins.size());
    }
}

server::~server() noexcept = default;

void server::start() {
    auto setup_start = std::chrono::steady_clock::now();

    util::log::info("APIServer2 version {} starting on port {} with {} I/O threads and {} total worker threads.", 
        g_version, m_port, m_io_threads, m_worker_threads);

    const int worker_threads_per_io = std::max(1, m_worker_threads / m_io_threads);
    util::log::info("Assigning {} worker threads per I/O worker.", worker_threads_per_io);

    std::vector<std::jthread> io_worker_threads;
    io_worker_threads.reserve(m_io_threads);

    for (int i = 0; i < m_io_threads; ++i) {
        auto worker = std::make_unique<io_worker>(
            m_port, m_metrics, m_router, m_allowed_origins, 
            worker_threads_per_io, m_queue_capacity, m_running
        );
        m_metrics->register_thread_pool(worker->get_thread_pool());
        m_workers.push_back(std::move(worker));
    }

    for (int i = 0; i < m_io_threads; ++i) {
        io_worker_threads.emplace_back([this, i] { m_workers[i]->run(); });
    }

    auto setup_end = std::chrono::steady_clock::now();
    auto setup_ms = std::chrono::duration_cast<std::chrono::milliseconds>(setup_end - setup_start).count();
    util::log::info("Server started in {} milliseconds.", setup_ms);

    signalfd_siginfo ssi;
    if (read(m_signals->get_fd(), &ssi, sizeof(ssi)) == sizeof(ssi)) {
        const char* signal_name = strsignal(ssi.ssi_signo);
        util::log::info("Received signal {} ({}), shutting down.", ssi.ssi_signo, signal_name ? signal_name : "Unknown");
    }
    
    m_running = false;
    for (auto& w : m_workers) {
        w->get_response_queue()->stop(); 
    }
}
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
    m_response_queue = std::make_unique<shared_queue<response_item, true>>(); 
    
    m_api_key = env::get<std::string>("API_KEY", "");
    m_mfa_uri = env::get<std::string>("MFA_URI", "/validate/totp");    
}

server::io_worker::~io_worker() noexcept {
    if (m_timer_fd != -1) close(m_timer_fd);
    if (m_event_fd != -1) close(m_event_fd);
    if (m_listening_fd != -1) close(m_listening_fd);
    if (m_thread_pool) m_thread_pool->stop();
    if (m_epoll_fd != -1) close(m_epoll_fd);
}

void server::io_worker::setup_timerfd() {
    m_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (m_timer_fd == -1) throw server_error("Failed to create timerfd");

    struct itimerspec ts{};
    ts.it_value.tv_sec = 5;      
    ts.it_interval.tv_sec = 5;   
    
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

// Extracted to fix SonarCloud Cognitive Complexity > 15
void server::io_worker::handle_epoll_event(const epoll_event& event) {
    const int fd = event.data.fd;
    const uint32_t ev = event.events;

    if (fd == m_listening_fd) {
        on_connect();
        return;
    } 
    if (fd == m_timer_fd) {
        on_timer_tick();
        return;
    } 
    if (fd == m_event_fd) {
        on_response_ready();
        return;
    } 
    
    if ((ev & EPOLLIN) != 0) {
        on_read(fd);
    }
    
    if ((ev & EPOLLOUT) != 0) {
        on_write(fd);
    }
    
    // Flattened nested 'if' statements to pass Sonar checks
    if ((ev & (EPOLLERR | EPOLLHUP)) != 0 && m_connections.contains(fd)) {
        close_connection(fd);
    } else if ((ev & EPOLLRDHUP) != 0) {
        if (auto it = m_connections.find(fd); it != m_connections.end()) {
            it->second.close_after_write = true;
        }
    }
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
            handle_epoll_event(events[i]);
        }
    }
    drain_pending_responses();
    util::log::debug("I/O worker thread {} finished.", std::this_thread::get_id());
}

void server::io_worker::on_timer_tick() {
    uint64_t expirations;
    if (read(m_timer_fd, &expirations, sizeof(expirations)) > 0) {
        check_timeouts();
    }
}

void server::io_worker::check_timeouts() {
    auto now = std::chrono::steady_clock::now();
    auto it_list = m_timeout_list.begin();
    
    while (it_list != m_timeout_list.end()) {
        int fd = *it_list;
        auto it_conn = m_connections.find(fd);
        
        if (it_conn == m_connections.end()) {
            it_list = m_timeout_list.erase(it_list);
            continue;
        }

        // Exemption: Do not timeout connections currently executing a heavy API task
        if (it_conn->second.is_processing) {
            ++it_list;
            continue;
        }

        // Reversed logic to eliminate 'else' branch (SonarCloud optimization)
        if (now - it_conn->second.last_activity <= server::READ_TIMEOUT) {
            break;
        }

        remove_from_epoll(fd);
        close(fd);
        m_metrics->decrement_connections();
        m_connections.erase(it_conn);
        it_list = m_timeout_list.erase(it_list);
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
        // Flattened nested 'if' to pass Sonar checks
        auto it = m_connections.find(item.client_fd);
        if (it != m_connections.end() && it->second.connection_id == item.connection_id) {
            it->second.is_processing = false; // Worker is done, resume standard timeouts
            it->second.response = std::move(item.res);
            do_write(item.client_fd, it->second);
        } else {
            util::log::warn("Dropped stale response for reused fd {}", item.client_fd);
        }
    }
}

void server::io_worker::drain_pending_responses() {
    util::log::info("I/O worker thread shutting down. Draining pending responses...");
    std::vector<epoll_event> events(MAX_EVENTS);

    util::log::info("Waiting for {} unfinished tasks to complete...", m_thread_pool->get_unfinished_tasks());

    while (m_thread_pool->get_unfinished_tasks() > 0 || m_response_queue->size() > 0) {
        process_response_queue();
        
        const int num_events = epoll_wait(m_epoll_fd, events.data(), events.size(), 10);
        
        if (num_events == -1) {
            if (errno == EINTR) continue;
            util::log::error("epoll_wait failed during drain in worker {}: {}", std::this_thread::get_id(), util::str_error_cpp(errno));
            break; 
        }

        for (int i = 0; i < num_events; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == m_listening_fd || fd == m_timer_fd || fd == m_event_fd) {
                continue;
            }

            if ((ev & EPOLLOUT) != 0) {
                on_write(fd);
            }
            
            // Extract the connection lookup to prevent a 4th level of nesting
            auto it = m_connections.find(fd);
            if (it == m_connections.end()) {
                continue; // The connection was already closed (perhaps by on_write hitting EOF)
            }

            // The remaining logic is now safely flattened to a maximum depth of 3
            if ((ev & (EPOLLERR | EPOLLHUP | EPOLLIN)) != 0) {
                close_connection(fd);
            } else if ((ev & EPOLLRDHUP) != 0) {
                it->second.close_after_write = true;
            }
        }
    }
    util::log::info("I/O worker thread {} drain complete.", std::this_thread::get_id());
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
            res.set_body(http::status::unauthorized, R"({"error":"Invalid or missing token"})");
            return;
        }

        if(endpoint->is_secure)
            util::log::debug("Authenticated request by user '{}' with sessionId '{}' for path '{}' from {}",
                request_ref.get_user(),
                request_ref.get_sessionId(),
                request_ref.get_path(),
                request_ref.get_remote_ip()
            );

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

void server::io_worker::dispatch_to_worker(int fd, uint64_t connection_id, http::request req, const api_endpoint* endpoint) {
    auto req_ptr = std::make_shared<http::request>(std::move(req));

    try {
        m_thread_pool->push_task([this, fd, connection_id, req_ptr, endpoint]() {
            const std::string request_id_str(req_ptr->get_header_value("x-request-id").value_or(""));
            const util::log::request_id_scope rid_scope(request_id_str);

            util::log::debug("Dispatching request to worker thread {} for fd {}", req_ptr->get_path(), fd);
            
            const auto start_time = std::chrono::high_resolution_clock::now();
            m_metrics->increment_active_threads();
            
            http::response res(req_ptr->get_header_value("Origin"));
            
            execute_handler(*req_ptr, res, endpoint);
            
            const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start_time);
            
            m_response_queue->push({fd, connection_id, std::move(res)});
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
            m_response_queue->push({fd, connection_id, std::move(res)});
        } catch (const server_error& ex) {
            util::log::error("Critical: Epoll failure for fd {}: {}. Closing connection.", fd, ex.what());
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
    epoll_event ev{};
    ev.events = events | EPOLLET | EPOLLRDHUP;
    ev.data.fd = fd;
    
    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1) {
        if (errno == ENOENT) {
            util::log::error("server::io_worker::modify_epoll -> fd {} not found: {}, adding it instead.", fd, util::str_error_cpp(errno));
            if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
                util::log::error("Fatal error adding fd {} to epoll: {}", fd, util::str_error_cpp(errno));
                close_connection(fd);
            }
        } else {
            util::log::error("Fatal error modifying epoll for fd {}: {}", fd, util::str_error_cpp(errno));
            close_connection(fd);
        }
    }
}

void server::io_worker::remove_from_epoll(int fd) const {
    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        util::log::error("Failed to remove fd {} from epoll: {}", fd, util::str_error_cpp(errno));
    }
}

void server::io_worker::on_connect() {
    while (true) {
        // Handle successful connection first to avoid nesting the error logic
        if (int client_fd = accept4(m_listening_fd, nullptr, nullptr, SOCK_NONBLOCK); client_fd != -1) {
            try {
                std::string client_ip = util::get_peer_ip_ipv4(client_fd);
                util::log::debug("Thread {} accepted new connection from {} on fd {}", std::this_thread::get_id(), client_ip, client_fd);
                
                auto& conn = m_connections.try_emplace(client_fd, std::move(client_ip)).first->second;
                conn.connection_id = ++m_next_connection_id;
                
                m_timeout_list.push_back(client_fd);
                conn.timeout_it = std::prev(m_timeout_list.end());
                
                add_to_epoll(client_fd, EPOLLIN | EPOLLONESHOT);
                m_metrics->increment_connections();
            } catch (const server_error& e) {
                util::log::error("Failed to initialize connection for fd {}: {}", client_fd, e.what());
                close(client_fd);
                m_connections.erase(client_fd); 
            }
            continue; // Loop again for the next pending connection
        }
        
        // If we reach here, accept4 returned -1. Flattened error handling (Max Depth 2):
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Normal exit: queue is empty, do nothing and let it hit the break
        } else if (errno == EMFILE || errno == ENFILE) {
            util::log::warn("accept4 failed: File descriptor limit reached (EMFILE/ENFILE). Halting accepts.");
        } else {
            // Recoverable error (e.g. ECONNABORTED)
            util::log::error("accept4 failed: {}", util::str_error_cpp(errno));
            continue; 
        }
        
        break; // Exactly 1 break statement for the entire while loop
    }
}

void server::io_worker::touch_connection(connection_state& conn) {
    conn.update_activity();
    m_timeout_list.splice(m_timeout_list.end(), m_timeout_list, conn.timeout_it);
}

void server::io_worker::on_read(int fd) {
    if (auto it = m_connections.find(fd); it != m_connections.end()
         && handle_socket_read(it->second, fd)) {
        if (it->second.parser.eof()) {
            process_request(fd);
        } else {
            modify_epoll(fd, EPOLLIN | EPOLLONESHOT);
        }
    }
}

void server::io_worker::on_write(int fd) {
    if (auto it = m_connections.find(fd); it != m_connections.end()) {
        do_write(fd, it->second);
    }
}

void server::io_worker::do_write(int fd, connection_state& conn) {
    if (!conn.response.has_value()) return;
    
    http::response& res = *conn.response;
    
    touch_connection(conn);

    while (res.available_size() > 0) {
        ssize_t bytes_sent = write(fd, res.buffer().data(), res.buffer().size());
        if (bytes_sent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                util::log::debug("rearming epoll for writing fd: {}", fd);
                modify_epoll(fd, EPOLLOUT | EPOLLONESHOT);
                return;
            }
            util::log::error("write error on fd {}: {}", fd, util::str_error_cpp(errno));
            close_connection(fd);
            return;
        }
        res.update_pos(bytes_sent);
    }

    if (res.available_size() == 0) {
        if (conn.close_after_write) {
            close_connection(fd);
            return;
        }
        
        conn.reset();
        touch_connection(conn);
        modify_epoll(fd, EPOLLIN | EPOLLONESHOT);
        util::log::debug("Response fully sent on fd {}", fd);
    }
}

void server::io_worker::close_connection(int fd) {
    remove_from_epoll(fd);
    close(fd);
    if (auto it = m_connections.find(fd); it != m_connections.end()) {
        m_timeout_list.erase(it->second.timeout_it);
        m_connections.erase(it);
        m_metrics->decrement_connections();
    }
}

bool server::io_worker::handle_socket_read(connection_state& conn, int fd) {
    touch_connection(conn);
    
    while (true) {
        try {
            auto buffer = conn.parser.get_buffer();
            if (buffer.empty()) {
                util::log::error("Parser buffer full for fd {}", fd);
                close_connection(fd);
                return false;
            }
            
            ssize_t bytes_read = read(fd, buffer.data(), buffer.size());
            
            // Handle successful read first to prevent nested error trees
            if (bytes_read > 0) {
                conn.parser.update_pos(bytes_read);
            } else {
                // Flattened error and EOF handling (Max Depth: 3)
                if (bytes_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    // Normal exit: socket read buffer is fully drained
                } else if (bytes_read == 0 && conn.parser.eof()) {
                    conn.close_after_write = true;
                } else if (bytes_read == -1) {
                    util::log::error("read error on fd {}: {}", fd, util::str_error_cpp(errno));
                    close_connection(fd);
                    return false;
                } else {
                    // Client hung up midway through an incomplete request or while idle.
                    close_connection(fd);
                    return false;
                }
                
                break; // Exactly 1 break statement for the entire while loop
            }
        } catch (const socket_buffer_error& e) {
            using enum http::status;
            close_connection(fd);
            util::log::warn("Socket buffer error on fd {} from IP {}: {}", fd, conn.remote_ip, e.what());
            return false; 
        } catch (/* NOSONAR */ const std::exception& e) {
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
    
    connection_state& conn = it->second;
    const uint64_t conn_id = conn.connection_id;

    if (auto res = conn.parser.finalize(); !res.has_value()) {
        util::log::error("Failed to parse request on fd {} from IP {}: {}", fd, conn.remote_ip, res.error().what());
        http::response err_res;
        err_res.set_body(http::status::bad_request, R"({"error":"Bad Request"})");
        
        conn.close_after_write = true;
        m_response_queue->push({fd, conn_id, std::move(err_res)});
        return;
    }

    http::request req(std::move(conn.parser), conn.remote_ip);
    route_parsed_request(fd, conn_id, std::move(req));
}

// Extracted to fix SonarCloud Cognitive Complexity > 15
void server::io_worker::route_parsed_request(int fd, uint64_t conn_id, http::request req) {
    const std::string request_id_str(req.get_header_value("x-request-id").value_or(""));
    const util::log::request_id_scope rid_scope(request_id_str);    

    if (!cors::is_origin_allowed(req.get_header_value("Origin"), m_allowed_origins)) {
        util::log::warn("CORS check failed for origin: {} for path '{}' from {}", 
            req.get_header_value("Origin").value_or("N/A"), req.get_path(), req.get_remote_ip());
        http::response err_res;
        err_res.set_body(http::status::forbidden, R"({"error":"CORS origin not allowed"})");
        m_response_queue->push({fd, conn_id, std::move(err_res)});
        return;
    }

    http::response res(req.get_header_value("Origin"));

    // Flattened routing logic via early returns to optimize SonarCloud complexity
    if (req.get_method() == http::method::options) {
        res.set_options();
        m_response_queue->push({fd, conn_id, std::move(res)});
        return;
    } 
    
    if (handle_internal_api(req, res)) {
        m_response_queue->push({fd, conn_id, std::move(res)});
        return;
    } 
    
    const auto* endpoint = m_router.find_handler(req.get_path());
    if (!endpoint) {
        util::log::warn("BOT-ALERT No handler found for path '{}' from {}", req.get_path(), req.get_remote_ip());
        res.set_body(http::status::not_found, R"({"error":"Not Found"})");
        m_response_queue->push({fd, conn_id, std::move(res)});
        return;
    } 
    
    // Flag the connection as executing to prevent it from timing out during heavy workloads
    if (auto it = m_connections.find(fd); it != m_connections.end()) {
        it->second.is_processing = true;
    }
    
    dispatch_to_worker(fd, conn_id, std::move(req), endpoint);
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

    util::log::info("APIServer2 version {} starting on {}:{}.", 
        g_version, util::get_pod_name(), m_port);

    util::log::info("Using {} I/O threads and {} total worker threads.", 
        m_io_threads, m_worker_threads);

    const int worker_threads_per_io = std::max(1, m_worker_threads / m_io_threads);
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
    auto setup_ms = std::chrono::duration_cast<std::chrono::microseconds>(setup_end - setup_start).count();
    util::log::info("Server started in {} microseconds.", setup_ms);

    if (signalfd_siginfo ssi; read(m_signals->get_fd(), &ssi, sizeof(ssi)) == sizeof(ssi)) {
        const char* signal_name = strsignal(ssi.ssi_signo);
        util::log::info("Received signal {} ({}), shutting down.", ssi.ssi_signo, signal_name ? signal_name : "Unknown");
    }
    
    m_running = false;
    for (const auto& w : m_workers) {
        w->get_response_queue()->stop(); 
    }
}
#include "mail_service.hpp"
#include "shared_queue.hpp"
#include "env.hpp"
#include "logger.hpp"
#include <curl/curl.h>
#include <thread>
#include <vector>
#include <format>
#include <chrono>
#include <cstring> // Required for std::memcpy

/**
 * @brief Represents the data for a single email task.
 */
struct mail_payload {
    std::string recipient;
    std::string subject;
    std::string body;
};

/**
 * @brief Private service class that manages the background thread and task queue.
 */
class mail_worker {
public:
    static mail_worker& instance() {
        /* NOSONAR */ static mail_worker service;
        return service;
    }

    void push_task(mail_payload&& payload) {
        m_queue.push(std::move(payload));
    }

    mail_worker(const mail_worker&) = delete;
    mail_worker& operator=(const mail_worker&) = delete;
    mail_worker(mail_worker&&) = delete;
    mail_worker& operator=(mail_worker&&) = delete;

private:
    mail_worker() 
        : m_thread([this](std::stop_token st) { worker_loop(st); }) {
        util::log::info("Mail background service started.");
    }

    ~mail_worker() {
        // SonarQube: Do not log in destructor as it may throw or cause issues during shutdown.
        m_queue.stop();
        // jthread joins automatically
    }

    void worker_loop(std::stop_token st) {
        while (!st.stop_requested()) {
            if (auto payload = m_queue.wait_and_pop(); payload.has_value()) {
                if (auto result = send_smtp(*payload); !result.has_value()) {
                    util::log::error("Failed to send email to {}: {}", payload->recipient, result.error());
                } else {
                    util::log::info("Email sent successfully to {}", payload->recipient);
                }
            } else {
                // Queue was stopped
                break;
            }
        }
    }

    /**
     * @brief Helper to provide data to libcurl for SMTP uploading.
     */
    struct upload_status {
        const std::vector<std::string>& lines;
        size_t current_line{0};
    };

    static size_t payload_source(char* ptr, size_t size, size_t nmemb, /* NOSONAR */ void* userp) {
        auto* status = static_cast<upload_status*>(userp);
        
        if (status->current_line >= status->lines.size()) {
            return 0;
        }

        const std::string& line = status->lines[status->current_line];
        const size_t len = line.length();
        
        if (size * nmemb < len) {
            return CURL_READFUNC_ABORT;
        }

        std::memcpy(ptr, line.c_str(), len);
        status->current_line++;
        return len;
    }

    [[nodiscard]] mail_result send_smtp(const mail_payload& payload) const {
        CURL* curl = curl_easy_init();
        if (!curl) {
            return std::unexpected("Failed to initialize CURL handle");
        }
        
        // SonarQube cpp:S4423 Fix: Enforce TLS v1.2 as minimum
        curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

        // RAII for CURL handle
        auto curl_deleter = [](/* NOSONAR */ CURL* c) { curl_easy_cleanup(c); };
        std::unique_ptr<CURL, decltype(curl_deleter)> curl_ptr(curl, curl_deleter);

        const std::string server = env::get<std::string>("MAIL_SERVER", "");
        const std::string user = env::get<std::string>("MAIL_USER", "");
        const std::string pass = env::get<std::string>("MAIL_PASSWORD", "");

        if (server.empty() || user.empty() || pass.empty()) {
            return std::unexpected("SMTP configuration is missing (MAIL_SERVER, MAIL_USER, or MAIL_PASSWORD)");
        }

        // Construct headers and body
        std::vector<std::string> message_lines;
        message_lines.reserve(8); 
        message_lines.emplace_back(std::format("To: <{}>\r\n", payload.recipient));
        message_lines.emplace_back(std::format("From: <{}>\r\n", user));
        message_lines.emplace_back(std::format("Subject: {}\r\n", payload.subject));
        
        if (payload.body.contains("<html") || payload.body.contains("<body")) {
            message_lines.emplace_back("Content-Type: text/html; charset=UTF-8\r\n");
        } else {
            message_lines.emplace_back("Content-Type: text/plain; charset=UTF-8\r\n");
        }
        
        message_lines.emplace_back("\r\n"); 
        message_lines.emplace_back(payload.body);
        message_lines.emplace_back("\r\n");

        upload_status status{message_lines};

        curl_easy_setopt(curl, CURLOPT_URL, server.c_str());
        curl_easy_setopt(curl, CURLOPT_USERNAME, user.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, pass.c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, std::format("<{}>", user).c_str());
        
        struct curl_slist* recipients = nullptr;
        recipients = curl_slist_append(recipients, std::format("<{}>", payload.recipient).c_str());
        auto slist_deleter = [](struct curl_slist* s) { curl_slist_free_all(s); };
        std::unique_ptr<struct curl_slist, decltype(slist_deleter)> recipients_ptr(recipients, slist_deleter);

        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients_ptr.get());
        
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
        curl_easy_setopt(curl, CURLOPT_READDATA, &status);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

        if (const CURLcode res = curl_easy_perform(curl); res != CURLE_OK) {
            return std::unexpected(std::format("curl_easy_perform() failed: {}", curl_easy_strerror(res)));
        }

        return {};
    }

    shared_queue<mail_payload> m_queue;
    std::jthread m_thread;
};


void send_email(std::string recipient, std::string subject, std::string body) {
    mail_worker::instance().push_task({
        .recipient = std::move(recipient),
        .subject = std::move(subject),
        .body = std::move(body)
    });
}

#ifndef MAIL_SERVICE_HPP
#define MAIL_SERVICE_HPP

#include <string>
#include <string_view>
#include <expected>

/**
 * @brief Asynchronously sends an email.
 * 
 * This function returns immediately. The email is processed in a dedicated 
 * background thread using libcurl and SMTP.
 * 
 * @param recipient The recipient's email address.
 * @param subject The email's subject line.
 * @param body The email's body (can be text or HTML).
 */
void send_email(std::string recipient, std::string subject, std::string body);

/**
 * @brief Represents the result of an email sending operation.
 * Used internally by the background worker.
 */
using mail_result = std::expected<void, std::string>;

#endif // MAIL_SERVICE_HPP

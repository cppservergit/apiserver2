#ifndef QRCODE_HPP
#define QRCODE_HPP

#include <qrencode.h>
#include <string>
#include <string_view>
#include <memory>
#include <expected>
#include <format>
#include <iterator>

namespace qr {

    namespace detail {
        struct QrDeleter {
            void operator()(QRcode* qr) const noexcept {
                if (qr != nullptr) {
                    QRcode_free(qr);
                }
            }
        };
        using unique_qr = std::unique_ptr<QRcode, QrDeleter>;
    }

    /**
     * @brief Generates a high-performance, compact SVG QR code using C++23.
     * 
     * Thread-safe and memory-safe. Uses a single <path> with horizontal 
     * run-length optimization to minimize SVG size.
     *
     * @param data The text or URI to encode.
     * @return A std::expected containing the SVG string or an error.
     */
    [[nodiscard]] inline std::expected<std::string, std::string> generate_svg(std::string_view data) noexcept {
        if (data.empty()) {
            return std::unexpected("QR data cannot be empty");
        }

        try {
            // libqrencode requires a null-terminated C-string
            const std::string data_str{data};
            
            // QR_ECLEVEL_L: 7% recovery, smallest size. Best for TOTP URIs.
            detail::unique_qr qr_ptr{QRcode_encodeString(data_str.c_str(), 0, QR_ECLEVEL_L, QR_MODE_8, 1)};
            
            if (!qr_ptr) {
                return std::unexpected("libqrencode failed to generate QR matrix");
            }

            constexpr int margin = 4;
            const int size = qr_ptr->width;
            const int full_size = size + (2 * margin);

            // Pre-allocate to minimize reallocations
            std::string result;
            result.reserve(static_cast<size_t>(size) * 32); 

            // Use crispEdges to prevent blurry rendering at small sizes
            std::format_to(std::back_inserter(result), 
                R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {0} {0}" shape-rendering="crispEdges">)"
                R"(<path fill="#ffffff" d="M0 0h{0}v{0}H0z"/>)"
                R"(<path fill="#000000" d=")", 
                full_size);

            // Helper lambdas to reduce nesting and cognitive complexity
            auto is_black = [&](int x, int y) noexcept { 
                return (/* NOSONAR */ qr_ptr->data[(y * size) + x] & 1) != 0; 
            };

            auto find_run_length = [&](int x, int y) noexcept {
                int run = 1;
                while ((x + run < size) && is_black(x + run, y)) {
                    ++run;
                }
                return run;
            };

            auto append_run = [&](int x, int y, int run) {
                std::format_to(std::back_inserter(result), "M{} {}h{}v1h-{}z", x + margin, y + margin, run, run);
            };

            auto process_row = [&](int y) {
                int x = 0;
                while (x < size) {
                    if (is_black(x, y)) {
                        const int run = find_run_length(x, y);
                        append_run(x, y, run);
                        x += run;
                    } else {
                        ++x;
                    }
                }
            };

            // Process each row independently
            for (int y = 0; y < size; ++y) {
                process_row(y);
            }

            result += R"("/></svg>)";
            return result;

        } catch (const std::bad_alloc&) {
            return std::unexpected("Critical memory allocation failure during QR generation");
        } catch (const std::exception& e) {
            return std::unexpected(std::format("QR Logic Error: {}", e.what()));
        }
    }
}

#endif // QRCODE_HPP

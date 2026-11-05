#include "esp32_https_client.hpp"

#include <cstdint>
#include <string>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"

namespace aura::firmware {
namespace {

// A read waits at most this long, so a cancel or the request's own deadline
// takes effect soon even while the service is silent.
constexpr int kReadTimeoutMs = 5000;

std::int64_t now_ms() { return esp_timer_get_time() / 1000; }

}  // namespace

HttpsResponse Esp32HttpsClient::post(const HttpsRequest& request, const HttpsChunk& on_chunk) {
    cancelled_.store(false);
    HttpsResponse response;
    if (request.url.rfind("https://", 0) != 0) {
        response.error = "NETWORK";
        return response;
    }
    esp_http_client_config_t config{};
    config.url = request.url.c_str();
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = kReadTimeoutMs;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.buffer_size = 4096;
    config.buffer_size_tx = 2048;
    config.keep_alive_enable = false;
    config.disable_auto_redirect = true;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        response.error = "NETWORK";
        return response;
    }
    for (const auto& header : request.headers) {
        esp_http_client_set_header(client, header.name.c_str(), header.value.c_str());
    }
    const std::int64_t deadline = now_ms() + request.timeout_ms;
    const auto finish = [&](const char* error) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (error != nullptr && response.error.empty()) response.error = error;
        if (error != nullptr && !on_chunk) response.body.clear();
        return response;
    };
    if (esp_http_client_open(client, static_cast<int>(request.body.size())) != ESP_OK) {
        return finish(cancelled_.load() ? "CANCELLED" : "NETWORK");
    }
    for (std::size_t sent = 0; sent < request.body.size();) {
        const int written = esp_http_client_write(client, request.body.data() + sent,
                                                  static_cast<int>(request.body.size() - sent));
        if (written <= 0 || cancelled_.load()) return finish(cancelled_.load() ? "CANCELLED" : "NETWORK");
        sent += static_cast<std::size_t>(written);
    }
    if (esp_http_client_fetch_headers(client) < 0) return finish("NETWORK");
    response.status = esp_http_client_get_status_code(client);
    char buffer[2048];
    for (;;) {
        if (cancelled_.load()) {
            response.status = 0;
            return finish("CANCELLED");
        }
        if (now_ms() > deadline) {
            response.status = 0;
            return finish("TIMEOUT");
        }
        const int read = esp_http_client_read(client, buffer, sizeof(buffer));
        if (read == -ESP_ERR_HTTP_EAGAIN) continue;  // nothing within a read: check cancel and the deadline
        if (read == 0) break;                         // the end of the body
        if (read < 0) {
            response.status = 0;
            return finish("NETWORK");
        }
        if (on_chunk) {
            if (!on_chunk(std::string_view(buffer, static_cast<std::size_t>(read)))) break;
        } else {
            if (response.body.size() + static_cast<std::size_t>(read) > request.max_body_bytes) {
                response.status = 0;
                return finish("RESPONSE_TOO_LARGE");
            }
            response.body.append(buffer, static_cast<std::size_t>(read));
        }
    }
    return finish(nullptr);
}

}  // namespace aura::firmware

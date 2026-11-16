#include "esp32_https_client.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace aura::firmware {
namespace {

constexpr const char* kTag = "aura_cloud";

// A read waits at most this long, so a cancel or the request's own deadline
// takes effect soon even while the service is silent.
constexpr int kReadTimeoutMs = 5000;

std::int64_t now_ms() { return esp_timer_get_time() / 1000; }

// "https://host": the server a kept connection belongs to.
std::string origin_of(const std::string& url) {
    const auto path = url.find('/', 8);
    return path == std::string::npos ? url : url.substr(0, path);
}

// What the log may show of a URL: its host and path. Keys travel in headers,
// which are never logged.
std::string shown_url(const std::string& url) {
    std::string shown = url.substr(std::min<std::size_t>(url.size(), 8));  // after "https://"
    const auto query = shown.find('?');
    if (query != std::string::npos) shown.resize(query);
    return shown;
}

// Internal RAM, which TLS and the panel's transfers share: free, and the
// largest block the panel's DMA could take.
struct InternalRam {
    unsigned free_kib{~0U};
    unsigned dma_block_kib{~0U};
    void sample() {
        free_kib = std::min(free_kib, static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U));
        dma_block_kib = std::min(
            dma_block_kib,
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) / 1024U));
    }
};

}  // namespace

Esp32HttpsClient::~Esp32HttpsClient() {
    for (auto& kept : kept_) drop(kept);
}

void Esp32HttpsClient::drop(Kept& kept) {
    if (kept.client != nullptr) {
        auto client = static_cast<esp_http_client_handle_t>(kept.client);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    kept.client = nullptr;
    kept.origin.clear();
}

HttpsResponse Esp32HttpsClient::post(const HttpsRequest& request, const HttpsChunk& on_chunk) {
    cancelled_.store(false);
    if (request.url.rfind("https://", 0) != 0) {
        HttpsResponse response;
        response.error = "NETWORK";
        return response;
    }
    const std::string origin = origin_of(request.url);
    Kept* kept = nullptr;
    for (auto& candidate : kept_) {
        if (candidate.client != nullptr && candidate.origin == origin) kept = &candidate;
    }
    const bool reuse = kept != nullptr;
    if (!reuse) {
        kept = &kept_[next_];
        next_ = (next_ + 1) % kept_.size();
        drop(*kept);
    }
    bool transport_failed = false;
    HttpsResponse response = exchange(request, on_chunk, *kept, origin, reuse, transport_failed);
    // A kept connection the server closed meanwhile fails before any answer
    // (nothing was streamed): the request goes again, once, on a new one.
    if (transport_failed && reuse && !cancelled_.load()) {
        response = exchange(request, on_chunk, *kept, origin, false, transport_failed);
    }
    return response;
}

HttpsResponse Esp32HttpsClient::exchange(const HttpsRequest& request, const HttpsChunk& on_chunk, Kept& kept,
                                         const std::string& origin, bool reuse, bool& transport_failed) {
    HttpsResponse response;
    transport_failed = false;
    esp_http_client_handle_t client = nullptr;
    if (reuse) {
        client = static_cast<esp_http_client_handle_t>(kept.client);
        if (esp_http_client_set_url(client, request.url.c_str()) != ESP_OK) {
            drop(kept);
            transport_failed = true;
            response.error = "NETWORK";
            return response;
        }
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_timeout_ms(client, kReadTimeoutMs);
    } else {
        drop(kept);
        esp_http_client_config_t config{};
        config.url = request.url.c_str();
        config.method = HTTP_METHOD_POST;
        config.timeout_ms = kReadTimeoutMs;
        config.crt_bundle_attach = esp_crt_bundle_attach;
        config.buffer_size = 4096;
        config.buffer_size_tx = 2048;
        config.keep_alive_enable = true;
        config.disable_auto_redirect = true;
        client = esp_http_client_init(&config);
        if (client == nullptr) {
            response.error = "NETWORK";
            return response;
        }
        kept.client = client;
        kept.origin = origin;
    }
    for (const auto& header : request.headers) {
        esp_http_client_set_header(client, header.name.c_str(), header.value.c_str());
    }
    const std::int64_t started = now_ms();
    const std::int64_t deadline = started + request.timeout_ms;
    // One line a request, as Aoraton's client logs its own: whether the
    // connection was kept, how long it took to connect, to send the body and to
    // hear back, what came back and the internal RAM left meanwhile (the first
    // questions on the board waited ~40 s, 2026-11-16).
    std::int64_t opened = -1;
    std::int64_t uploaded = -1;
    std::int64_t answered = -1;
    std::size_t sent = 0;
    std::size_t received = 0;
    InternalRam ram;
    ram.sample();
    // `whole`: the exchange ended with the body read to its end, so the
    // connection may serve the next request. The request's headers, which carry
    // a key, leave the kept connection at once.
    const auto finish = [&](const char* error, bool whole) {
        ram.sample();
        for (const auto& header : request.headers) esp_http_client_delete_header(client, header.name.c_str());
        if (!whole || error != nullptr) drop(kept);
        if (error != nullptr && response.error.empty()) response.error = error;
        if (error != nullptr && !on_chunk) response.body.clear();
        const auto since = [started](std::int64_t at) { return static_cast<long long>(at < 0 ? -1 : at - started); };
        ESP_LOGI(kTag,
                 "https POST %s%s -> %d%s%s, sent %u of %u, got %u bytes; open %lld, sent %lld, headers %lld, "
                 "total %lld ms; internal %u KiB, DMA block %u KiB",
                 shown_url(request.url).c_str(), reuse ? " (kept)" : "", response.status,
                 error != nullptr ? " " : "", error != nullptr ? error : "", static_cast<unsigned>(sent),
                 static_cast<unsigned>(request.body.size()), static_cast<unsigned>(received), since(opened),
                 since(uploaded), since(answered), static_cast<long long>(now_ms() - started), ram.free_kib,
                 ram.dma_block_kib);
        return response;
    };
    if (esp_http_client_open(client, static_cast<int>(request.body.size())) != ESP_OK) {
        transport_failed = !cancelled_.load();
        return finish(cancelled_.load() ? "CANCELLED" : "NETWORK", false);
    }
    opened = now_ms();
    ram.sample();
    while (sent < request.body.size()) {
        const int written = esp_http_client_write(client, request.body.data() + sent,
                                                  static_cast<int>(request.body.size() - sent));
        if (written <= 0 || cancelled_.load()) {
            transport_failed = !cancelled_.load();
            return finish(cancelled_.load() ? "CANCELLED" : "NETWORK", false);
        }
        sent += static_cast<std::size_t>(written);
    }
    uploaded = now_ms();
    ram.sample();
    if (esp_http_client_fetch_headers(client) < 0) {
        transport_failed = !cancelled_.load();
        return finish("NETWORK", false);
    }
    answered = now_ms();
    response.status = esp_http_client_get_status_code(client);
    char buffer[2048];
    for (;;) {
        if (cancelled_.load()) {
            response.status = 0;
            return finish("CANCELLED", false);
        }
        if (now_ms() > deadline) {
            response.status = 0;
            return finish("TIMEOUT", false);
        }
        const int read = esp_http_client_read(client, buffer, sizeof(buffer));
        if (read == -ESP_ERR_HTTP_EAGAIN) continue;  // nothing within a read: check cancel and the deadline
        if (read == 0) break;                         // the end of the body
        if (read < 0) {
            response.status = 0;
            return finish("NETWORK", false);
        }
        received += static_cast<std::size_t>(read);
        if (on_chunk) {
            // The reader has what it wanted: the rest stays unread, so the
            // connection cannot serve another request.
            if (!on_chunk(std::string_view(buffer, static_cast<std::size_t>(read)))) return finish(nullptr, false);
        } else {
            if (response.body.size() + static_cast<std::size_t>(read) > request.max_body_bytes) {
                response.status = 0;
                return finish("RESPONSE_TOO_LARGE", false);
            }
            response.body.append(buffer, static_cast<std::size_t>(read));
        }
    }
    return finish(nullptr, esp_http_client_is_complete_data_received(client));
}

}  // namespace aura::firmware

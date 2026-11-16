#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <string>

#include "aura/https_client.hpp"

namespace aura::firmware {

// HTTPS POSTs for the device's own assistant (Groq, Azure), with
// esp_http_client and the certificate bundle. One request at a time, on the
// cloud worker; cancel() may come from the UI loop and ends the request at its
// next read.
//
// A connection to each of the two servers stays open after a whole exchange,
// as Aoraton's client keeps its own: a TLS handshake took 1.3-2.6 s on the
// board, and a question made three (2026-11-16). One the server has closed
// meanwhile fails before any answer, and the request goes again, once, on a new
// connection.
class Esp32HttpsClient final : public IHttpsClient {
public:
    ~Esp32HttpsClient() override;
    HttpsResponse post(const HttpsRequest& request, const HttpsChunk& on_chunk) override;
    void cancel() override { cancelled_.store(true); }

private:
    struct Kept {
        void* client{nullptr};  // esp_http_client_handle_t
        std::string origin;
    };

    HttpsResponse exchange(const HttpsRequest& request, const HttpsChunk& on_chunk, Kept& kept,
                           const std::string& origin, bool reuse, bool& transport_failed);
    static void drop(Kept& kept);

    std::array<Kept, 2> kept_{};
    std::size_t next_{0};  // the slot a third server takes
    std::atomic<bool> cancelled_{false};
};

}  // namespace aura::firmware

#pragma once

#include <atomic>

#include "aura/https_client.hpp"

namespace aura::firmware {

// HTTPS POSTs for the device's own assistant (Groq, Azure), with
// esp_http_client and the certificate bundle. One request at a time, on the
// cloud worker; cancel() may come from the UI loop and ends the request at its
// next read. NOT VERIFIED ON HARDWARE.
class Esp32HttpsClient final : public IHttpsClient {
public:
    HttpsResponse post(const HttpsRequest& request, const HttpsChunk& on_chunk) override;
    void cancel() override { cancelled_.store(true); }

private:
    std::atomic<bool> cancelled_{false};
};

}  // namespace aura::firmware

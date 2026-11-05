#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace aura {

struct HttpsHeader {
    std::string name;
    std::string value;
};

struct HttpsRequest {
    std::string url;  // https:// only
    std::vector<HttpsHeader> headers;  // keys travel only here, never in the URL or the body
    std::string body;
    std::uint32_t timeout_ms{45000};
    std::size_t max_body_bytes{2 * 1024 * 1024};  // a longer response fails with RESPONSE_TOO_LARGE
};

struct HttpsResponse {
    int status{0};       // 0: no response
    std::string body;    // empty when the body was streamed
    std::string error;   // with status 0: TIMEOUT, CANCELLED, NETWORK or RESPONSE_TOO_LARGE
};

// Each piece of a streamed body as it arrives; returning false ends the request.
using HttpsChunk = std::function<bool(std::string_view)>;

// One HTTPS POST, made by the platform (firmware: esp_http_client with the
// certificate bundle). It blocks, so it runs on a worker, never the UI thread.
class IHttpsClient {
public:
    virtual ~IHttpsClient() = default;
    virtual HttpsResponse post(const HttpsRequest& request, const HttpsChunk& on_chunk) = 0;
    // Ends the request in progress, from another thread.
    virtual void cancel() = 0;
};

}  // namespace aura

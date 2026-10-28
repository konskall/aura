#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace aura::simulator {

struct BridgeResponse {
    int status{0};  // 0: the bridge could not be reached
    std::string body;
    std::string content_type;
};

// Loopback-only HTTP to the local bridge (127.0.0.1), bearer token, no proxy.
BridgeResponse bridge_request(const wchar_t* method, std::uint16_t port, const std::string& path,
                              const std::string& token, const std::string& body, const char* content_type,
                              int timeout_ms, std::size_t max_bytes);

// Same authenticated loopback request, but each network chunk of a 200 body
// is exposed to the provider worker as it arrives and is not kept in `body`.
// Returning false stops the download; `max_bytes` bounds the whole stream.
BridgeResponse bridge_stream_request(const wchar_t* method, std::uint16_t port, const std::string& path,
                                     const std::string& token, const std::string& body, const char* content_type,
                                     int timeout_ms, std::size_t max_bytes,
                                     const std::function<bool(std::string_view)>& on_chunk);

// The string value of `key` in a flat JSON response; control characters and
// unpaired surrogates are dropped (the text is untrusted display text).
std::string json_string(const std::string& json, const std::string& key);
// The boolean value of `key`, or `fallback` when absent.
bool json_bool(const std::string& json, const std::string& key, bool fallback = false);

}  // namespace aura::simulator

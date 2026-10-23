#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

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

// The string value of `key` in a flat JSON response; control characters and
// unpaired surrogates are dropped (the text is untrusted display text).
std::string json_string(const std::string& json, const std::string& key);
// The boolean value of `key`, or `fallback` when absent.
bool json_bool(const std::string& json, const std::string& key, bool fallback = false);

}  // namespace aura::simulator

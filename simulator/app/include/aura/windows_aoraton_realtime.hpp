#pragma once
#include "aura/aoraton_realtime.hpp"
#include <memory>

namespace aura::simulator {
// Native TLS WebSocket. One network owner and one blocking receiver; bounded
// invalidations only. No LVGL, persistence or private payload logging.
class WindowsAoratonRealtime final : public IAoratonRealtime {
public:
    WindowsAoratonRealtime();
    ~WindowsAoratonRealtime() override;
    void configure(const std::string& base_url, const std::string& anon_key, const std::string& access_token,
                   const std::vector<std::string>& rooms) override;
    AoratonRealtimeUpdate poll() override;
    void stop() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace aura::simulator

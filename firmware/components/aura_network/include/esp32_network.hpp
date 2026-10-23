#pragma once

#include "aura/wifi.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "esp_event.h"
#include "esp_timer.h"

namespace aura::firmware {

// Results handed from the Wi-Fi/event tasks to the UI loop, which applies
// them to the shared WifiModel under the display lock. No LVGL access here.
struct WifiEvent {
    enum class Kind { ScanDone, ScanFailed, ConnectSucceeded, ConnectFailed, LinkChanged };
    Kind kind{Kind::LinkChanged};
    std::vector<WifiNetwork> networks;
    std::string ssid;
    std::string ip_address;
    std::string error_code;
    bool connected{false};
    int rssi_dbm{-100};
};

// Wi-Fi station for HTTPS services (weather today, Aoraton later) plus SNTP.
// Credentials live in NVS namespace "aura_wifi" (keys "ssid" and optional
// "pass") and are written only after a join has obtained an IP address; the
// driver's own flash storage is disabled so a failed password is never kept.
class Esp32Network {
public:
    // Requires nvs_flash_init(). Starts the radio even when nothing is
    // provisioned so networks can be scanned and joined from the screen.
    bool start();
    bool radio_ready() const noexcept { return radio_ready_; }
    bool connected() const noexcept { return connected_.load(); }
    // 0 when disconnected, otherwise 1..3 derived from the AP RSSI.
    int signal_level() const;
    std::string saved_ssid() const;

    bool request_scan();
    // Validated by the shared WifiModel before this call.
    bool request_connect(const std::string& ssid, WifiSecurity security, const std::string& password);
    void cancel_connect();
    void forget();
    bool poll(WifiEvent& event);
    // True once SNTP has set a plausible wall-clock time.
    bool time_valid() const;

private:
    static void on_event(void* context, esp_event_base_t base, std::int32_t id, void* data);
    static void on_attempt_timeout(void* context);
    static void on_reconnect_timer(void* context);
    void push(WifiEvent event);
    void finish_scan(bool ok);
    void handle_disconnect(std::uint8_t reason);
    void handle_got_ip(const std::string& ip_address);
    void fail_attempt(const char* code);
    void clear_attempt_secret();
    bool connect_saved();
    void schedule_reconnect();
    void start_time_sync();

    mutable std::mutex mutex_;
    std::deque<WifiEvent> events_;
    std::atomic<bool> connected_{false};
    bool radio_ready_{false};
    bool time_sync_started_{false};
    // A join typed on the screen (guarded by mutex_).
    bool attempt_active_{false};
    std::string attempt_ssid_;
    WifiSecurity attempt_security_{WifiSecurity::Unknown};
    std::array<char, 65> attempt_password_{};
    int attempt_retries_{0};
    std::string link_ssid_;
    std::uint32_t reconnect_delay_ms_{1000};
    esp_timer_handle_t attempt_timer_{nullptr};
    esp_timer_handle_t reconnect_timer_{nullptr};
};

}  // namespace aura::firmware

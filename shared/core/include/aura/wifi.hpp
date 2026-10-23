#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aura {

constexpr std::size_t kWifiNetworkCapacity = 12;
constexpr std::size_t kWifiSsidMaxBytes = 32;
constexpr std::size_t kWifiPassphraseMinBytes = 8;
constexpr std::size_t kWifiPassphraseMaxBytes = 63;
constexpr std::size_t kWifiPskHexBytes = 64;

enum class WifiSecurity {
    Open,
    WpaPersonal,
    Wpa3Personal,
    Wep,
    Enterprise,
    Unknown,
};

enum class WifiLinkState {
    Unavailable,   // no radio/adapter, or the driver failed
    Disconnected,  // radio ready, not associated
    Connecting,
    Connected,
};

// Where a value came from. The simulator never claims a device association it
// did not make: a host-PC link is `Host`, a desktop stand-in is `Simulated`.
enum class WifiOrigin {
    Live,
    Host,
    Simulated,
    Demo,
};

enum class WifiPasswordCheck {
    Ok,
    NotRequired,
    TooShort,
    TooLong,
    InvalidCharacters,
    UnsupportedSecurity,
};

struct WifiNetwork {
    std::string ssid;
    int rssi_dbm{-100};
    WifiSecurity security{WifiSecurity::Unknown};
    bool connected{false};
    bool saved{false};
};

// Untrusted radio data is validated and bounded before it reaches this
// snapshot. Passwords never enter it: they go straight to the platform adapter.
struct WifiSnapshot {
    WifiLinkState link{WifiLinkState::Unavailable};
    WifiOrigin link_origin{WifiOrigin::Live};
    std::string connected_ssid;
    int connected_rssi_dbm{-100};
    WifiSecurity connected_security{WifiSecurity::Unknown};
    std::string ip_address;
    std::string saved_ssid;
    std::string target_ssid;
    WifiSecurity target_security{WifiSecurity::Unknown};
    bool scanning{false};
    bool scanned{false};
    WifiOrigin scan_origin{WifiOrigin::Live};
    std::array<WifiNetwork, kWifiNetworkCapacity> networks{};
    std::size_t network_count{0};
    std::string scan_error;
    std::string connect_error;
    std::string unavailable_reason;
    std::uint64_t sequence{0};
};

struct WifiScanResult {
    bool ok{false};
    std::vector<WifiNetwork> networks;
    std::string error_code;
};

struct WifiConnectResult {
    bool ok{false};
    std::string ssid;
    std::string ip_address;
    std::string error_code;
};

class WifiModel {
public:
    const WifiSnapshot& snapshot() const noexcept { return snapshot_; }

    void set_radio_available(bool available, std::string reason = {});
    bool begin_scan();
    void scan_succeeded(const std::vector<WifiNetwork>& networks, WifiOrigin origin);
    void scan_failed(std::string error_code);
    // Validates the typed request (SSID, security, password rules) before any
    // platform work starts; on rejection the reason is in `connect_error`.
    bool begin_connect(const std::string& ssid, WifiSecurity security, std::string_view password);
    void connect_succeeded(const std::string& ssid, std::string ip_address, WifiOrigin origin);
    void connect_failed(std::string error_code);
    void cancel_connect();
    void set_link(bool connected, const std::string& ssid, int rssi_dbm, std::string ip_address,
                  WifiOrigin origin);
    void set_saved_network(const std::string& ssid);
    void forget_saved_network();

private:
    void refresh_flags();
    void touch() noexcept { ++snapshot_.sequence; }

    WifiSnapshot snapshot_{};
};

bool wifi_ssid_valid(std::string_view ssid) noexcept;
WifiPasswordCheck wifi_check_password(WifiSecurity security, std::string_view password) noexcept;
bool wifi_security_supported(WifiSecurity security) noexcept;
// Display-safe SSID: invalid UTF-8 and control bytes become '?'.
std::string wifi_display_name(std::string_view ssid);
// 1..3 bars for a visible network (0 only for "no signal").
int wifi_signal_bars(int rssi_dbm) noexcept;
// Drops hidden/invalid entries, keeps the strongest duplicate, marks the
// connected/saved network, orders connected > saved > signal, bounds the list.
std::vector<WifiNetwork> wifi_normalize_networks(std::vector<WifiNetwork> networks,
                                                 const std::string& connected_ssid,
                                                 const std::string& saved_ssid);
const char* wifi_link_state_name(WifiLinkState state) noexcept;
const char* wifi_security_name(WifiSecurity security) noexcept;

}  // namespace aura

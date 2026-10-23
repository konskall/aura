#pragma once

#include "aura/wifi.hpp"

#include <future>
#include <string>

namespace aura::simulator {

// What the host PC's own Wi-Fi adapter reports. The simulator never changes
// the host connection: scans are real (read-only), while a "connect" in the
// simulator is a labelled stand-in because the password can only be checked
// by the device's radio.
struct WifiHostScan {
    WifiScanResult scan;
    bool adapter_present{false};
    bool host_connected{false};
    std::string host_ssid;
    int host_rssi_dbm{-100};
    WifiSecurity host_security{WifiSecurity::Unknown};
};

class WindowsWifiService {
public:
    // Fast, synchronous adapter probe (no scan); used once at start-up.
    static WifiHostScan probe();
    // Triggers a fresh scan and returns the visible networks plus the host link.
    std::future<WifiHostScan> scan();
};

}  // namespace aura::simulator

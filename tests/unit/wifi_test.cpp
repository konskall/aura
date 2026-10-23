#include "aura/wifi.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}  // namespace

int main() {
    using namespace aura;

    // Typed validation shared by simulator and firmware.
    expect(wifi_ssid_valid("Σπίτι"), "UTF-8 SSID accepted");
    expect(!wifi_ssid_valid(""), "empty SSID rejected");
    expect(!wifi_ssid_valid(std::string(33, 'a')), "33-byte SSID rejected");
    expect(!wifi_ssid_valid(std::string("ab\0cd", 5)), "embedded NUL rejected");
    expect(wifi_check_password(WifiSecurity::Open, "") == WifiPasswordCheck::NotRequired, "open needs no password");
    expect(wifi_check_password(WifiSecurity::WpaPersonal, "short") == WifiPasswordCheck::TooShort, "short passphrase");
    expect(wifi_check_password(WifiSecurity::WpaPersonal, "correct horse") == WifiPasswordCheck::Ok, "passphrase ok");
    expect(wifi_check_password(WifiSecurity::Wpa3Personal, std::string(64, 'A')) == WifiPasswordCheck::Ok,
           "64 hex digits are a raw PSK");
    expect(wifi_check_password(WifiSecurity::WpaPersonal, std::string(64, 'z')) == WifiPasswordCheck::TooLong,
           "64 non-hex characters rejected");
    expect(wifi_check_password(WifiSecurity::WpaPersonal, "κωδικός123") == WifiPasswordCheck::InvalidCharacters,
           "non-ASCII passphrase rejected");
    expect(wifi_check_password(WifiSecurity::Enterprise, "whatever1") == WifiPasswordCheck::UnsupportedSecurity,
           "enterprise unsupported");
    expect(wifi_check_password(WifiSecurity::Wep, "12345678") == WifiPasswordCheck::UnsupportedSecurity,
           "WEP unsupported");

    // Untrusted names are made display-safe.
    expect(wifi_display_name("Καφέ Wi-Fi") == "Καφέ Wi-Fi", "valid UTF-8 kept");
    expect(wifi_display_name(std::string("a\x01" "b", 3)) == "a?b", "control byte replaced");
    expect(wifi_display_name(std::string("\xC3\x28", 2)) == "?(", "invalid UTF-8 replaced");
    expect(wifi_display_name("\xE2\x80\xAE" "evil") == "?evil", "bidi override replaced");
    expect(wifi_signal_bars(-50) == 3 && wifi_signal_bars(-70) == 2 && wifi_signal_bars(-90) == 1, "signal bars");

    // Normalization: hidden dropped, duplicates merged, ordering, bounded.
    std::vector<WifiNetwork> raw{
        {"Γραφείο", -80, WifiSecurity::WpaPersonal},
        {"", -30, WifiSecurity::Open},
        {"Σπίτι", -70, WifiSecurity::WpaPersonal},
        {"Σπίτι", -45, WifiSecurity::WpaPersonal},
        {"Καφέ", -55, WifiSecurity::Open},
    };
    for (int index = 0; index < 20; ++index) raw.push_back({"Extra " + std::to_string(index), -95, WifiSecurity::Unknown});
    const auto normalized = wifi_normalize_networks(raw, "Γραφείο", "Γραφείο");
    expect(normalized.size() == kWifiNetworkCapacity, "list bounded");
    expect(normalized[0].ssid == "Γραφείο" && normalized[0].connected && normalized[0].saved, "connected first");
    expect(normalized[1].ssid == "Σπίτι" && normalized[1].rssi_dbm == -45, "duplicate keeps strongest");
    expect(normalized[2].ssid == "Καφέ", "then by signal");

    WifiModel model;
    expect(model.snapshot().link == WifiLinkState::Unavailable, "starts unavailable");
    expect(!model.begin_scan(), "no scan without a radio");
    model.set_radio_available(true);
    expect(model.snapshot().link == WifiLinkState::Disconnected, "radio ready");
    const auto ready_sequence = model.snapshot().sequence;
    model.set_radio_available(true);
    expect(model.snapshot().sequence == ready_sequence, "unchanged radio state does not churn");
    expect(model.begin_scan(), "scan starts");
    expect(!model.begin_scan(), "second scan rejected while scanning");
    model.scan_succeeded(raw, WifiOrigin::Live);
    expect(!model.snapshot().scanning && model.snapshot().scanned, "scan finished");
    expect(model.snapshot().network_count == kWifiNetworkCapacity, "scan stored bounded");

    expect(!model.begin_connect("Σπίτι", WifiSecurity::WpaPersonal, "1234"), "short password rejected");
    expect(model.snapshot().connect_error == "PASSWORD_TOO_SHORT", "rejection reason recorded");
    expect(model.snapshot().link == WifiLinkState::Disconnected, "rejected request does not connect");
    expect(model.begin_connect("Σπίτι", WifiSecurity::WpaPersonal, "correct horse"), "valid request starts");
    expect(model.snapshot().link == WifiLinkState::Connecting && model.snapshot().target_ssid == "Σπίτι",
           "connecting to target");
    expect(!model.begin_connect("Καφέ", WifiSecurity::Open, ""), "one connection at a time");
    model.set_link(true, "Γραφείο", -60, "10.0.0.2", WifiOrigin::Live);
    expect(model.snapshot().link == WifiLinkState::Connecting, "stray link updates ignored while connecting");
    model.connect_failed("WRONG_PASSWORD");
    expect(model.snapshot().link == WifiLinkState::Disconnected &&
           model.snapshot().connect_error == "WRONG_PASSWORD" && model.snapshot().saved_ssid.empty(),
           "failure keeps nothing saved");
    expect(model.begin_connect("Σπίτι", WifiSecurity::WpaPersonal, "correct horse battery"), "retry starts");
    model.connect_succeeded("Σπίτι", "192.168.1.20", WifiOrigin::Live);
    expect(model.snapshot().link == WifiLinkState::Connected && model.snapshot().connected_ssid == "Σπίτι" &&
           model.snapshot().saved_ssid == "Σπίτι" && model.snapshot().ip_address == "192.168.1.20",
           "success connects and saves");
    expect(model.snapshot().networks[0].ssid == "Σπίτι" && model.snapshot().networks[0].connected,
           "connected network ordered first");
    const auto connected_sequence = model.snapshot().sequence;
    model.set_link(true, "Σπίτι", model.snapshot().connected_rssi_dbm, "192.168.1.20", WifiOrigin::Live);
    expect(model.snapshot().sequence == connected_sequence, "periodic identical status does not churn");
    model.forget_saved_network();
    expect(model.snapshot().saved_ssid.empty() && model.snapshot().link == WifiLinkState::Disconnected,
           "forget disconnects the device network");

    model.set_link(true, "Γραφείο", -58, {}, WifiOrigin::Host);
    model.set_saved_network("Γραφείο");
    model.forget_saved_network();
    expect(model.snapshot().link == WifiLinkState::Connected, "a host link is never disconnected by forget");

    expect(model.begin_connect("Καφέ", WifiSecurity::Open, ""), "open network needs no password");
    model.cancel_connect();
    expect(model.snapshot().link == WifiLinkState::Disconnected && model.snapshot().target_ssid.empty(),
           "cancel abandons the request");
    model.set_radio_available(false, "NO_ADAPTER");
    expect(model.snapshot().link == WifiLinkState::Unavailable &&
           model.snapshot().unavailable_reason == "NO_ADAPTER", "radio loss recorded");

    std::cout << "wifi_test: PASS\n";
    return 0;
}

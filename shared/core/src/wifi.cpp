#include "aura/wifi.hpp"

#include <algorithm>
#include <utility>

namespace aura {

namespace {

bool is_hex(char ch) noexcept {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

// Length of the well-formed UTF-8 sequence at `index`, or 0 when it is invalid.
std::size_t utf8_sequence(std::string_view text, std::size_t index, std::uint32_t& code) noexcept {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::size_t length = 0;
    std::uint32_t minimum = 0;
    if (lead < 0x80) {
        code = lead;
        return 1;
    }
    if ((lead & 0xE0U) == 0xC0U) {
        length = 2;
        minimum = 0x80;
        code = lead & 0x1FU;
    } else if ((lead & 0xF0U) == 0xE0U) {
        length = 3;
        minimum = 0x800;
        code = lead & 0x0FU;
    } else if ((lead & 0xF8U) == 0xF0U) {
        length = 4;
        minimum = 0x10000;
        code = lead & 0x07U;
    } else {
        return 0;
    }
    if (index + length > text.size()) return 0;
    for (std::size_t offset = 1; offset < length; ++offset) {
        const auto next = static_cast<unsigned char>(text[index + offset]);
        if ((next & 0xC0U) != 0x80U) return 0;
        code = (code << 6U) | (next & 0x3FU);
    }
    if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return 0;
    return length;
}

// Controls and bidirectional overrides could disguise one network as another.
bool display_unsafe(std::uint32_t code) noexcept {
    return code < 0x20 || (code >= 0x7F && code <= 0x9F) || (code >= 0x200B && code <= 0x200F) ||
           (code >= 0x202A && code <= 0x202E) || (code >= 0x2066 && code <= 0x2069) || code == 0xFEFF;
}

void order_networks(std::vector<WifiNetwork>& networks) {
    std::stable_sort(networks.begin(), networks.end(), [](const WifiNetwork& left, const WifiNetwork& right) {
        if (left.connected != right.connected) return left.connected;
        if (left.saved != right.saved) return left.saved;
        return left.rssi_dbm > right.rssi_dbm;
    });
}

}  // namespace

bool wifi_ssid_valid(std::string_view ssid) noexcept {
    return !ssid.empty() && ssid.size() <= kWifiSsidMaxBytes && ssid.find('\0') == std::string_view::npos;
}

bool wifi_security_supported(WifiSecurity security) noexcept {
    return security == WifiSecurity::Open || security == WifiSecurity::WpaPersonal ||
           security == WifiSecurity::Wpa3Personal;
}

WifiPasswordCheck wifi_check_password(WifiSecurity security, std::string_view password) noexcept {
    if (security == WifiSecurity::Open) return WifiPasswordCheck::NotRequired;
    if (!wifi_security_supported(security)) return WifiPasswordCheck::UnsupportedSecurity;
    // WPA/WPA2/WPA3 personal: an 8..63 character printable-ASCII passphrase,
    // or a raw 64-digit hexadecimal PSK.
    if (password.size() < kWifiPassphraseMinBytes) return WifiPasswordCheck::TooShort;
    if (password.size() == kWifiPskHexBytes) {
        return std::all_of(password.begin(), password.end(), is_hex) ? WifiPasswordCheck::Ok
                                                                      : WifiPasswordCheck::TooLong;
    }
    if (password.size() > kWifiPassphraseMaxBytes) return WifiPasswordCheck::TooLong;
    for (const char ch : password) {
        const auto value = static_cast<unsigned char>(ch);
        if (value < 0x20 || value > 0x7E) return WifiPasswordCheck::InvalidCharacters;
    }
    return WifiPasswordCheck::Ok;
}

std::string wifi_display_name(std::string_view ssid) {
    std::string result;
    result.reserve(ssid.size());
    std::size_t index = 0;
    while (index < ssid.size()) {
        std::uint32_t code = 0;
        const auto length = utf8_sequence(ssid, index, code);
        if (length == 0 || display_unsafe(code)) {
            result.push_back('?');
            index += length == 0 ? 1 : length;
            continue;
        }
        result.append(ssid.substr(index, length));
        index += length;
    }
    return result;
}

int wifi_signal_bars(int rssi_dbm) noexcept {
    if (rssi_dbm >= -60) return 3;
    return rssi_dbm >= -75 ? 2 : 1;
}

std::vector<WifiNetwork> wifi_normalize_networks(std::vector<WifiNetwork> networks,
                                                 const std::string& connected_ssid,
                                                 const std::string& saved_ssid) {
    std::vector<WifiNetwork> result;
    result.reserve(std::min<std::size_t>(networks.size(), kWifiNetworkCapacity * 4));
    for (auto& network : networks) {
        if (!wifi_ssid_valid(network.ssid)) continue;
        network.rssi_dbm = std::clamp(network.rssi_dbm, -120, 0);
        const auto existing = std::find_if(result.begin(), result.end(), [&](const WifiNetwork& value) {
            return value.ssid == network.ssid;
        });
        if (existing != result.end()) {
            // Several access points (or bands) share one SSID; show it once.
            if (network.rssi_dbm > existing->rssi_dbm) existing->rssi_dbm = network.rssi_dbm;
            if (existing->security == WifiSecurity::Unknown) existing->security = network.security;
            continue;
        }
        result.push_back(std::move(network));
    }
    for (auto& network : result) {
        network.connected = !connected_ssid.empty() && network.ssid == connected_ssid;
        network.saved = !saved_ssid.empty() && network.ssid == saved_ssid;
    }
    order_networks(result);
    if (result.size() > kWifiNetworkCapacity) result.resize(kWifiNetworkCapacity);
    return result;
}

void WifiModel::set_radio_available(bool available, std::string reason) {
    if (available) {
        if (snapshot_.link != WifiLinkState::Unavailable) return;
        snapshot_.link = WifiLinkState::Disconnected;
        snapshot_.unavailable_reason.clear();
    } else {
        if (snapshot_.link == WifiLinkState::Unavailable && snapshot_.unavailable_reason == reason) return;
        snapshot_.link = WifiLinkState::Unavailable;
        snapshot_.unavailable_reason = std::move(reason);
        snapshot_.scanning = false;
        snapshot_.connected_ssid.clear();
        snapshot_.ip_address.clear();
        snapshot_.target_ssid.clear();
    }
    refresh_flags();
    touch();
}

bool WifiModel::begin_scan() {
    if (snapshot_.link == WifiLinkState::Unavailable || snapshot_.scanning) return false;
    snapshot_.scanning = true;
    snapshot_.scan_error.clear();
    touch();
    return true;
}

void WifiModel::scan_succeeded(const std::vector<WifiNetwork>& networks, WifiOrigin origin) {
    const auto normalized = wifi_normalize_networks(networks, snapshot_.connected_ssid, snapshot_.saved_ssid);
    snapshot_.networks = {};
    snapshot_.network_count = normalized.size();
    std::copy(normalized.begin(), normalized.end(), snapshot_.networks.begin());
    snapshot_.scanning = false;
    snapshot_.scanned = true;
    snapshot_.scan_origin = origin;
    snapshot_.scan_error.clear();
    touch();
}

void WifiModel::scan_failed(std::string error_code) {
    snapshot_.scanning = false;
    snapshot_.scan_error = error_code.empty() ? "SCAN_FAILED" : std::move(error_code);
    touch();
}

bool WifiModel::begin_connect(const std::string& ssid, WifiSecurity security, std::string_view password) {
    const auto reject = [&](const char* code) {
        snapshot_.connect_error = code;
        touch();
        return false;
    };
    if (snapshot_.link == WifiLinkState::Unavailable) return reject("RADIO_UNAVAILABLE");
    if (snapshot_.link == WifiLinkState::Connecting) return reject("BUSY");
    if (!wifi_ssid_valid(ssid)) return reject("INVALID_SSID");
    switch (wifi_check_password(security, password)) {
        case WifiPasswordCheck::Ok:
        case WifiPasswordCheck::NotRequired: break;
        case WifiPasswordCheck::TooShort: return reject("PASSWORD_TOO_SHORT");
        case WifiPasswordCheck::TooLong: return reject("PASSWORD_TOO_LONG");
        case WifiPasswordCheck::InvalidCharacters: return reject("PASSWORD_INVALID");
        case WifiPasswordCheck::UnsupportedSecurity: return reject("UNSUPPORTED_SECURITY");
    }
    snapshot_.link = WifiLinkState::Connecting;
    snapshot_.target_ssid = ssid;
    snapshot_.target_security = security;
    snapshot_.connected_ssid.clear();
    snapshot_.ip_address.clear();
    snapshot_.connect_error.clear();
    refresh_flags();
    touch();
    return true;
}

void WifiModel::connect_succeeded(const std::string& ssid, std::string ip_address, WifiOrigin origin) {
    if (snapshot_.link != WifiLinkState::Connecting || ssid != snapshot_.target_ssid) return;
    snapshot_.link = WifiLinkState::Connected;
    snapshot_.link_origin = origin;
    snapshot_.connected_ssid = ssid;
    snapshot_.connected_security = snapshot_.target_security;
    snapshot_.ip_address = std::move(ip_address);
    for (std::size_t index = 0; index < snapshot_.network_count; ++index) {
        if (snapshot_.networks[index].ssid == ssid) snapshot_.connected_rssi_dbm = snapshot_.networks[index].rssi_dbm;
    }
    // A completed association is what saves a network; a typed password that
    // never connected is never remembered.
    snapshot_.saved_ssid = ssid;
    snapshot_.target_ssid.clear();
    snapshot_.connect_error.clear();
    refresh_flags();
    touch();
}

void WifiModel::connect_failed(std::string error_code) {
    if (snapshot_.link != WifiLinkState::Connecting) return;
    snapshot_.link = WifiLinkState::Disconnected;
    snapshot_.connect_error = error_code.empty() ? "CONNECT_FAILED" : std::move(error_code);
    refresh_flags();
    touch();
}

void WifiModel::cancel_connect() {
    if (snapshot_.link != WifiLinkState::Connecting) return;
    snapshot_.link = WifiLinkState::Disconnected;
    snapshot_.target_ssid.clear();
    snapshot_.connect_error.clear();
    refresh_flags();
    touch();
}

void WifiModel::set_link(bool connected, const std::string& ssid, int rssi_dbm, std::string ip_address,
                         WifiOrigin origin) {
    // While a typed request runs, only its own result may change the link.
    if (snapshot_.link == WifiLinkState::Unavailable || snapshot_.link == WifiLinkState::Connecting) return;
    const auto link = connected ? WifiLinkState::Connected : WifiLinkState::Disconnected;
    const std::string next_ssid = connected && wifi_ssid_valid(ssid) ? ssid : std::string{};
    if (!connected) ip_address.clear();
    if (snapshot_.link == link && snapshot_.connected_ssid == next_ssid &&
        snapshot_.connected_rssi_dbm == rssi_dbm && snapshot_.ip_address == ip_address &&
        snapshot_.link_origin == origin) {
        return;
    }
    snapshot_.link = link;
    snapshot_.link_origin = origin;
    snapshot_.connected_ssid = next_ssid;
    snapshot_.connected_rssi_dbm = connected ? std::clamp(rssi_dbm, -120, 0) : -100;
    snapshot_.ip_address = std::move(ip_address);
    if (connected) {
        for (std::size_t index = 0; index < snapshot_.network_count; ++index) {
            if (snapshot_.networks[index].ssid == next_ssid) {
                snapshot_.connected_security = snapshot_.networks[index].security;
            }
        }
    }
    refresh_flags();
    touch();
}

void WifiModel::set_saved_network(const std::string& ssid) {
    const std::string next = wifi_ssid_valid(ssid) ? ssid : std::string{};
    if (snapshot_.saved_ssid == next) return;
    snapshot_.saved_ssid = next;
    refresh_flags();
    touch();
}

void WifiModel::forget_saved_network() {
    if (snapshot_.saved_ssid.empty()) return;
    if (snapshot_.link == WifiLinkState::Connected && snapshot_.link_origin != WifiOrigin::Host &&
        snapshot_.connected_ssid == snapshot_.saved_ssid) {
        snapshot_.link = WifiLinkState::Disconnected;
        snapshot_.connected_ssid.clear();
        snapshot_.ip_address.clear();
    }
    snapshot_.saved_ssid.clear();
    refresh_flags();
    touch();
}

void WifiModel::refresh_flags() {
    std::vector<WifiNetwork> networks(snapshot_.networks.begin(),
                                      snapshot_.networks.begin() + static_cast<std::ptrdiff_t>(snapshot_.network_count));
    for (auto& network : networks) {
        network.connected = snapshot_.link == WifiLinkState::Connected && network.ssid == snapshot_.connected_ssid;
        network.saved = !snapshot_.saved_ssid.empty() && network.ssid == snapshot_.saved_ssid;
    }
    order_networks(networks);
    std::copy(networks.begin(), networks.end(), snapshot_.networks.begin());
}

const char* wifi_link_state_name(WifiLinkState state) noexcept {
    switch (state) {
        case WifiLinkState::Unavailable: return "UNAVAILABLE";
        case WifiLinkState::Disconnected: return "DISCONNECTED";
        case WifiLinkState::Connecting: return "CONNECTING";
        case WifiLinkState::Connected: return "CONNECTED";
    }
    return "UNAVAILABLE";
}

const char* wifi_security_name(WifiSecurity security) noexcept {
    switch (security) {
        case WifiSecurity::Open: return "OPEN";
        case WifiSecurity::WpaPersonal: return "WPA2";
        case WifiSecurity::Wpa3Personal: return "WPA3";
        case WifiSecurity::Wep: return "WEP";
        case WifiSecurity::Enterprise: return "ENTERPRISE";
        case WifiSecurity::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

}  // namespace aura

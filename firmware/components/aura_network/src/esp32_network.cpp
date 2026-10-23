#include "esp32_network.hpp"

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "nvs.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <utility>

namespace aura::firmware {

namespace {

constexpr const char* kTag = "aura_network";
constexpr const char* kNamespace = "aura_wifi";
constexpr std::size_t kEventCapacity = 8;
constexpr std::size_t kScanRecordLimit = 20;
constexpr std::uint64_t kAttemptTimeoutUs = 20ULL * 1000ULL * 1000ULL;
constexpr std::uint32_t kReconnectMaxMs = 60000;
// AURA is Greek-first: Europe/Athens (EET/EEST). Other zones need provisioning.
constexpr const char* kTimezone = "EET-2EEST,M3.5.0/3,M10.5.0/4";
// 2024-01-01T00:00:00Z: anything earlier means SNTP has not set the clock yet.
constexpr std::time_t kPlausibleEpoch = 1704067200;

void clear_bytes(void* bytes, std::size_t size) {
    volatile auto* cursor = static_cast<volatile std::uint8_t*>(bytes);
    while (size-- > 0) *cursor++ = 0;
}

WifiSecurity security_from(wifi_auth_mode_t mode) {
    switch (mode) {
        case WIFI_AUTH_OPEN: return WifiSecurity::Open;
        case WIFI_AUTH_WEP: return WifiSecurity::Wep;
        case WIFI_AUTH_WPA_PSK:
        case WIFI_AUTH_WPA2_PSK:
        case WIFI_AUTH_WPA_WPA2_PSK: return WifiSecurity::WpaPersonal;
        case WIFI_AUTH_WPA3_PSK:
        case WIFI_AUTH_WPA2_WPA3_PSK: return WifiSecurity::Wpa3Personal;
        case WIFI_AUTH_WPA2_ENTERPRISE: return WifiSecurity::Enterprise;
        default: return WifiSecurity::Unknown;  // OWE, WAPI, WPA3-Enterprise, DPP
    }
}

std::string text_from(const std::uint8_t* bytes, std::size_t capacity) {
    const auto* text = reinterpret_cast<const char*>(bytes);
    return std::string(text, strnlen(text, capacity));
}

}  // namespace

bool Esp32Network::start() {
    bool ok = esp_netif_init() == ESP_OK;
    const esp_err_t loop = esp_event_loop_create_default();
    ok = ok && (loop == ESP_OK || loop == ESP_ERR_INVALID_STATE) && esp_netif_create_default_wifi_sta() != nullptr;
    if (ok) {
        wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
        // RAM storage: the driver must not persist a password that never
        // connected. AURA's own NVS entry is written after a successful join.
        ok = esp_wifi_init(&init) == ESP_OK && esp_wifi_set_storage(WIFI_STORAGE_RAM) == ESP_OK &&
             esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_event, this, nullptr) == ESP_OK &&
             esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_event, this, nullptr) == ESP_OK &&
             esp_wifi_set_mode(WIFI_MODE_STA) == ESP_OK;
    }
    if (ok) {
        esp_timer_create_args_t attempt{};
        attempt.callback = &on_attempt_timeout;
        attempt.arg = this;
        attempt.name = "aura_wifi_join";
        esp_timer_create_args_t reconnect{};
        reconnect.callback = &on_reconnect_timer;
        reconnect.arg = this;
        reconnect.name = "aura_wifi_retry";
        ok = esp_timer_create(&attempt, &attempt_timer_) == ESP_OK &&
             esp_timer_create(&reconnect, &reconnect_timer_) == ESP_OK && esp_wifi_start() == ESP_OK;
    }
    if (!ok) {
        ESP_LOGW(kTag, "Wi-Fi radio start failed");
        return false;
    }
    setenv("TZ", kTimezone, 1);
    tzset();
    radio_ready_ = true;
    ESP_LOGI(kTag, "Wi-Fi radio started (%s)", saved_ssid().empty() ? "not provisioned" : "provisioned");
    return true;
}

int Esp32Network::signal_level() const {
    if (!connected_.load()) return 0;
    wifi_ap_record_t access_point{};
    if (esp_wifi_sta_get_ap_info(&access_point) != ESP_OK) return 1;
    if (access_point.rssi >= -60) return 3;
    return access_point.rssi >= -75 ? 2 : 1;
}

std::string Esp32Network::saved_ssid() const {
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return {};
    char ssid[kWifiSsidMaxBytes + 1] = {};
    std::size_t size = sizeof(ssid);
    const bool found = nvs_get_str(handle, "ssid", ssid, &size) == ESP_OK;
    nvs_close(handle);
    return found ? std::string(ssid) : std::string{};
}

bool Esp32Network::request_scan() {
    if (!radio_ready_) return false;
    const esp_err_t result = esp_wifi_scan_start(nullptr, false);
    if (result == ESP_OK) return true;
    WifiEvent event;
    event.kind = WifiEvent::Kind::ScanFailed;
    event.error_code = result == ESP_ERR_WIFI_STATE ? "BUSY" : "SCAN_FAILED";
    push(std::move(event));
    return false;
}

bool Esp32Network::request_connect(const std::string& ssid, WifiSecurity security, const std::string& password) {
    if (!radio_ready_ || !wifi_ssid_valid(ssid) || password.size() >= attempt_password_.size()) return false;
    wifi_config_t config{};
    std::memcpy(config.sta.ssid, ssid.data(), ssid.size());
    std::memcpy(config.sta.password, password.data(), std::min(password.size(), sizeof(config.sta.password)));
    config.sta.threshold.authmode = security == WifiSecurity::Open ? WIFI_AUTH_OPEN :
        (security == WifiSecurity::Wpa3Personal ? WIFI_AUTH_WPA3_PSK : WIFI_AUTH_WPA2_PSK);
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        attempt_active_ = true;
        attempt_ssid_ = ssid;
        attempt_security_ = security;
        clear_attempt_secret();
        std::memcpy(attempt_password_.data(), password.data(), password.size());
        attempt_retries_ = 0;
    }
    if (reconnect_timer_) esp_timer_stop(reconnect_timer_);
    esp_wifi_disconnect();
    const bool started = esp_wifi_set_config(WIFI_IF_STA, &config) == ESP_OK && esp_wifi_connect() == ESP_OK;
    clear_bytes(&config, sizeof(config));
    if (!started) {
        fail_attempt("CONNECT_FAILED");
        return true;  // the failure is reported through poll()
    }
    if (attempt_timer_) {
        esp_timer_stop(attempt_timer_);
        esp_timer_start_once(attempt_timer_, kAttemptTimeoutUs);
    }
    return true;
}

void Esp32Network::cancel_connect() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!attempt_active_) return;
        attempt_active_ = false;
        clear_attempt_secret();
    }
    if (attempt_timer_) esp_timer_stop(attempt_timer_);
    esp_wifi_disconnect();
    if (!connect_saved()) {
        WifiEvent event;
        event.kind = WifiEvent::Kind::LinkChanged;
        push(std::move(event));
    }
}

void Esp32Network::forget() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        attempt_active_ = false;
        clear_attempt_secret();
        link_ssid_.clear();
    }
    if (attempt_timer_) esp_timer_stop(attempt_timer_);
    if (reconnect_timer_) esp_timer_stop(reconnect_timer_);
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_key(handle, "ssid");
        nvs_erase_key(handle, "pass");
        nvs_commit(handle);
        nvs_close(handle);
    }
    esp_wifi_disconnect();
    wifi_config_t empty{};
    esp_wifi_set_config(WIFI_IF_STA, &empty);
}

bool Esp32Network::poll(WifiEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.empty()) return false;
    event = std::move(events_.front());
    events_.pop_front();
    return true;
}

bool Esp32Network::time_valid() const {
    return std::time(nullptr) > kPlausibleEpoch;
}

void Esp32Network::push(WifiEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.size() >= kEventCapacity) events_.pop_front();
    events_.push_back(std::move(event));
}

void Esp32Network::finish_scan(bool ok) {
    WifiEvent event;
    std::uint16_t count = static_cast<std::uint16_t>(kScanRecordLimit);
    std::vector<wifi_ap_record_t> records(kScanRecordLimit);
    if (!ok || esp_wifi_scan_get_ap_records(&count, records.data()) != ESP_OK) {
        esp_wifi_clear_ap_list();
        event.kind = WifiEvent::Kind::ScanFailed;
        event.error_code = "SCAN_FAILED";
        push(std::move(event));
        return;
    }
    event.kind = WifiEvent::Kind::ScanDone;
    event.networks.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        const auto& record = records[index];
        WifiNetwork network;
        network.ssid = text_from(record.ssid, sizeof(record.ssid));
        network.rssi_dbm = record.rssi;
        network.security = security_from(record.authmode);
        event.networks.push_back(std::move(network));
    }
    push(std::move(event));
}

void Esp32Network::handle_disconnect(std::uint8_t reason) {
    connected_ = false;
    bool attempt = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        attempt = attempt_active_;
    }
    if (attempt) {
        // Our own esp_wifi_disconnect() before switching networks.
        if (reason == WIFI_REASON_ASSOC_LEAVE) return;
        switch (reason) {
            case WIFI_REASON_AUTH_FAIL:
            case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            case WIFI_REASON_HANDSHAKE_TIMEOUT:
            case WIFI_REASON_MIC_FAILURE:
                fail_attempt("WRONG_PASSWORD");
                return;
            case WIFI_REASON_NO_AP_FOUND:
                fail_attempt("NETWORK_NOT_FOUND");
                return;
            default: break;
        }
        int retries = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            retries = ++attempt_retries_;
        }
        if (retries <= 2) {
            esp_wifi_connect();
            return;
        }
        fail_attempt("CONNECT_FAILED");
        return;
    }
    WifiEvent event;
    event.kind = WifiEvent::Kind::LinkChanged;
    push(std::move(event));
    schedule_reconnect();
}

void Esp32Network::handle_got_ip(const std::string& ip_address) {
    connected_ = true;
    reconnect_delay_ms_ = 1000;
    wifi_ap_record_t access_point{};
    const bool have_ap = esp_wifi_sta_get_ap_info(&access_point) == ESP_OK;
    const std::string ssid = have_ap ? text_from(access_point.ssid, sizeof(access_point.ssid)) : std::string{};
    bool was_attempt = false;
    std::string attempt_ssid;
    std::array<char, 65> password{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (attempt_active_) {
            was_attempt = true;
            attempt_active_ = false;
            attempt_ssid = attempt_ssid_;
            password = attempt_password_;
            clear_attempt_secret();
        }
        link_ssid_ = ssid.empty() ? attempt_ssid : ssid;
    }
    WifiEvent event;
    event.ip_address = ip_address;
    event.rssi_dbm = have_ap ? access_point.rssi : -100;
    if (was_attempt) {
        if (attempt_timer_) esp_timer_stop(attempt_timer_);
        // Only a network that actually connected is remembered.
        nvs_handle_t handle = 0;
        if (nvs_open(kNamespace, NVS_READWRITE, &handle) == ESP_OK) {
            nvs_set_str(handle, "ssid", attempt_ssid.c_str());
            if (password[0] != '\0') nvs_set_str(handle, "pass", password.data());
            else nvs_erase_key(handle, "pass");
            nvs_commit(handle);
            nvs_close(handle);
        }
        clear_bytes(password.data(), password.size());
        event.kind = WifiEvent::Kind::ConnectSucceeded;
        event.ssid = attempt_ssid;
    } else {
        event.kind = WifiEvent::Kind::LinkChanged;
        event.connected = true;
        event.ssid = ssid;
    }
    push(std::move(event));
    start_time_sync();
}

void Esp32Network::fail_attempt(const char* code) {
    if (attempt_timer_) esp_timer_stop(attempt_timer_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!attempt_active_) return;
        attempt_active_ = false;
        clear_attempt_secret();
    }
    WifiEvent event;
    event.kind = WifiEvent::Kind::ConnectFailed;
    event.error_code = code;
    push(std::move(event));
    // Like a phone, go back to the network that was saved before the attempt.
    esp_wifi_disconnect();
    if (!connect_saved()) {
        WifiEvent link;
        link.kind = WifiEvent::Kind::LinkChanged;
        push(std::move(link));
    }
}

// Caller holds mutex_.
void Esp32Network::clear_attempt_secret() {
    clear_bytes(attempt_password_.data(), attempt_password_.size());
}

bool Esp32Network::connect_saved() {
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
    // NVS strings carry a terminator: 32-byte SSIDs and 64-digit PSKs need
    // one byte more than the driver's fields.
    char ssid[kWifiSsidMaxBytes + 1] = {};
    char password[kWifiPskHexBytes + 1] = {};
    std::size_t size = sizeof(ssid);
    const bool has_ssid = nvs_get_str(handle, "ssid", ssid, &size) == ESP_OK && ssid[0] != '\0';
    size = sizeof(password);
    if (nvs_get_str(handle, "pass", password, &size) != ESP_OK) password[0] = '\0';
    nvs_close(handle);
    if (!has_ssid) {
        clear_bytes(password, sizeof(password));
        return false;
    }
    wifi_config_t config{};
    std::memcpy(config.sta.ssid, ssid, strnlen(ssid, sizeof(config.sta.ssid)));
    std::memcpy(config.sta.password, password, strnlen(password, sizeof(config.sta.password)));
    config.sta.threshold.authmode = password[0] != '\0' ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    const bool ok = esp_wifi_set_config(WIFI_IF_STA, &config) == ESP_OK && esp_wifi_connect() == ESP_OK;
    clear_bytes(&config, sizeof(config));
    clear_bytes(password, sizeof(password));
    return ok;
}

void Esp32Network::schedule_reconnect() {
    if (!reconnect_timer_ || saved_ssid().empty()) return;
    esp_timer_stop(reconnect_timer_);
    esp_timer_start_once(reconnect_timer_, static_cast<std::uint64_t>(reconnect_delay_ms_) * 1000ULL);
    // Exponential back-off keeps a missing router from draining the battery.
    reconnect_delay_ms_ = std::min<std::uint32_t>(reconnect_delay_ms_ * 2U, kReconnectMaxMs);
}

void Esp32Network::start_time_sync() {
    if (time_sync_started_) return;
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.wait_for_sync = false;
    if (esp_netif_sntp_init(&config) == ESP_OK) {
        time_sync_started_ = true;
    } else {
        ESP_LOGW(kTag, "SNTP start failed");
    }
}

void Esp32Network::on_attempt_timeout(void* context) {
    static_cast<Esp32Network*>(context)->fail_attempt("CONNECT_TIMEOUT");
}

void Esp32Network::on_reconnect_timer(void* context) {
    auto* self = static_cast<Esp32Network*>(context);
    if (!self->connected_.load()) self->connect_saved();
}

void Esp32Network::on_event(void* context, esp_event_base_t base, std::int32_t id, void* data) {
    auto* self = static_cast<Esp32Network*>(context);
    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_STA_START:
                self->connect_saved();
                break;
            case WIFI_EVENT_SCAN_DONE: {
                const auto* done = static_cast<const wifi_event_sta_scan_done_t*>(data);
                self->finish_scan(done != nullptr && done->status == 0);
                break;
            }
            case WIFI_EVENT_STA_DISCONNECTED: {
                const auto* info = static_cast<const wifi_event_sta_disconnected_t*>(data);
                self->handle_disconnect(info ? info->reason : 0);
                break;
            }
            default:
                break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto* got = static_cast<const ip_event_got_ip_t*>(data);
        char address[16] = {};
        if (got) esp_ip4addr_ntoa(&got->ip_info.ip, address, sizeof(address));
        self->handle_got_ip(address);
    }
}

}  // namespace aura::firmware

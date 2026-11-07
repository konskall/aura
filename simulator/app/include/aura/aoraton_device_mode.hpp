#pragma once

#include "aura/aoraton_account_transport.hpp"
#include "aura/aoraton_device_link.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace aura::simulator {

struct AoratonDeviceModeOptions {
    // The device's encrypted NVS, on this PC: the account's own DPAPI vault
    // (not the simulator's usual one) and the settings the PC helper sent.
    std::filesystem::path store_path;
    std::filesystem::path settings_path;
    // The helper's loopback callback, as on the device (127.0.0.1:4779).
    std::uint16_t callback_port{4779};
    // AURA_AORATON_ALLOW_LOOPBACK=1: a test's local fake Supabase over http.
    bool allow_loopback{false};
    // The project's public values, as the board has them built in from
    // config/aoraton.local.json: AURA_AORATON_URL, AURA_AORATON_ANON_KEY and
    // AURA_INCO_ENABLED (dev-simulator.ps1 sets them). Used while the PC has
    // sent none, so the phone alone can sign the account in. Empty: none.
    AoratonLinkConfig built_in;
};

// %LOCALAPPDATA%\AURA\aoraton-device.dpapi and aoraton-device-settings.txt, or
// AURA_AORATON_DEVICE_STORE / AURA_AORATON_DEVICE_SETTINGS; the callback port
// from AURA_AORATON_CALLBACK_PORT; the built-in project values (above).
bool load_aoraton_device_mode_options(AoratonDeviceModeOptions& options, std::string& error);

// AURA_AORATON_SIGN_IN=device, as run.bat device and dev-simulator.ps1
// -AoratonDevice set it.
bool aoraton_device_sign_in_requested();

// The settings file: the project's public URL, its anon key and the inco
// opt-in, one per line, replaced whole.
bool save_aoraton_device_settings(const std::filesystem::path& path, const AoratonLinkConfig& settings);
bool load_aoraton_device_settings(const std::filesystem::path& path, bool allow_loopback, AoratonLinkConfig& settings);

// The simulator signing in the way the device does (ADR 0010, AURA_AORATON_SIGN_IN=device):
// the account starts only with the settings the PC helper sent over File
// transfer, keeps them, and its sign-in goes through the helper instead of
// opening this PC's browser. The same shared link, pump and worker as on the
// board; Windows stands in for the device's TLS, crypto and NVS.
class AoratonDeviceMode final {
public:
    AoratonDeviceMode(AoratonMailbox& mailbox, AoratonDeviceModeOptions options);
    ~AoratonDeviceMode();
    AoratonDeviceMode(const AoratonDeviceMode&) = delete;
    AoratonDeviceMode& operator=(const AoratonDeviceMode&) = delete;

    // For the transfer server: POST /api/aoraton/*.
    IAoratonLinkEndpoint* endpoint() noexcept { return &link_; }
    // The stored settings, as the device reads its NVS at boot.
    void start();
    // Once per frame on the UI thread: new settings restart the account, then
    // the shared pump.
    void pump(IAoratonService& service);
    void stop();
    bool account_ready() const noexcept { return transport_ != nullptr; }
    // The phone's way (ADR 0010), on the UI thread: the owner tapped sign-in;
    // after each File transfer snapshot, the QR's address (empty: none).
    void want_phone_sign_in() noexcept { phone_wanted_ = true; }
    std::string pump_phone(const TransferSnapshot& transfer);
    void set_location_city(std::string city);

private:
    bool take_settings(const AoratonLinkConfig& settings);
    bool settings_locked(AoratonLinkConfig& settings) const;
    void restart();

    AoratonMailbox& mailbox_;
    AoratonDeviceModeOptions options_;
    mutable std::mutex settings_mutex_;
    std::atomic<bool> configured_{false};
    std::atomic<bool> changed_{false};
    AoratonDeviceLink link_;
    std::unique_ptr<AoratonAccountTransport> transport_;
    std::string city_;
    bool phone_wanted_{false};
};

}  // namespace aura::simulator

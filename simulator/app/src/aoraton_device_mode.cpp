#include "aura/aoraton_device_mode.hpp"

#include "aura/file_transfer.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <fstream>
#include <system_error>
#include <utility>

namespace aura::simulator {
namespace {

// Paths may hold any user name: read the environment as wide text.
std::filesystem::path environment_path(const wchar_t* name) {
    std::wstring value(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size()));
    if (length == 0 || length >= value.size()) return {};
    value.resize(length);
    return std::filesystem::path(value);
}

std::string environment_text(const char* name) {
    char value[64] = {};
    const DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));
    return length > 0 && length < sizeof(value) ? std::string(value, length) : std::string{};
}

// The anon key is longer than environment_text reads.
std::string environment_long(const char* name) {
    std::string value(1024, '\0');
    const DWORD length = GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size()));
    if (length == 0 || length >= value.size()) return {};
    value.resize(length);
    return value;
}

bool same(const AoratonLinkConfig& left, const AoratonLinkConfig& right) {
    return left.base_url == right.base_url && left.anon_key == right.anon_key && left.inco_enabled == right.inco_enabled;
}

}  // namespace

bool aoraton_device_sign_in_requested() { return environment_text("AURA_AORATON_SIGN_IN") == "device"; }

bool load_aoraton_device_mode_options(AoratonDeviceModeOptions& options, std::string& error) {
    options = {};
    const auto folder = WindowsAoratonVault::default_path().parent_path();
    options.store_path = environment_path(L"AURA_AORATON_DEVICE_STORE");
    if (options.store_path.empty() && !folder.empty()) options.store_path = folder / L"aoraton-device.dpapi";
    options.settings_path = environment_path(L"AURA_AORATON_DEVICE_SETTINGS");
    if (options.settings_path.empty() && !folder.empty()) options.settings_path = folder / L"aoraton-device-settings.txt";
    if (options.store_path.empty() || options.settings_path.empty()) {
        error = "AORATON_STORE_UNAVAILABLE";
        return false;
    }
    const std::string port = environment_text("AURA_AORATON_CALLBACK_PORT");
    if (!port.empty()) {
        if (port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos || std::stoul(port) == 0 ||
            std::stoul(port) > 65535) {
            error = "AORATON_CALLBACK_PORT_INVALID";
            return false;
        }
        options.callback_port = static_cast<std::uint16_t>(std::stoul(port));
    }
    options.allow_loopback = environment_text("AURA_AORATON_ALLOW_LOOPBACK") == "1";
    options.built_in.base_url = environment_long("AURA_AORATON_URL");
    options.built_in.anon_key = environment_long("AURA_AORATON_ANON_KEY");
    options.built_in.inco_enabled = environment_text("AURA_INCO_ENABLED") == "1";
    if (!aoraton_link_config_valid(options.built_in, options.allow_loopback)) options.built_in = {};
    return true;
}

bool save_aoraton_device_settings(const std::filesystem::path& path, const AoratonLinkConfig& settings) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    auto temporary = path;
    temporary += L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << settings.base_url << '\n' << settings.anon_key << '\n' << (settings.inco_enabled ? '1' : '0') << '\n';
        if (!output.flush()) {
            output.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    std::filesystem::remove(path, error);  // rename may not overwrite on Windows
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

bool load_aoraton_device_settings(const std::filesystem::path& path, bool allow_loopback, AoratonLinkConfig& settings) {
    settings = {};
    std::ifstream input(path, std::ios::binary);
    std::string inco;
    if (!input || !std::getline(input, settings.base_url) || !std::getline(input, settings.anon_key) ||
        !std::getline(input, inco) || (inco != "0" && inco != "1")) {
        settings = {};
        return false;
    }
    settings.inco_enabled = inco == "1";
    if (!aoraton_link_config_valid(settings, allow_loopback)) {
        settings = {};
        return false;
    }
    return true;
}

AoratonDeviceMode::AoratonDeviceMode(AoratonMailbox& mailbox, AoratonDeviceModeOptions options)
    : mailbox_(mailbox),
      options_(std::move(options)),
      link_([this](const AoratonLinkConfig& settings) { return take_settings(settings); }, options_.allow_loopback) {}

AoratonDeviceMode::~AoratonDeviceMode() { stop(); }

// The transfer server's thread: the helper sends the settings on every run;
// only a change restarts the account (as on the device).
bool AoratonDeviceMode::take_settings(const AoratonLinkConfig& settings) {
    std::lock_guard<std::mutex> lock(settings_mutex_);
    AoratonLinkConfig stored;
    if (load_aoraton_device_settings(options_.settings_path, options_.allow_loopback, stored) && same(stored, settings)) {
        configured_ = true;
        return true;
    }
    if (!save_aoraton_device_settings(options_.settings_path, settings)) return false;
    configured_ = true;
    changed_ = true;
    return true;
}

// The settings the PC sent last, as the device keeps them in NVS, or else the
// built-in ones.
bool AoratonDeviceMode::settings_locked(AoratonLinkConfig& settings) const {
    if (load_aoraton_device_settings(options_.settings_path, options_.allow_loopback, settings)) return true;
    settings = options_.built_in;
    return !settings.base_url.empty();
}

void AoratonDeviceMode::start() {
    {
        std::lock_guard<std::mutex> lock(settings_mutex_);
        AoratonLinkConfig settings;
        if (!settings_locked(settings)) return;
    }
    configured_ = true;
    restart();
}

void AoratonDeviceMode::restart() {
    transport_.reset();  // its worker stops before the next one starts
    AoratonLinkConfig settings;
    {
        std::lock_guard<std::mutex> lock(settings_mutex_);
        if (!settings_locked(settings)) return;
    }
    AoratonTransportOptions transport;
    transport.config.base_url = settings.base_url;
    transport.config.anon_key = settings.anon_key;
    transport.config.redirect_url = LoopbackSignInChannel::redirect_url(options_.callback_port);
    transport.config.inco.enabled = settings.inco_enabled;
    transport.store_path = options_.store_path;
    transport.callback_port = options_.callback_port;
    transport.sign_in_channel = &link_;
    transport_ = std::make_unique<AoratonAccountTransport>(mailbox_, std::move(transport));
    transport_->set_location_city(city_);
    transport_->start();
}

void AoratonDeviceMode::pump(IAoratonService& service) {
    if (changed_.exchange(false)) restart();
    aoraton_link_pump(link_, service, configured_, transport_ != nullptr);
}

std::string AoratonDeviceMode::pump_phone(const TransferSnapshot& transfer) {
    return aoraton_phone_pump(link_, transfer, phone_wanted_, [](std::array<std::uint8_t, 16>& secret) {
        return BCryptGenRandom(nullptr, secret.data(), static_cast<ULONG>(secret.size()),
                               BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
    });
}

void AoratonDeviceMode::stop() { transport_.reset(); }

void AoratonDeviceMode::set_location_city(std::string city) {
    city_ = std::move(city);
    if (transport_) transport_->set_location_city(city_);
}

}  // namespace aura::simulator

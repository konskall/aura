#include "aura/aoraton_account_transport.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>

namespace aura::simulator {
namespace {

std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string{};
}

std::int64_t steady_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::int64_t unix_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

// This PC's offset from UTC at `utc_unix`, daylight saving included.
int local_utc_offset_minutes(std::int64_t utc_unix) {
    const std::time_t when = static_cast<std::time_t>(utc_unix);
    std::tm local{};
    std::tm utc{};
    if (localtime_s(&local, &when) != 0 || gmtime_s(&utc, &when) != 0) return 0;
    int days = local.tm_yday - utc.tm_yday;
    if (local.tm_year != utc.tm_year) days = local.tm_year > utc.tm_year ? 1 : -1;
    return days * 1440 + (local.tm_hour - utc.tm_hour) * 60 + (local.tm_min - utc.tm_min);
}

}  // namespace

bool WindowsAoratonStore::load(AoratonStoredSecrets& secrets) {
    AoratonVaultSecrets stored;
    if (!vault_.load(stored)) return false;
    AoratonStoredSecrets loaded;
    loaded.refresh_token = stored.refresh_token;
    for (const auto& room : stored.room_keys) {
        AoratonUnlockedKey key;
        key.room_key = room.room_id;
        key.rk = room.key;
        loaded.keys.push_back(std::move(key));
    }
    stored.clear();
    secrets = std::move(loaded);
    return true;
}

bool WindowsAoratonStore::save(const AoratonStoredSecrets& secrets) {
    AoratonVaultSecrets stored;
    stored.refresh_token = secrets.refresh_token;
    for (const auto& key : secrets.keys) {
        AoratonVaultRoomKey room;
        room.room_id = key.room_key;
        room.key = key.rk;
        stored.room_keys.push_back(std::move(room));
    }
    const bool ok = vault_.store(stored);
    stored.clear();
    return ok;
}

bool WindowsAoratonStore::erase() { return vault_.erase(); }

bool load_aoraton_transport_options(AoratonTransportOptions& options, std::string& error) {
    const std::string url = environment("AURA_AORATON_URL");
    const std::string key = environment("AURA_AORATON_ANON_KEY");
    if (url.empty() || key.empty()) {
        error = "AORATON_CONFIG_MISSING";
        return false;
    }
    const bool loopback_allowed = environment("AURA_AORATON_ALLOW_LOOPBACK") == "1";
    if (url.rfind("https://", 0) != 0 && !(loopback_allowed && url.rfind("http://127.0.0.1:", 0) == 0)) {
        error = "AORATON_CONFIG_INSECURE_URL";
        return false;
    }
    const std::string port = environment("AURA_AORATON_CALLBACK_PORT");
    if (!port.empty()) {
        const long value = std::strtol(port.c_str(), nullptr, 10);
        if (value < 1024 || value > 65535) {
            error = "AORATON_CONFIG_BAD_PORT";
            return false;
        }
        options.callback_port = static_cast<std::uint16_t>(value);
    }
    options.config.base_url = url;
    options.config.anon_key = key;
    options.config.redirect_url = LoopbackSignInChannel::redirect_url(options.callback_port);
    const std::string store = environment("AURA_AORATON_STORE");
    options.store_path = store.empty() ? WindowsAoratonVault::default_path() : std::filesystem::path(store);
    options.fetch_instead_of_browser = environment("AURA_AORATON_BROWSER") == "fetch";
    options.config.inco.enabled = environment("AURA_INCO_ENABLED") == "1";
    if (options.store_path.empty()) {
        error = "AORATON_STORE_UNAVAILABLE";
        return false;
    }
    return true;
}

AoratonTransportOptions AoratonAccountTransport::with_inco_hooks(AoratonTransportOptions options,
                                                                 AoratonAccountTransport* self) {
    options.config.inco.utc_offset_minutes = local_utc_offset_minutes;
    options.config.inco.location_city = [self] {
        std::lock_guard<std::mutex> lock(self->city_mutex_);
        return self->city_;
    };
    // Ids and codes only (the worker never passes text here).
    options.config.inco.log = [](const std::string& line) { std::cerr << "AURA " << line << '\n'; };
    return options;
}

void AoratonAccountTransport::set_location_city(std::string city) {
    std::lock_guard<std::mutex> lock(city_mutex_);
    city_ = std::move(city);
}

AoratonAccountTransport::AoratonAccountTransport(AoratonMailbox& mailbox, AoratonTransportOptions options)
    : options_(with_inco_hooks(std::move(options), this)),
      store_(options_.store_path),
      channel_(options_.callback_port,
               options_.fetch_instead_of_browser
                   ? LoopbackSignInChannel::BrowserOpener([this](const std::string& url) {
                         // The fetch blocks until the loopback thread answered the callback.
                         return http_.fetch_following_redirects(url) == 200;
                     })
                   : LoopbackSignInChannel::BrowserOpener{}),
      worker_(mailbox, options_.config, http_, account_crypto_, message_crypto_, store_, channel_) {}

AoratonAccountTransport::~AoratonAccountTransport() { stop(); }

void AoratonAccountTransport::start() {
    if (thread_.joinable()) return;
    stopping_ = false;
    thread_ = std::thread([this] { run(); });
}

void AoratonAccountTransport::stop() {
    stopping_ = true;
    if (thread_.joinable()) thread_.join();
}

void AoratonAccountTransport::run() {
    worker_.start(steady_ms(), unix_seconds());
    while (!stopping_) {
        worker_.step(steady_ms(), unix_seconds());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

}  // namespace aura::simulator

#pragma once

#include "aura/aoraton_account_worker.hpp"
#include "aura/aoraton_device_link.hpp"
#include "aura/esp32_aoraton_crypto.hpp"
#include "aura/esp32_voice_decoder.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace aura::firmware {

// The return address the PC helper listens on (the same one the owner added
// to Supabase's redirect list for the simulator: http://127.0.0.1:4779/**).
constexpr char kAoratonPcRedirectUrl[] = "http://127.0.0.1:4779/aoraton/callback";

// mbedTLS and the hardware random generator behind the shared account worker.
// PBKDF2 runs in slices and yields, so 600,000 rounds never starve the idle
// task's watchdog or the UI.
class Esp32AoratonAccountCrypto final : public IAoratonAccountCrypto {
public:
    bool random_bytes(std::uint8_t* destination, std::size_t size) override;
    bool sha256(const std::uint8_t* data, std::size_t size, std::array<std::uint8_t, 32>& digest) override;
    bool pbkdf2_sha256(const std::string& passphrase, const std::vector<std::uint8_t>& salt, std::uint32_t iterations,
                       std::array<std::uint8_t, 32>& key) override;
    bool aes_gcm_open(const std::array<std::uint8_t, 32>& key, const std::string& wire, std::string& plaintext) override;
};

// esp_http_client with the certificate bundle: any method, https only, no
// redirects, and a bounded answer (status 0 when anything goes wrong).
class Esp32AoratonHttp final : public IAoratonHttp {
public:
    static constexpr std::size_t kMaxResponseBytes = 512 * 1024;
    // A request may ask for more (attachments, in PSRAM), never beyond this.
    static constexpr std::size_t kMaxDownloadBytes = 4 * 1024 * 1024 + 64;
    AoratonHttpResponse send(const AoratonHttpRequest& request) override;
};

// The refresh token and room keys as one bounded record in encrypted NVS
// (namespace "aura_aoraton"); never in a log.
class Esp32AoratonSecretStore final : public IAoratonSecretStore {
public:
    bool load(AoratonStoredSecrets& secrets) override;
    bool save(const AoratonStoredSecrets& secrets) override;
    bool erase() override;
};

// Encrypted attachments on the microSD card, in /sdcard/aura/.cache/aoraton:
// `<SHA-256>.enc` files, each written to a temporary file and renamed into
// place. Without a mounted card the cache is off.
class Esp32AoratonCacheFiles final : public IAoratonCacheFiles {
public:
    bool list(std::vector<AoratonCacheEntry>& entries) override;
    bool read(const std::string& name, std::size_t max_bytes, std::string& data) override;
    bool write(const std::string& name, const std::string& data) override;
    void remove(const std::string& name) override;
};

// The project's public values and the inco opt-in, from the PC helper.
bool esp32_aoraton_load_config(AoratonLinkConfig& config);
bool esp32_aoraton_save_config(const AoratonLinkConfig& config);

// Supabase realtime over esp_transport (TLS and WebSocket) on its own task:
// the shared Phoenix framing, a heartbeat every 20 s, and reconnection with
// backoff. configure/poll/stop never block on the network.
class Esp32AoratonRealtime final : public IAoratonRealtime {
public:
    Esp32AoratonRealtime();
    ~Esp32AoratonRealtime() override;
    Esp32AoratonRealtime(const Esp32AoratonRealtime&) = delete;
    Esp32AoratonRealtime& operator=(const Esp32AoratonRealtime&) = delete;
    void configure(const std::string& base_url, const std::string& anon_key, const std::string& access_token,
                   const std::vector<std::string>& rooms) override;
    AoratonRealtimeUpdate poll() override;
    void stop() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// The owner's account on the device: the shared worker on one thread, its
// sign-in relayed through the PC (AoratonDeviceLink), stepping every 100 ms.
class Esp32AoratonAccount final {
public:
    Esp32AoratonAccount(AoratonMailbox& mailbox, const AoratonLinkConfig& link_config, AoratonDeviceLink& link);
    ~Esp32AoratonAccount();
    Esp32AoratonAccount(const Esp32AoratonAccount&) = delete;
    Esp32AoratonAccount& operator=(const Esp32AoratonAccount&) = delete;

    // Runs the crypto self-test first; false (and a code on the serial log)
    // when it fails, and then the account stays off.
    bool start();
    void stop();
    // From the UI thread: the saved weather city, for inco's location answers.
    void set_location_city(std::string city);

private:
    static AoratonAccountConfig account_config(const AoratonLinkConfig& link_config, Esp32AoratonAccount* self);
    void run();

    std::mutex city_mutex_;
    std::string city_;
    Esp32AoratonHttp http_;
    Esp32AoratonAccountCrypto account_crypto_;
    Esp32AoratonCryptoV2 message_crypto_;
    Esp32AoratonSecretStore store_;
    Esp32AoratonRealtime realtime_;
    Esp32VoiceDecoder voice_decoder_;
    Esp32AoratonCacheFiles cache_files_;
    AoratonAttachmentCache attachment_cache_{cache_files_};
    AoratonAccountWorker worker_;
    std::thread thread_;
    std::atomic<bool> stopping_{false};
};

}  // namespace aura::firmware

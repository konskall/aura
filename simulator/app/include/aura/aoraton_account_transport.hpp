#pragma once

#include "aura/aoraton_account_worker.hpp"
#include "aura/loopback_sign_in.hpp"
#include "aura/windows_aoraton_crypto.hpp"
#include "aura/windows_aoraton_vault.hpp"
#include "aura/winhttp_client.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

namespace aura::simulator {

// DPAPI-backed secret store over the v11 vault file format: the refresh token
// and room keys are kept, the access token never is.
class WindowsAoratonStore final : public IAoratonSecretStore {
public:
    explicit WindowsAoratonStore(std::filesystem::path path) : vault_(std::move(path)) {}
    bool load(AoratonStoredSecrets& secrets) override;
    bool save(const AoratonStoredSecrets& secrets) override;
    bool erase() override;

private:
    WindowsAoratonVault vault_;
};

struct AoratonTransportOptions {
    AoratonAccountConfig config;
    std::filesystem::path store_path;
    std::uint16_t callback_port{4779};
    // Stand-in for the browser (fake-backend smoke only): fetch the authorize
    // URL and follow its redirect to the loopback callback.
    bool fetch_instead_of_browser{false};
};

// Reads AURA_AORATON_URL and AURA_AORATON_ANON_KEY (set by dev-simulator.ps1
// from the git-ignored config/aoraton.local.json). Only https URLs are taken,
// or http://127.0.0.1 when AURA_AORATON_ALLOW_LOOPBACK=1 (the smoke test).
bool load_aoraton_transport_options(AoratonTransportOptions& options, std::string& error);

// Runs the shared AoratonAccountWorker on one thread, stepping every 100 ms.
class AoratonAccountTransport final {
public:
    AoratonAccountTransport(AoratonMailbox& mailbox, AoratonTransportOptions options);
    ~AoratonAccountTransport();
    AoratonAccountTransport(const AoratonAccountTransport&) = delete;
    AoratonAccountTransport& operator=(const AoratonAccountTransport&) = delete;

    void start();
    void stop();

private:
    void run();

    AoratonTransportOptions options_;
    WinHttpClient http_;
    WindowsAoratonAccountCrypto account_crypto_;
    WindowsAoratonCryptoV2 message_crypto_;
    WindowsAoratonStore store_;
    LoopbackSignInChannel channel_;
    AoratonAccountWorker worker_;
    std::thread thread_;
    std::atomic<bool> stopping_{false};
};

}  // namespace aura::simulator

// End-to-end smoke of the Aoraton account path on Windows, against the local
// fake in tests/interop/aoraton_fake_supabase.mjs (run_aoraton_account_smoke.mjs
// starts it and sets the environment). Real WinHTTP, loopback listener, DPAPI
// store and CNG; no network beyond 127.0.0.1 and no secret is ever printed.
#include "aura/aoraton_account_transport.hpp"
#include "aura/aoraton_crypto.hpp"
#include "aura/aoraton_real.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr char kAlpha[] = "v2.QUFBQUFBQUFBQUFBQUFBQUFB";

std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string{};
}

}  // namespace

int main() {
    using namespace aura;
    using namespace aura::simulator;

    AoratonTransportOptions options;
    std::string error;
    if (!load_aoraton_transport_options(options, error)) {
        std::cerr << "aoraton smoke: " << error << '\n';
        return 2;
    }
    const std::string report_path = environment("AURA_AORATON_SMOKE_REPORT");
    // The synthetic passphrase contains non-ASCII text; the C runtime's narrow
    // environment would mangle it, so it arrives base64url-encoded.
    std::vector<std::uint8_t> passphrase_bytes;
    aoraton_base64url_decode(environment("AURA_AORATON_SMOKE_PASSPHRASE_B64"), passphrase_bytes);
    const std::string passphrase(passphrase_bytes.begin(), passphrase_bytes.end());
    if (report_path.empty() || passphrase.empty()) {
        std::cerr << "aoraton smoke: report path and synthetic passphrase are required\n";
        return 2;
    }

    AoratonMailbox mailbox;
    WindowsAoratonAccountCrypto ids;
    RealAoratonService service(mailbox, AoratonRuntimeMode::IntegrationTest,
                               [&ids] { return aoraton_new_client_message_id(ids); });
    AoratonAccountTransport transport(mailbox, options);
    transport.start();

    auto wait_for = [&](const std::function<bool()>& done, int timeout_ms) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            service.tick(20);
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return false;
    };
    const auto& snap = service.snapshot();
    auto alpha = [&]() -> const AoratonRoom* {
        for (const auto& room : snap.rooms) {
            if (room.id == kAlpha) return &room;
        }
        return nullptr;
    };

    bool signed_in = service.begin_pairing() &&
                     wait_for([&] { return snap.pairing == AoratonPairingState::Paired; }, 15000);
    const bool rooms = signed_in && wait_for([&] { return snap.rooms.size() == 2; }, 10000);
    const std::size_t room_count = snap.rooms.size();
    const bool unlocked = rooms && service.unlock_keys(passphrase) &&
                          wait_for([&] {
                              return snap.key_state == AoratonKeyState::Ready && alpha() && alpha()->key_available &&
                                     !alpha()->preview.empty();
                          }, 15000);
    const bool history = unlocked && service.select_room(kAlpha) &&
                         wait_for([&] { return !snap.messages.empty(); }, 10000);
    const std::size_t history_count = snap.messages.size();
    const bool sent = history && service.prepare_send(kAlpha, "DEMO από το AURA") && service.confirm_send() &&
                      wait_for([&] {
                          return !snap.messages.empty() &&
                                 snap.messages.back().delivery == AoratonDelivery::Acknowledged &&
                                 snap.messages.back().direction == AoratonDirection::Outgoing;
                      }, 15000);
    const bool incoming = sent && wait_for([&] { return !snap.notifications.empty(); }, 30000);
    const bool store_written = std::filesystem::exists(options.store_path);
    const bool signed_out = incoming && service.unpair() &&
                            wait_for([&] {
                                return snap.pairing == AoratonPairingState::NotPaired &&
                                       !std::filesystem::exists(options.store_path);
                            }, 10000);
    transport.stop();

    std::ofstream report(report_path, std::ios::binary | std::ios::trunc);
    report << "{\"signed_in\":" << (signed_in ? "true" : "false") << ",\"rooms\":" << room_count
           << ",\"rooms_listed\":" << (rooms ? "true" : "false") << ",\"unlocked\":" << (unlocked ? "true" : "false")
           << ",\"history\":" << history_count << ",\"sent\":" << (sent ? "\"Acknowledged\"" : "null")
           << ",\"incoming_notified\":" << (incoming ? 1 : 0) << ",\"store_written\":"
           << (store_written ? "true" : "false") << ",\"signed_out\":" << (signed_out ? "true" : "false")
           << ",\"last_error\":\"" << snap.last_error << "\"}\n";
    const bool ok = signed_in && rooms && unlocked && history && sent && incoming && store_written && signed_out;
    std::cout << (ok ? "aoraton account smoke ok" : "aoraton account smoke FAILED") << '\n';
    return ok ? 0 : 1;
}

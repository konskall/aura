// A device stand-in for the PC helper's interop test: the simulator's transfer
// server with the shared AoratonDeviceLink and a fake account worker. It
// prints `READY <address> <code>` (DEMO values), then one line per event the
// test checks: CONFIG OK|BAD, CODE_OK|CODE_WRONG, CANCELLED, INVITE_OK|WRONG.
//   aura_aoraton_link_host <expected code> <expected invite> <callback port>
#include "aura/aoraton_account.hpp"
#include "aura/aoraton_device_link.hpp"
#include "aura/file_transfer_runtime.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    const std::string expected_code = argv[1];
    const std::string expected_invite = argv[2];
    const std::string callback_port = argv[3];
    const auto root = std::filesystem::temp_directory_path() / ("aura-link-host-" + callback_port);
    std::filesystem::create_directories(root);

    std::atomic<bool> configured{false};
    aura::AoratonDeviceLink link([&](const aura::AoratonLinkConfig& config) {
        const bool valid = aura::aoraton_link_config_valid(config);
        std::cout << (valid ? "CONFIG OK" : "CONFIG BAD") << std::endl;
        configured = valid;
        return valid;
    });
    aura::simulator::FileTransferRuntime transfer(root.u8string(), false, "127.0.0.1", 0, &link);
    if (!transfer.start()) return 3;
    for (int wait = 0; wait < 200; ++wait) {
        transfer.poll();
        const auto snapshot = transfer.session().snapshot();
        if (snapshot.state == aura::TransferState::Ready) {
            std::cout << "READY " << snapshot.address << ' ' << snapshot.pin << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    aura::AoratonPairingState pairing = aura::AoratonPairingState::NotPaired;
    std::string error;
    bool open = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    while (std::chrono::steady_clock::now() < deadline) {
        transfer.poll();
        if (link.take_sign_in_request()) {
            pairing = aura::AoratonPairingState::WaitingForBrowser;
            open = link.open("https://demo-project.supabase.co/auth/v1/authorize?provider=google&redirect_to=" +
                             aura::aoraton_url_encode("http://127.0.0.1:" + callback_port + "/aoraton/callback") +
                             "&code_challenge=DEMOchallenge&code_challenge_method=s256");
        }
        if (open) {
            std::string code;
            const auto status = link.poll(code);
            if (status == aura::AoratonCallbackStatus::Received) {
                std::cout << (code == expected_code ? "CODE_OK" : "CODE_WRONG") << std::endl;
                pairing = aura::AoratonPairingState::Paired;
                open = false;
            } else if (status == aura::AoratonCallbackStatus::Failed) {
                std::cout << "CANCELLED" << std::endl;
                pairing = aura::AoratonPairingState::Error;
                error = "SIGN_IN_CANCELLED";
                open = false;
            }
        }
        std::string invite;
        if (link.take_invite(invite)) std::cout << (invite == expected_invite ? "INVITE_OK" : "INVITE_WRONG") << std::endl;
        link.publish(configured, pairing, error);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    transfer.stop();
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    return 0;
}

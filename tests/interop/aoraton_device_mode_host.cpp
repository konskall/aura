// The simulator's Aoraton device mode for the PC helper's end-to-end test: the
// real transfer server, AoratonDeviceMode and the account worker, against the
// local fake Supabase (run_aoraton_device_mode_test.mjs sets the environment).
// It prints `READY <address> <code>`, then CONFIGURED, SIGNED_IN and ROOMS <n>
// as they happen; never a token, a key or a message. With --phone the owner
// has tapped sign-in on the device: it also prints `PHONE <the QR's address>`.
#include "aura/aoraton_account.hpp"
#include "aura/aoraton_device_mode.hpp"
#include "aura/aoraton_real.hpp"
#include "aura/file_transfer_runtime.hpp"
#include "aura/windows_aoraton_crypto.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    using namespace aura;
    using namespace aura::simulator;
    const bool phone = argc > 1 && std::string(argv[1]) == "--phone";

    // The switch run.bat device sets, read the way the simulator reads it.
    std::cout << "MODE " << (aoraton_device_sign_in_requested() ? "device" : "usual") << std::endl;
    AoratonDeviceModeOptions options;
    std::string error;
    if (!load_aoraton_device_mode_options(options, error)) {
        std::cerr << "device mode: " << error << '\n';
        return 2;
    }
    const auto root = options.settings_path.parent_path() / "transfer";
    std::filesystem::create_directories(root);

    AoratonMailbox mailbox;
    WindowsAoratonAccountCrypto ids;
    RealAoratonService service(mailbox, AoratonRuntimeMode::IntegrationTest,
                               [&ids] { return aoraton_new_client_message_id(ids); });
    AoratonDeviceMode device(mailbox, options);
    device.start();
    FileTransferRuntime transfer(root.u8string(), false, "127.0.0.1", 0, device.endpoint());
    if (!transfer.start()) return 3;
    for (int wait = 0; wait < 200; ++wait) {
        transfer.poll();
        const auto snapshot = transfer.session().snapshot();
        if (snapshot.state == TransferState::Ready) {
            std::cout << "READY " << snapshot.address << ' ' << snapshot.pin << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    bool configured = false;
    bool signed_in = false;
    bool rooms = false;
    bool shown = false;
    if (phone) device.want_phone_sign_in();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    while (std::chrono::steady_clock::now() < deadline) {
        transfer.poll();
        service.tick(20);
        device.pump(service);
        const auto qr = device.pump_phone(transfer.session().snapshot());
        if (!shown && !qr.empty()) {
            shown = true;
            std::cout << "PHONE " << qr << std::endl;
        }
        const auto& snapshot = service.snapshot();
        if (!configured && device.account_ready()) {
            configured = true;
            std::cout << "CONFIGURED" << std::endl;
        }
        if (!signed_in && snapshot.pairing == AoratonPairingState::Paired) {
            signed_in = true;
            std::cout << "SIGNED_IN" << std::endl;
        }
        if (!rooms && !snapshot.rooms.empty()) {
            rooms = true;
            std::cout << "ROOMS " << snapshot.rooms.size() << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    transfer.stop();
    device.stop();
    return 0;
}

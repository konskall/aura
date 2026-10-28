#include "aura/windows_ancs_service.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    int seconds = 10;
    if (argc == 3 && std::string(argv[1]) == "--seconds") {
        char* end = nullptr;
        const long parsed = std::strtol(argv[2], &end, 10);
        if (!end || *end != '\0' || parsed < 5 || parsed > 120) {
            std::cerr << "Usage: aura_ancs_probe [--seconds 5..120]\n";
            return 3;
        }
        seconds = static_cast<int>(parsed);
    } else if (argc != 1) {
        std::cerr << "Usage: aura_ancs_probe [--seconds 5..120]\n";
        return 3;
    }
    aura::simulator::WindowsAncsService service;
    service.start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    int notifications = 0;
    bool connected = false;
    std::string error = "ANCS_PROBE_TIMEOUT";
    while (std::chrono::steady_clock::now() < deadline) {
        aura::simulator::WindowsAncsEvent event;
        while (service.poll(event)) {
            if (event.kind == aura::simulator::WindowsAncsEvent::Kind::Link) {
                connected = event.link == aura::PhoneLinkState::Connected;
                error = event.error_code;
                if (!connected) {
                    std::cout << "AURA ANCS: no authorised paired iPhone service (" << error << ")\n";
                    service.stop();
                    return 2;
                }
            } else if (event.kind == aura::simulator::WindowsAncsEvent::Kind::Upsert) {
                ++notifications;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    service.stop();
    if (!connected) {
        std::cout << "AURA ANCS: probe timed out; pair and authorise the iPhone in Windows first\n";
        return 2;
    }
    // Contents are intentionally not printed or persisted.
    std::cout << "AURA ANCS: connected; notification records received=" << notifications << "\n";
    return 0;
}

#include "aura/radio.hpp"
#include "aura/windows_audio_backend.hpp"
#include "aura/windows_radio_service.hpp"

#include "SDL.h"

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <thread>

int main() {
    SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);

    aura::simulator::WindowsAudioBackend audio;
    if (!audio.initialize()) {
        std::cerr << "radio live test: audio initialization failed\n";
        return EXIT_FAILURE;
    }
    aura::simulator::WindowsRadioService service(audio);

    auto search = service.search("rock");
    if (search.wait_for(std::chrono::seconds(20)) != std::future_status::ready) {
        std::cerr << "radio live test: directory timeout\n";
        return EXIT_FAILURE;
    }
    const auto directory = search.get();
    if (!directory.ok || directory.stations.empty()) {
        std::cerr << "radio live test: directory failed: " << directory.error_code << '\n';
        return EXIT_FAILURE;
    }

    const auto stations = aura::greek_default_stations();
    if (stations.empty()) return EXIT_FAILURE;
    const auto generation = service.play(stations.front());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        aura::simulator::RadioWorkerEvent event;
        while (service.poll(event)) {
            if (event.generation != generation) continue;
            if (event.type == aura::simulator::RadioWorkerEventType::Playing) {
                std::cout << "radio live test: PASS; directory_results=" << directory.stations.size()
                          << "; station=" << stations.front().name << '\n';
                service.stop();
                audio.shutdown();
                return EXIT_SUCCESS;
            }
            if (event.type == aura::simulator::RadioWorkerEventType::Failed) {
                std::cerr << "radio live test: stream failed: " << event.text << '\n';
                return EXIT_FAILURE;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    std::cerr << "radio live test: stream timeout\n";
    return EXIT_FAILURE;
}

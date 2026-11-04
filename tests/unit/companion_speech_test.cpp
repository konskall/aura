// Real companion/bridge/audio path, using only loopback and SDL's silent driver.
#include <winsock2.h>
#include <ws2tcpip.h>

#include "aura/companion_runtime.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>

namespace {

void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

class FakeSpeechBridge {
public:
    FakeSpeechBridge() {
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        expect(
            bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 && listen(listener_, 8) == 0,
            "fake speech bridge listens on loopback");
        int size = sizeof(address);
        expect(getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size) == 0, "ephemeral port");
        port = ntohs(address.sin_port);
        server_ = std::thread([this] {
            for (;;) {
                const auto client = accept(listener_, nullptr, nullptr);
                if (client == INVALID_SOCKET) break;
                workers_.emplace_back([this, client] { serve(client); });
            }
        });
    }

    ~FakeSpeechBridge() {
        release();
        closesocket(listener_);
        server_.join();
        for (auto& worker : workers_)
            worker.join();
    }

    void release() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            released_ = true;
        }
        gate_.notify_all();
    }

    std::uint16_t port{};
    std::atomic<int> requested{0};
    std::atomic<int> replied{0};

private:
    void serve(SOCKET client) {
        const DWORD timeout = 5000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        std::string request;
        char buffer[4096];
        while (request.size() < 8192) {
            const int count = recv(client, buffer, sizeof(buffer), 0);
            if (count <= 0) break;
            request.append(buffer, static_cast<std::size_t>(count));
            const auto end = request.find("\r\n\r\n");
            if (end == std::string::npos) continue;
            const auto at = request.find("Content-Length: ");
            const auto length = at == std::string::npos ? 0UL : std::strtoul(request.c_str() + at + 16, nullptr, 10);
            if (request.size() >= end + 4 + length) break;
        }
        std::string type = "application/json";
        std::string status = "200 OK";
        std::string body = R"({"stt":false,"tts_el":true,"tts_en":true,"tts_provider":"windows"})";
        const bool synthesize = request.find("POST /v1/speech/synthesize ") == 0;
        if (synthesize) {
            ++requested;
            std::unique_lock<std::mutex> lock(mutex_);
            expect(gate_.wait_for(lock, std::chrono::seconds(10), [this] { return released_; }),
                   "test releases pending synthesis");
            lock.unlock();
            if (request.find("DEMO failure") != std::string::npos) {
                status = "503 Service Unavailable";
                body = R"({"code":"DEMO_SYNTHESIS_FAILED"})";
            } else {
                const auto wav = aura::make_wav(std::vector<std::int16_t>(16000 * 3, 1000), 16000);
                type = "audio/wav";
                body.assign(reinterpret_cast<const char*>(wav.data()), wav.size());
            }
        }
        const std::string response = "HTTP/1.1 " + status + "\r\nContent-Type: " + type +
                                     "\r\nContent-Length: " + std::to_string(body.size()) +
                                     "\r\nConnection: close\r\n\r\n" + body;
        for (std::size_t sent = 0; sent < response.size();) {
            const int count = send(client, response.data() + sent, static_cast<int>(response.size() - sent), 0);
            if (count <= 0) break;
            sent += static_cast<std::size_t>(count);
        }
        shutdown(client, SD_SEND);
        closesocket(client);
        if (synthesize) ++replied;
    }

    SOCKET listener_{INVALID_SOCKET};
    std::thread server_;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable gate_;
    bool released_{false};
};

template <typename Predicate, typename Pump>
void until(Predicate predicate, Pump pump, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        pump();
        SDL_Delay(2);
    }
    expect(predicate(), message);
}

}  // namespace

int main() {
    WSADATA sockets{};
    expect(WSAStartup(MAKEWORD(2, 2), &sockets) == 0, "winsock starts");
    SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    {
        FakeSpeechBridge bridge;
        aura::simulator::WindowsAudioBackend audio;
        expect(audio.initialize(), "silent audio opens");
        auto speech = std::make_shared<aura::simulator::BridgeSpeechService>("DEMO-test-token", bridge.port);
        speech->refresh_health();
        expect(speech->tts_available("el"), "fake Greek speech is available");
        aura::AssistantController controller;
        aura::simulator::CompanionContext context;
        context.audio = &audio;
        context.controller = &controller;
        context.deterministic = true;
        aura::simulator::CompanionRuntime companion(context);
        aura::AuraUiCallbacks callbacks;
        int audio_stops = 0;
        callbacks.on_audio_stop = [&] {
            ++audio_stops;
            audio.stop();
        };
        companion.install(callbacks);
        companion.start(speech);
        const auto pump = [&] {
            audio.tick(2);
            companion.poll(2);
        };

        // Fill the four retired slots and cancel one more request. The fifth
        // future has to stay owned, but its eventual result must be discarded.
        for (int index = 1; index <= 5; ++index) {
            callbacks.on_speak("DEMO pending", "el");
            expect(companion.extras().speaking, "synthesis preparation is stoppable immediately");
            until([&] { return bridge.requested >= index; }, pump, "synthesis reaches the fake bridge");
            const auto start = std::chrono::steady_clock::now();
            callbacks.on_speak_stop();
            expect(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(150),
                   "stop never waits for network synthesis");
            expect(!companion.extras().speaking, "stop clears the speaking snapshot");
        }
        bridge.release();
        until([&] { return bridge.replied == 5; }, pump, "cancelled synthesis completes");
        for (int index = 0; index < 100; ++index) {
            pump();
            SDL_Delay(2);
        }
        expect(!companion.extras().spoken_reply && audio.diagnostics().state == aura::AudioState::Ready,
               "no cancelled result starts playback, including the retained fifth job");

        callbacks.on_speak("DEMO playback", "el");
        until([&] { return audio.diagnostics().state == aura::AudioState::Playing; }, pump, "speech plays");
        expect(companion.extras().speaking, "playback keeps the stop state");
        callbacks.on_audio_stop();
        expect(audio_stops == 1 && !companion.extras().speaking && audio.diagnostics().state == aura::AudioState::Ready,
               "the speaking screen stop cancels speech and preserves its audio callback");

        callbacks.on_speak("DEMO completion", "el");
        until([&] { return audio.diagnostics().state == aura::AudioState::Playing; }, pump, "speech restarts");
        audio.simulate_complete();
        pump();
        expect(!companion.extras().speaking, "natural completion restores read aloud");

        callbacks.on_speak("DEMO failure", "el");
        until([&] { return !companion.extras().speaking; }, pump, "synthesis failure restores read aloud");
        expect(audio.diagnostics().state != aura::AudioState::Playing, "failed synthesis never plays");
    }
    {
        std::error_code path_error;
        const auto storage_root = std::filesystem::current_path(path_error) / "artifacts";
        expect(!path_error, "settings test workspace is available");
        std::filesystem::create_directories(storage_root, path_error);
        expect(!path_error, "settings test workspace directory exists");
        const auto temporary_root = std::filesystem::absolute(storage_root).lexically_normal();
        const auto directory = temporary_root / ("AURA-sync-settings-test-" + std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directories(directory, path_error);
        expect(!path_error, "synthetic settings directory created");
        const auto checked = std::filesystem::absolute(directory).lexically_normal();
        expect(checked.parent_path() == temporary_root &&
                   checked.filename().string().find("AURA-sync-settings-test-") == 0,
               "settings test storage stays inside its explicitly named temporary directory");
        aura::simulator::WindowsAudioBackend audio;
        expect(audio.initialize(), "silent audio for the settings runtime");
        aura::MockAoratonService service;
        aura::AssistantController controller;
        aura::simulator::CompanionContext context;
        context.audio = &audio;
        context.controller = &controller;
        context.aoraton = &service;
        context.store_dir = directory.string();
        {
            aura::simulator::CompanionRuntime runtime(context);
            aura::AuraUiCallbacks callbacks;
            runtime.install(callbacks);
            expect(callbacks.on_aoraton_sync_changed({false, 60}), "settings callback persists off and one minute");
            expect(!callbacks.on_aoraton_sync_changed({true, 2}), "invalid interval rejected before persistence");
        }
        {
            aura::MockAoratonService restored;
            context.aoraton = &restored;
            aura::simulator::CompanionRuntime runtime(context);
            runtime.start(nullptr);
            expect(restored.snapshot().sync_settings == aura::AoratonSyncSettings{false, 60},
                   "actual simulator runtime restores the chosen interval and off switch after restart");
        }
        // Delete only these known synthetic files, then the empty directory;
        // no recursive traversal through a redirected/reparse parent is used.
        for (const auto* name : {"aoraton-sync.txt", "language.txt", "sound.txt", "clock.txt", "notes.txt",
                                 "organizer.txt", "display.txt"}) {
            for (const auto* suffix : {"", ".bak", ".tmp"})
                std::filesystem::remove(checked / (std::string(name) + suffix), path_error);
        }
        std::filesystem::remove(checked, path_error);
    }
    SDL_Quit();
    WSACleanup();
    std::cout << "aura_companion_speech_tests passed\n";
}

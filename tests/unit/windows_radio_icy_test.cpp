// Loopback only: a fake Shoutcast v1 server on 127.0.0.1 answers "ICY 200 OK"
// (which WinHTTP rejects) with silent MP3 frames. It checks the HTTP/1.0
// fallback, automatic reconnection after the stream drops, and that switching
// stations does not wait for a blocked read. No external service is contacted.
#include "aura/radio.hpp"
#include "aura/windows_audio_backend.hpp"
#include "aura/windows_radio_service.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include "SDL.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// Silent MPEG-1 Layer III frames: 128 kbit/s, 44.1 kHz, stereo.
std::string silent_frames(int count) {
    std::string bytes;
    for (int index = 0; index < count; ++index) {
        std::string frame(417, '\0');
        frame[0] = static_cast<char>(0xFF);
        frame[1] = static_cast<char>(0xFB);
        frame[2] = static_cast<char>(0x90);
        bytes += frame;
    }
    return bytes;
}

class FakeShoutcast {
public:
    FakeShoutcast() {
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        expect(bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
                   listen(listener_, 8) == 0,
               "a loopback listener");
        int size = sizeof(address);
        getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size);
        port_ = ntohs(address.sin_port);
        acceptor_ = std::thread([this] { accept_loop(); });
    }

    ~FakeShoutcast() {
        done_.store(true);
        closesocket(listener_);
        if (acceptor_.joinable()) acceptor_.join();
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& client : clients_) {
            if (client.joinable()) client.join();
        }
    }

    std::string url(const char* path) const { return "http://127.0.0.1:" + std::to_string(port_) + path; }
    int connections() const { return connections_.load(); }

private:
    void accept_loop() {
        while (!done_.load()) {
            const SOCKET client = accept(listener_, nullptr, nullptr);
            if (client == INVALID_SOCKET) return;
            ++connections_;
            std::lock_guard<std::mutex> lock(mutex_);
            clients_.emplace_back([this, client] { serve(client); });
        }
    }

    // "/close" sends a little audio and hangs up; "/hold" sends it and then
    // stays silent with the connection open, as a stalled server does.
    void serve(SOCKET client) {
        std::string request;
        char buffer[1024];
        while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192) {
            const int received = recv(client, buffer, sizeof(buffer), 0);
            if (received <= 0) break;
            request.append(buffer, static_cast<std::size_t>(received));
        }
        const bool hold = request.find("GET /hold") == 0;
        const std::string reply = "ICY 200 OK\r\nicy-name: DEMO\r\nicy-metaint: 0\r\n\r\n" + silent_frames(40);
        send(client, reply.data(), static_cast<int>(reply.size()), 0);
        if (hold) {
            const auto until = Clock::now() + std::chrono::seconds(15);
            while (!done_.load() && Clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        shutdown(client, SD_BOTH);
        closesocket(client);
    }

    SOCKET listener_{INVALID_SOCKET};
    std::uint16_t port_{0};
    std::atomic<bool> done_{false};
    std::atomic<int> connections_{0};
    std::thread acceptor_;
    std::mutex mutex_;
    std::vector<std::thread> clients_;
};

// Waits for an event of `type` for `generation`; false on a failure or timeout.
bool wait_for(aura::simulator::WindowsRadioService& service, std::uint64_t generation,
              aura::simulator::RadioWorkerEventType type, std::chrono::milliseconds limit) {
    const auto deadline = Clock::now() + limit;
    while (Clock::now() < deadline) {
        aura::simulator::RadioWorkerEvent event;
        while (service.poll(event)) {
            if (event.generation != generation) continue;
            if (event.type == type) return true;
            if (event.type == aura::simulator::RadioWorkerEventType::Failed) {
                std::cerr << "radio failed: " << event.text << '\n';
                return false;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

}  // namespace

int main() {
    using aura::simulator::RadioWorkerEventType;
    SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    WSADATA data;
    expect(WSAStartup(MAKEWORD(2, 2), &data) == 0, "winsock starts");
    {
        FakeShoutcast server;
        aura::simulator::WindowsAudioBackend audio;
        expect(audio.initialize(), "dummy audio opens");
        {
            aura::simulator::WindowsRadioService service(audio);

            const aura::RadioStation dropping{"t1", "DEMO drop", server.url("/close"), 128, {}};
            const auto first = service.play(dropping);
            expect(wait_for(service, first, RadioWorkerEventType::Playing, std::chrono::seconds(10)),
                   "an ICY 200 OK stream plays");
            expect(wait_for(service, first, RadioWorkerEventType::Reconnecting, std::chrono::seconds(10)),
                   "a dropped stream is joined again");
            expect(wait_for(service, first, RadioWorkerEventType::Playing, std::chrono::seconds(10)),
                   "and plays again");

            const aura::RadioStation stalled{"t2", "DEMO stall", server.url("/hold"), 128, {}};
            const auto second = service.play(stalled);
            expect(wait_for(service, second, RadioWorkerEventType::Playing, std::chrono::seconds(10)),
                   "the stalling stream plays first");
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            const auto switched = Clock::now();
            const auto third = service.play(dropping);
            expect(wait_for(service, third, RadioWorkerEventType::Playing, std::chrono::seconds(10)),
                   "the next station plays");
            const auto switch_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - switched).count();
            std::cout << "station switch took " << switch_ms << " ms; connections=" << server.connections() << '\n';
            expect(switch_ms < 1500, "switching does not wait for a blocked read");
            service.stop();
        }
        audio.shutdown();
    }
    WSACleanup();
    std::cout << "aura_windows_radio_icy_tests passed\n";
    return 0;
}

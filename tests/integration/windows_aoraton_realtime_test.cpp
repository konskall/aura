#include "aura/windows_aoraton_realtime.hpp"
#include "aura/winhttp_client.hpp"
#include "aura/json_reader.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
template <typename Predicate>
void until(Predicate predicate, int timeout_ms, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    expect(false, message);
}
}  // namespace
int main(int argc, char** argv) {
    expect(argc == 2 && std::string(argv[1]).rfind("http://127.0.0.1:", 0) == 0, "loopback fake endpoint required");
    const std::string base = argv[1];
    aura::simulator::WinHttpClient http;
    const auto control = [&](const char* path) {
        aura::AoratonHttpRequest request;
        request.method = "GET";
        request.url = base + path;
        const auto response = http.send(request);
        expect(response.status == 200, "fake control request succeeded");
        return response.body;
    };
    const auto started = std::chrono::steady_clock::now();
    {
        aura::simulator::WindowsAoratonRealtime client;
        client.configure(base, "DEMO-anon", "DEMO-jwt-1", {"alpha", "beta"});
        until(
            [&] {
                const auto state = client.poll();
                return state.active && state.refresh_required;
            },
            5000, "both actual WinHTTP websocket subscriptions become ready and request backfill");
        control("/insert");
        until([&] { return client.poll().refresh_required; }, 2000, "fragmented INSERT arrives immediately");
        control("/drop");
        until([&] { return !client.poll().active; }, 2000, "connection loss disables realtime");
        until(
            [&] {
                const auto state = client.poll();
                return state.active && state.refresh_required;
            },
            5000, "socket reconnects and requests gap backfill");
        client.configure(base, "DEMO-anon", "DEMO-jwt-2", {"alpha", "beta"});
        expect(!client.poll().active, "JWT rotation immediately invalidates old readiness");
        until(
            [&] {
                const auto state = client.poll();
                return state.active && state.refresh_required;
            },
            5000, "rotated JWT is authenticated on a fresh connection");
        control("/heartbeat");
        until([&] { return client.poll().refresh_required; }, 25000,
              "idle connection stays alive through acknowledged Phoenix heartbeat");
        aura::JsonValue stats;
        expect(aura::parse_json(control("/stats"), stats) && stats.find("heartbeat")->as_bool() &&
                   stats.find("connections")->as_int() == 3,
               "idle socket did not silently churn or fake heartbeat readiness");
        control("/oversize");
        until([&] { return !client.poll().active; }, 2000, "oversized frame closes the socket");
        const auto stopped = std::chrono::steady_clock::now();
        client.stop();
        expect(!client.poll().active && !client.poll().refresh_required, "stop clears pending state immediately");
        expect(std::chrono::steady_clock::now() - stopped < std::chrono::milliseconds(100),
               "stop never waits for network I/O");
    }
    expect(std::chrono::steady_clock::now() - started < std::chrono::seconds(40), "bounded socket teardown");
    std::cout << "native realtime websocket: PASS\n";
}

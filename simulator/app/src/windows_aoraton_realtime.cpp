#include "aura/windows_aoraton_realtime.hpp"
#include "aura/aoraton_account.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

namespace aura::simulator {
namespace {
using Clock = std::chrono::steady_clock;
void wipe(std::string& value) {
    if (!value.empty()) SecureZeroMemory(value.data(), value.size());
    value.clear();
}
struct Config {
    std::string base, key, token;
    std::vector<std::string> rooms;
    ~Config() { clear(); }
    void clear() {
        wipe(base);
        wipe(key);
        wipe(token);
        rooms.clear();
    }
    bool operator==(const Config& other) const {
        return base == other.base && key == other.key && token == other.token && rooms == other.rooms;
    }
};
struct Handle {
    HINTERNET value{nullptr};
    ~Handle() {
        if (value) WinHttpCloseHandle(value);
    }
};

bool open_socket(const Config& config, Handle& session, Handle& connection, Handle& socket) {
    // HTTP upgrade uses the same endpoint and certificate validation as REST.
    // Plain HTTP is accepted only for the explicit loopback fake transport.
    const bool secure = config.base.rfind("https://", 0) == 0;
    if (!secure && config.base.rfind("http://127.0.0.1:", 0) != 0) return false;
    if (config.base.size() > 2048 || config.key.size() > 8192) return false;
    for (const unsigned char c : config.base)
        if (c < 0x21 || c > 0x7e) return false;
    const std::wstring url(config.base.begin(), config.base.end());
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = parts.dwUserNameLength =
        parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.dwUserNameLength || parts.dwPasswordLength ||
        parts.dwExtraInfoLength ||
        (parts.dwUrlPathLength && std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) != L"/"))
        return false;
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::string path = "/realtime/v1/websocket?apikey=" + aoraton_url_encode(config.key) + "&vsn=1.0.0";
    std::wstring wide_path(path.begin(), path.end());
    wipe(path);
    session.value = WinHttpOpen(L"AURA-Aoraton-Realtime/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.value) return false;
    WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    WinHttpSetOption(session.value, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    connection.value = WinHttpConnect(session.value, host.c_str(), parts.nPort, 0);
    if (!connection.value) return false;
    Handle request;
    request.value = WinHttpOpenRequest(connection.value, L"GET", wide_path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    SecureZeroMemory(wide_path.data(), wide_path.size() * sizeof(wchar_t));
    if (!request.value) return false;
    DWORD disable = WINHTTP_DISABLE_REDIRECTS;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)) ||
        !WinHttpSetOption(request.value, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ||
        !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr))
        return false;
    DWORD status = 0, length = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX) ||
        status != 101)
        return false;
    socket.value = WinHttpWebSocketCompleteUpgrade(request.value, 0);
    if (!socket.value) return false;
    DWORD close_timeout = 100;
    WinHttpSetOption(socket.value, WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT, &close_timeout, sizeof(close_timeout));
    return true;
}

bool send_frame(HINTERNET socket, std::string text) {
    const DWORD result = WinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, text.data(),
                                              static_cast<DWORD>(text.size()));
    wipe(text);
    return result == NO_ERROR;
}
}  // namespace

struct WindowsAoratonRealtime::Impl {
    std::mutex mutex;
    std::condition_variable changed;
    Config desired;
    std::uint64_t generation{0};
    bool stopping{false};
    bool enabled{false};
    bool reported_active{false};
    AoratonRealtimeUpdate update;
    std::thread owner;

    Impl() : owner([this] { run(); }) {}
    ~Impl() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            ++generation;
            desired.clear();
        }
        changed.notify_all();
        owner.join();
    }
    bool current(std::uint64_t version) {
        std::lock_guard<std::mutex> lock(mutex);
        return !stopping && enabled && generation == version;
    }
    void publish(std::uint64_t version, bool active, bool refresh) {
        std::lock_guard<std::mutex> lock(mutex);
        if (generation != version || !enabled || stopping) return;
        update.refresh_required = update.refresh_required || refresh || (active && !update.active);
        update.active = active;
    }
    void connected(const Config& config, std::uint64_t version) {
        Handle session, connection, socket;
        if (!open_socket(config, session, connection, socket) || !current(version)) return;
        AoratonRealtimeProtocol protocol(config.rooms);
        auto joins = protocol.joins(config.token);
        if (joins.size() != config.rooms.size()) return;
        bool sent = true;
        for (auto& frame : joins) {
            if (sent) sent = send_frame(socket.value, frame);
            wipe(frame);
        }
        if (!sent) return;
        std::atomic<bool> failed{false};
        std::atomic<bool> ready{false};
        std::atomic<std::uint64_t> acknowledged{0};
        const auto opened = Clock::now();
        std::thread receiver([&] {
            std::array<char, 8192> buffer{};
            std::string frame;
            while (current(version)) {
                DWORD bytes = 0;
                WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
                const auto result = WinHttpWebSocketReceive(socket.value, buffer.data(),
                                                            static_cast<DWORD>(buffer.size()), &bytes, &type);
                if (result != NO_ERROR || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
                if ((type != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE &&
                     type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) ||
                    frame.size() + bytes > AoratonRealtimeProtocol::kMaxFrameBytes)
                    break;
                frame.append(buffer.data(), bytes);
                SecureZeroMemory(buffer.data(), buffer.size());
                if (type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) continue;
                if (!protocol.receive(frame)) break;
                wipe(frame);
                const auto& reply = protocol.heartbeat_reply();
                if (reply.size() > 1 && reply[0] == 'h') {
                    char* end = nullptr;
                    const auto number = std::strtoull(reply.c_str() + 1, &end, 10);
                    if (end && *end == '\0') acknowledged = number;
                }
                ready = protocol.active();
                publish(version, ready, protocol.take_refresh());
            }
            wipe(frame);
            SecureZeroMemory(buffer.data(), buffer.size());
            failed = true;
            changed.notify_all();
        });
        auto last_heartbeat = opened;
        std::uint64_t heartbeat = 0;
        while (current(version) && !failed) {
            const auto now = Clock::now();
            // Joining and heartbeat acknowledgements are bounded; a silent
            // half-open socket cannot keep the UI claiming realtime forever.
            if ((!ready && now - opened > std::chrono::seconds(15)) ||
                (heartbeat != acknowledged && now - last_heartbeat > std::chrono::seconds(10)))
                break;
            if (now - last_heartbeat >= std::chrono::seconds(20)) {
                ++heartbeat;
                if (!send_frame(socket.value, protocol.heartbeat("h" + std::to_string(heartbeat)))) break;
                last_heartbeat = now;
            }
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait_for(lock, std::chrono::milliseconds(100),
                             [&] { return stopping || generation != version || failed; });
        }
        // Close cancels the outstanding synchronous receive (Microsoft's
        // WebSocket contract); only the owner destroys the handle afterwards.
        WinHttpWebSocketClose(socket.value, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
        receiver.join();
    }
    void run() {
        unsigned failures = 0;
        std::uint64_t previous = 0;
        for (;;) {
            Config config;
            std::uint64_t version;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [&] { return stopping || enabled; });
                if (stopping) return;
                config = desired;
                version = generation;
            }
            if (version != previous) failures = 0;
            previous = version;
            const auto started = Clock::now();
            connected(config, version);
            publish(version, false, false);
            if (Clock::now() - started > std::chrono::seconds(30)) failures = 0;
            const auto delay = std::chrono::seconds(std::min(30U, 1U << std::min(failures++, 5U)));
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait_for(lock, delay, [&] { return stopping || generation != version; });
        }
    }
};

WindowsAoratonRealtime::WindowsAoratonRealtime() : impl_(std::make_unique<Impl>()) {}
WindowsAoratonRealtime::~WindowsAoratonRealtime() = default;
void WindowsAoratonRealtime::configure(const std::string& base, const std::string& key, const std::string& token,
                                       const std::vector<std::string>& rooms) {
    if (rooms.empty() || rooms.size() > AoratonRealtimeProtocol::kMaxRooms || token.empty() || token.size() > 16384 ||
        std::any_of(rooms.begin(), rooms.end(),
                    [](const std::string& room) { return !AoratonRealtimeProtocol::valid_room(room); })) {
        stop();
        return;
    }
    Config next;
    next.base = base;
    next.key = key;
    next.token = token;
    next.rooms = rooms;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->enabled && impl_->desired == next) return;
        impl_->desired.clear();
        impl_->desired = next;
        impl_->enabled = true;
        ++impl_->generation;
        impl_->update = {};
    }
    impl_->changed.notify_all();
}
AoratonRealtimeUpdate WindowsAoratonRealtime::poll() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto update = impl_->update;
    impl_->update.refresh_required = false;
    if (update.active != impl_->reported_active) {
        impl_->reported_active = update.active;
        // Status codes only: no endpoint, room identifiers, JWT or frame data.
        std::clog << "AURA Aoraton realtime: " << (update.active ? "SUBSCRIBED" : "POLLING_FALLBACK") << std::endl;
    }
    return update;
}
void WindowsAoratonRealtime::stop() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->enabled = false;
        ++impl_->generation;
        impl_->desired.clear();
        impl_->update = {};
    }
    impl_->changed.notify_all();
}
}  // namespace aura::simulator

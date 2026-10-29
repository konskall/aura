#include "aura/windows_radio_service.hpp"

#include "aura/mp3_decoder.hpp"
#include "aura/windows_audio_backend.hpp"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>

namespace aura::simulator {
namespace {

constexpr std::size_t kMaxSearchBytes = 512U * 1024U;
constexpr std::size_t kMaxEvents = 16;
constexpr std::uint32_t kHighWaterMs = 3500;
constexpr std::uint32_t kLowWaterMs = 1800;
constexpr int kReadTimeout = -1;
constexpr int kReadError = -2;
// About ten seconds of silence from the server ends the attempt.
constexpr int kMaxQuietReads = 4;
// A stream that sends this much and nothing playable is not MP3.
constexpr std::size_t kMaxBytesBeforeAudio = 512U * 1024U;

class InternetHandle final {
public:
    explicit InternetHandle(HINTERNET value = nullptr) : value_(value) {}
    ~InternetHandle() { if (value_) WinHttpCloseHandle(value_); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    InternetHandle(InternetHandle&& other) noexcept : value_(other.value_) { other.value_ = nullptr; }
    InternetHandle& operator=(InternetHandle&& other) noexcept {
        if (this == &other) return *this;
        if (value_) WinHttpCloseHandle(value_);
        value_ = other.value_;
        other.value_ = nullptr;
        return *this;
    }
    operator HINTERNET() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ != nullptr; }
private:
    HINTERNET value_;
};

struct UrlParts {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port{0};
    bool secure{false};
};

std::wstring widen_ascii(const std::string& value) { return std::wstring(value.begin(), value.end()); }

bool crack_url(const std::string& url, UrlParts& parts) {
    if (url.empty() || url.size() > 2048) return false;
    for (const unsigned char ch : url) {
        if (ch < 0x21 || ch > 0x7E) return false;
    }
    std::wstring wide = widen_ascii(url);
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &components)) return false;
    if (components.nScheme != INTERNET_SCHEME_HTTP && components.nScheme != INTERNET_SCHEME_HTTPS) return false;
    parts.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    parts.host.assign(components.lpszHostName, components.dwHostNameLength);
    parts.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.lpszExtraInfo) parts.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    if (parts.path.empty()) parts.path = L"/";
    parts.port = components.nPort;
    return !parts.host.empty();
}

InternetHandle open_session(const wchar_t* agent, int receive_timeout_ms) {
    InternetHandle session(WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0));
    if (session) {
        WinHttpSetTimeouts(session, 6000, 6000, 8000, receive_timeout_ms);
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
        WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
    return session;
}

bool status_code(HINTERNET request, DWORD& status) {
    DWORD size = sizeof(status);
    return WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) != FALSE;
}

std::size_t icy_metaint(HINTERNET request) {
    wchar_t value[32]{};
    DWORD size = sizeof(value);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"icy-metaint", value, &size,
                             WINHTTP_NO_HEADER_INDEX)) return 0;
    wchar_t* end = nullptr;
    const auto parsed = std::wcstoul(value, &end, 10);
    if (end == value || *end != L'\0' || parsed > 16U * 1024U * 1024U) return 0;
    return static_cast<std::size_t>(parsed);
}

// One directory request (`path`), tried on each radio-browser server in turn.
RadioSearchResult fetch_search_path(const std::string& path) {
    RadioSearchResult result;
    for (const auto& host : radio_browser_hosts()) {
        const std::string url = "https://" + host + path;
        UrlParts parts;
        if (!crack_url(url, parts)) continue;
        auto session = open_session(L"AURA-Radio/1.0", 12000);
        if (!session) continue;
        InternetHandle connection(WinHttpConnect(session, parts.host.c_str(), parts.port, 0));
        if (!connection) continue;
        InternetHandle request(WinHttpOpenRequest(connection, L"GET", parts.path.c_str(), nullptr,
                                                   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                   parts.secure ? WINHTTP_FLAG_SECURE : 0));
        if (!request) continue;
        constexpr wchar_t headers[] = L"Accept: application/json\r\nAccept-Encoding: identity\r\n";
        if (!WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request, nullptr)) continue;
        DWORD status = 0;
        if (!status_code(request, status) || status != 200) continue;
        std::string body;
        bool failed = false;
        while (true) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available)) { failed = true; break; }
            if (available == 0) break;
            if (body.size() + available > kMaxSearchBytes) { failed = true; break; }
            const auto old = body.size();
            body.resize(old + available);
            DWORD read = 0;
            if (!WinHttpReadData(request, body.data() + old, available, &read)) { failed = true; break; }
            body.resize(old + read);
        }
        if (failed) continue;
        if (!parse_radio_browser_stations(body, result.stations)) {
            result.error_code = "RADIO_DIRECTORY_INVALID";
            return result;
        }
        result.ok = true;
        return result;
    }
    result.error_code = "RADIO_DIRECTORY_UNAVAILABLE";
    return result;
}

RadioSearchResult fetch_search(const std::string& query) {
    return search_radio_directory(query, fetch_search_path);
}

bool start_winsock() {
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

}  // namespace

WindowsRadioService::WindowsRadioService(WindowsAudioBackend& audio)
    : audio_(audio), winsock_ready_(start_winsock()), worker_(&WindowsRadioService::run, this) {}

WindowsRadioService::~WindowsRadioService() {
    {
        std::lock_guard<std::mutex> lock(command_mutex_);
        shutting_down_ = true;
        command_pending_ = true;
        ++generation_;
    }
    abort_active();
    command_ready_.notify_one();
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> audio_lock(audio_control_mutex_);
        audio_.stream_close();
    }
    if (winsock_ready_) WSACleanup();
}

std::uint64_t WindowsRadioService::play(const RadioStation& station) {
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> audio_lock(audio_control_mutex_);
        generation = generation_.fetch_add(1) + 1;
        audio_.stream_close();
    }
    // The worker may be blocked reading the old station: cut that short.
    abort_active();
    {
        std::lock_guard<std::mutex> lock(command_mutex_);
        command_station_ = station;
        command_pending_ = true;
    }
    command_ready_.notify_one();
    return generation;
}

void WindowsRadioService::stop() {
    {
        std::lock_guard<std::mutex> audio_lock(audio_control_mutex_);
        ++generation_;
        audio_.stream_close();
    }
    abort_active();
    {
        std::lock_guard<std::mutex> lock(command_mutex_);
        command_station_.reset();
        command_pending_ = true;
    }
    command_ready_.notify_one();
}

void WindowsRadioService::set_hold(bool hold) { audio_.stream_hold(hold); }

std::future<RadioSearchResult> WindowsRadioService::search(std::string query) const {
    return std::async(std::launch::async, [query = std::move(query)] { return fetch_search(query); });
}

bool WindowsRadioService::poll(RadioWorkerEvent& event) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    if (events_.empty()) return false;
    event = std::move(events_.front());
    events_.pop_front();
    return true;
}

bool WindowsRadioService::current(std::uint64_t generation) const noexcept {
    return generation_.load() == generation;
}

void WindowsRadioService::publish(RadioWorkerEvent event) {
    if (!current(event.generation)) return;
    std::lock_guard<std::mutex> lock(event_mutex_);
    if (events_.size() >= kMaxEvents) events_.pop_front();
    events_.push_back(std::move(event));
}

bool WindowsRadioService::adopt_request(void* request, std::uint64_t generation) {
    std::lock_guard<std::mutex> lock(abort_mutex_);
    if (!current(generation)) {
        WinHttpCloseHandle(static_cast<HINTERNET>(request));
        return false;
    }
    active_request_ = request;
    return true;
}

void WindowsRadioService::release_request(void* request) {
    std::lock_guard<std::mutex> lock(abort_mutex_);
    if (active_request_ != request) return;  // already closed by abort_active()
    WinHttpCloseHandle(static_cast<HINTERNET>(request));
    active_request_ = nullptr;
}

bool WindowsRadioService::adopt_socket(std::uintptr_t socket, std::uint64_t generation) {
    std::lock_guard<std::mutex> lock(abort_mutex_);
    if (!current(generation)) {
        closesocket(static_cast<SOCKET>(socket));
        return false;
    }
    active_socket_ = socket;
    return true;
}

void WindowsRadioService::release_socket(std::uintptr_t socket) {
    std::lock_guard<std::mutex> lock(abort_mutex_);
    if (active_socket_ != socket) return;
    closesocket(static_cast<SOCKET>(socket));
    active_socket_ = kNoSocket;
}

void WindowsRadioService::abort_active() {
    std::lock_guard<std::mutex> lock(abort_mutex_);
    if (active_request_) {
        // Closing the request from this thread cancels the worker's blocking call.
        WinHttpCloseHandle(static_cast<HINTERNET>(active_request_));
        active_request_ = nullptr;
    }
    if (active_socket_ != kNoSocket) {
        const auto socket = static_cast<SOCKET>(active_socket_);
        shutdown(socket, SD_BOTH);
        closesocket(socket);
        active_socket_ = kNoSocket;
    }
}

void WindowsRadioService::run() {
    while (true) {
        std::optional<RadioStation> station;
        std::uint64_t generation = 0;
        {
            std::unique_lock<std::mutex> lock(command_mutex_);
            command_ready_.wait(lock, [&] { return command_pending_ || shutting_down_; });
            if (shutting_down_) return;
            command_pending_ = false;
            station = command_station_;
            generation = generation_.load();
        }
        if (station) stream(*station, generation);
    }
}

void WindowsRadioService::stream(const RadioStation& station, std::uint64_t generation) {
    // A live stream that drops (a network blip, a server restart) is joined
    // again with growing pauses before the owner is told; a long stretch of
    // music starts the count again.
    int attempt = 0;
    while (current(generation)) {
        bool heard_audio = false;
        const auto started = std::chrono::steady_clock::now();
        const auto code = stream_once(station, generation, heard_audio);
        if (!current(generation)) return;
        if (heard_audio && std::chrono::steady_clock::now() - started > std::chrono::seconds(30)) attempt = 0;
        const auto delay = radio_error_retryable(code) ? radio_retry_delay_ms(attempt) : 0U;
        if (delay == 0) {
            {
                std::lock_guard<std::mutex> audio_lock(audio_control_mutex_);
                if (current(generation)) audio_.stream_close();
            }
            publish({RadioWorkerEventType::Failed, generation, code});
            return;
        }
        ++attempt;
        publish({RadioWorkerEventType::Reconnecting, generation, code});
        for (std::uint32_t waited = 0; waited < delay && current(generation); waited += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

std::string WindowsRadioService::stream_once(const RadioStation& station, std::uint64_t generation,
                                             bool& heard_audio) {
    UrlParts parts;
    if (!crack_url(station.url, parts)) return "RADIO_INVALID_URL";
    auto session = open_session(L"AURA-Radio/1.0", 2500);
    if (!session) return "RADIO_NETWORK";
    InternetHandle connection(WinHttpConnect(session, parts.host.c_str(), parts.port, 0));
    if (!connection) return "RADIO_NETWORK";
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", parts.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, parts.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request) return "RADIO_NETWORK";
    if (!adopt_request(request, generation)) return {};
    constexpr wchar_t headers[] = L"Accept: audio/mpeg,*/*;q=0.5\r\nAccept-Encoding: identity\r\nIcy-MetaData: 1\r\n";
    if (!WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        const DWORD error = GetLastError();
        release_request(request);
        // Shoutcast v1 answers "ICY 200 OK", which WinHTTP rejects: plain
        // http:// streams are then read with a small HTTP/1.0 client.
        if (error == ERROR_WINHTTP_INVALID_SERVER_RESPONSE && !parts.secure && current(generation)) {
            return stream_icy(parts.host, parts.path, parts.port, generation, heard_audio);
        }
        return "RADIO_NETWORK";
    }
    DWORD status = 0;
    if (!status_code(request, status) || status < 200 || status >= 300) {
        release_request(request);
        return "RADIO_HTTP";
    }
    const auto metaint = icy_metaint(request);
    const StreamReader read = [request](std::uint8_t* buffer, int size) -> int {
        DWORD received = 0;
        if (!WinHttpReadData(request, buffer, static_cast<DWORD>(size), &received)) {
            return GetLastError() == ERROR_WINHTTP_TIMEOUT ? kReadTimeout : kReadError;
        }
        return static_cast<int>(received);
    };
    auto code = pump(read, metaint, generation, heard_audio);
    release_request(request);
    return code;
}

std::string WindowsRadioService::stream_icy(const std::wstring& host, const std::wstring& path,
                                            std::uint16_t port, std::uint64_t generation, bool& heard_audio) {
    if (!winsock_ready_) return "RADIO_NETWORK";
    ADDRINFOW hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    ADDRINFOW* addresses = nullptr;
    const auto service = std::to_wstring(port);
    if (GetAddrInfoW(host.c_str(), service.c_str(), &hints, &addresses) != 0 || !addresses) return "RADIO_NETWORK";
    SOCKET socket = INVALID_SOCKET;
    for (auto* address = addresses; address && socket == INVALID_SOCKET && current(generation);
         address = address->ai_next) {
        SOCKET candidate = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (candidate == INVALID_SOCKET) continue;
        // A bounded connect: an unreachable server must not hold the worker.
        u_long nonblocking = 1;
        ioctlsocket(candidate, FIONBIO, &nonblocking);
        bool connected = ::connect(candidate, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0;
        if (!connected && WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(candidate, &writable);
            timeval timeout{6, 0};
            connected = select(0, nullptr, &writable, nullptr, &timeout) > 0 && FD_ISSET(candidate, &writable);
        }
        if (!connected) {
            closesocket(candidate);
            continue;
        }
        nonblocking = 0;
        ioctlsocket(candidate, FIONBIO, &nonblocking);
        const DWORD receive_timeout = 2500;
        setsockopt(candidate, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&receive_timeout),
                   sizeof(receive_timeout));
        socket = candidate;
    }
    FreeAddrInfoW(addresses);
    if (socket == INVALID_SOCKET) return current(generation) ? "RADIO_NETWORK" : std::string{};
    const auto handle = static_cast<std::uintptr_t>(socket);
    if (!adopt_socket(handle, generation)) return {};

    // HTTP/1.0, which Shoutcast v1 answers with "ICY 200 OK"; the URL is ASCII.
    const std::string request = "GET " + std::string(path.begin(), path.end()) + " HTTP/1.0\r\nHost: " +
                                std::string(host.begin(), host.end()) +
                                "\r\nUser-Agent: AURA-Radio/1.0\r\nAccept: audio/mpeg,*/*;q=0.5\r\n"
                                "Icy-MetaData: 1\r\nConnection: close\r\n\r\n";
    if (send(socket, request.data(), static_cast<int>(request.size()), 0) != static_cast<int>(request.size())) {
        release_socket(handle);
        return "RADIO_NETWORK";
    }
    // The head, bounded; whatever follows it is already stream data.
    std::string head;
    std::array<char, 2048> chunk{};
    std::size_t head_end = std::string::npos;
    while (head_end == std::string::npos && head.size() < 8192 && current(generation)) {
        const int received = recv(socket, chunk.data(), static_cast<int>(chunk.size()), 0);
        if (received <= 0) {
            release_socket(handle);
            return "RADIO_NETWORK";
        }
        head.append(chunk.data(), static_cast<std::size_t>(received));
        head_end = head.find("\r\n\r\n");
    }
    StreamResponseHead parsed;
    if (head_end == std::string::npos ||
        !parse_stream_response_head(std::string_view(head).substr(0, head_end + 4), parsed)) {
        release_socket(handle);
        return current(generation) ? "RADIO_HTTP" : std::string{};
    }
    std::string leftover = head.substr(head_end + 4);
    const StreamReader read = [&leftover, socket](std::uint8_t* buffer, int size) -> int {
        if (!leftover.empty()) {
            const auto count = std::min<std::size_t>(leftover.size(), static_cast<std::size_t>(size));
            std::memcpy(buffer, leftover.data(), count);
            leftover.erase(0, count);
            return static_cast<int>(count);
        }
        const int received = recv(socket, reinterpret_cast<char*>(buffer), size, 0);
        if (received >= 0) return received;
        return WSAGetLastError() == WSAETIMEDOUT ? kReadTimeout : kReadError;
    };
    auto code = pump(read, parsed.metaint, generation, heard_audio);
    release_socket(handle);
    return code;
}

std::string WindowsRadioService::pump(const StreamReader& read, std::size_t metaint, std::uint64_t generation,
                                      bool& heard_audio) {
    IcyStreamReader icy(metaint);
    Mp3StreamDecoder decoder;
    std::array<std::uint8_t, 16U * 1024U> input{};
    std::vector<std::uint8_t> audio_bytes;
    std::size_t received_total = 0;
    int quiet_reads = 0;
    while (current(generation)) {
        const int received = read(input.data(), static_cast<int>(input.size()));
        if (received == kReadTimeout) {
            if (++quiet_reads >= kMaxQuietReads) return "RADIO_TIMEOUT";
            continue;
        }
        if (received < 0) return "RADIO_NETWORK";
        if (received == 0) return heard_audio ? "RADIO_STREAM_ENDED" : "RADIO_DECODE";
        quiet_reads = 0;
        received_total += static_cast<std::size_t>(received);
        if (!heard_audio && received_total > kMaxBytesBeforeAudio) return "RADIO_DECODE";
        audio_bytes.clear();
        icy.feed(input.data(), static_cast<std::size_t>(received), audio_bytes);
        std::string title;
        if (icy.take_title(title) && !title.empty()) publish({RadioWorkerEventType::Title, generation, std::move(title)});
        decoder.feed(audio_bytes.data(), audio_bytes.size());
        while (current(generation)) {
            Mp3Pcm pcm;
            const auto frames = decoder.decode(pcm, 1152U * 8U);
            if (frames == 0) break;
            bool audio_ok = false;
            {
                std::lock_guard<std::mutex> audio_lock(audio_control_mutex_);
                audio_ok = current(generation) && audio_.stream_open(pcm.sample_rate, pcm.channels) &&
                           audio_.stream_push(pcm.samples.data(), frames);
            }
            if (!audio_ok) return current(generation) ? "RADIO_AUDIO" : std::string{};
            if (!heard_audio) {
                heard_audio = true;
                publish({RadioWorkerEventType::Playing, generation, {}});
            }
            while (current(generation) && audio_.stream_buffered_ms() > kHighWaterMs) {
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
                if (audio_.stream_buffered_ms() <= kLowWaterMs) break;
            }
        }
    }
    return {};
}

}  // namespace aura::simulator

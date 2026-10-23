#include "aura/loopback_sign_in.hpp"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>

#include <cstring>
#include <string>

namespace aura::simulator {
namespace {

constexpr char kCallbackPath[] = "/aoraton/callback";

constexpr char kDonePage[] =
    "<!doctype html><html lang=\"el\"><head><meta charset=\"utf-8\"><title>AURA</title></head>"
    "<body style=\"font-family:sans-serif;background:#0b0d12;color:#e8ecf3;text-align:center;padding:48px\">"
    "<h1>AURA</h1><p>Η σύνδεση ολοκληρώθηκε. Μπορείς να κλείσεις αυτή την καρτέλα.</p>"
    "<p>Sign-in complete. You can close this tab.</p></body></html>";

constexpr char kFailedPage[] =
    "<!doctype html><html lang=\"el\"><head><meta charset=\"utf-8\"><title>AURA</title></head>"
    "<body style=\"font-family:sans-serif;background:#0b0d12;color:#e8ecf3;text-align:center;padding:48px\">"
    "<h1>AURA</h1><p>Η σύνδεση ακυρώθηκε.</p><p>Sign-in was cancelled.</p></body></html>";

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string query_value(const std::string& query, const char* name) {
    const std::string key = std::string(name) + "=";
    std::size_t at = 0;
    while (at < query.size()) {
        const std::size_t end = query.find('&', at);
        const std::string pair = query.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (pair.compare(0, key.size(), key) == 0) {
            std::string out;
            for (std::size_t index = key.size(); index < pair.size(); ++index) {
                if (pair[index] == '%' && index + 2 < pair.size() && hex_digit(pair[index + 1]) >= 0 &&
                    hex_digit(pair[index + 2]) >= 0) {
                    out.push_back(static_cast<char>((hex_digit(pair[index + 1]) << 4) | hex_digit(pair[index + 2])));
                    index += 2;
                } else {
                    out.push_back(pair[index] == '+' ? ' ' : pair[index]);
                }
            }
            return out;
        }
        if (end == std::string::npos) break;
        at = end + 1;
    }
    return {};
}

void respond(SOCKET client, const char* status, const char* body) {
    const std::string header = std::string("HTTP/1.1 ") + status +
                               "\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\n"
                               "Connection: close\r\nContent-Length: " + std::to_string(std::strlen(body)) + "\r\n\r\n";
    ::send(client, header.data(), static_cast<int>(header.size()), 0);
    ::send(client, body, static_cast<int>(std::strlen(body)), 0);
}

bool default_browser(const std::string& url) {
    const std::wstring wide(url.begin(), url.end());
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
}

}  // namespace

LoopbackSignInChannel::LoopbackSignInChannel(std::uint16_t port, BrowserOpener opener)
    : port_(port), opener_(opener ? std::move(opener) : BrowserOpener(default_browser)) {}

LoopbackSignInChannel::~LoopbackSignInChannel() { close(); }

std::string LoopbackSignInChannel::redirect_url(std::uint16_t port) {
    return "http://127.0.0.1:" + std::to_string(port) + kCallbackPath;
}

bool LoopbackSignInChannel::open(const std::string& authorize_url) {
    close();
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }
    BOOL exclusive = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || ::listen(listener, 4) != 0) {
        closesocket(listener);
        WSACleanup();
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        code_.clear();
        received_ = false;
        failed_ = false;
    }
    stopping_ = false;
    thread_ = std::thread([this, listener] { serve(static_cast<std::uintptr_t>(listener)); });
    if (!opener_(authorize_url)) {
        close();
        return false;
    }
    return true;
}

void LoopbackSignInChannel::serve(std::uintptr_t raw_listener) {
    const SOCKET listener = static_cast<SOCKET>(raw_listener);
    while (!stopping_) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listener, &readable);
        timeval timeout{0, 200000};
        if (select(0, &readable, nullptr, nullptr, &timeout) <= 0) continue;
        SOCKET client = ::accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        DWORD receive_timeout = 3000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&receive_timeout),
                   sizeof(receive_timeout));
        std::string request;
        char buffer[1024];
        while (request.size() < 8192 && request.find("\r\n\r\n") == std::string::npos) {
            const int read = ::recv(client, buffer, sizeof(buffer), 0);
            if (read <= 0) break;
            request.append(buffer, static_cast<std::size_t>(read));
        }
        const std::size_t line_end = request.find("\r\n");
        const std::string line = request.substr(0, line_end);
        bool done = false;
        if (line.compare(0, 4, "GET ") == 0) {
            const std::size_t target_end = line.find(' ', 4);
            const std::string target = line.substr(4, target_end == std::string::npos ? std::string::npos : target_end - 4);
            const std::size_t question = target.find('?');
            const std::string path = target.substr(0, question);
            const std::string query = question == std::string::npos ? std::string{} : target.substr(question + 1);
            if (path == kCallbackPath) {
                std::string code = query_value(query, "code");
                std::lock_guard<std::mutex> lock(mutex_);
                if (!code.empty() && code.size() <= 512) {
                    code_ = std::move(code);
                    received_ = true;
                    respond(client, "200 OK", kDonePage);
                } else {
                    failed_ = true;
                    respond(client, "200 OK", kFailedPage);
                }
                done = true;
            }
        }
        if (!done) respond(client, "404 Not Found", "<!doctype html><title>404</title>");
        SecureZeroMemory(request.data(), request.size());
        shutdown(client, SD_SEND);
        closesocket(client);
        if (done) break;
    }
    closesocket(listener);
}

AoratonCallbackStatus LoopbackSignInChannel::poll(std::string& code) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (received_) {
        code = std::move(code_);
        code_.clear();
        received_ = false;
        return AoratonCallbackStatus::Received;
    }
    if (failed_) {
        failed_ = false;
        return AoratonCallbackStatus::Failed;
    }
    return AoratonCallbackStatus::Pending;
}

void LoopbackSignInChannel::close() {
    stopping_ = true;
    if (thread_.joinable()) {
        thread_.join();
        WSACleanup();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    SecureZeroMemory(code_.data(), code_.size());
    code_.clear();
}

}  // namespace aura::simulator

#include "aura/winhttp_client.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <string>

namespace aura::simulator {
namespace {

std::wstring widen_ascii(const std::string& value) { return std::wstring(value.begin(), value.end()); }

class Handle final {
public:
    explicit Handle(HINTERNET value = nullptr) : value_(value) {}
    ~Handle() { if (value_) WinHttpCloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
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

bool crack(const std::string& url, UrlParts& parts) {
    if (url.empty() || url.size() > 8192) return false;
    for (const char c : url) {
        if (static_cast<unsigned char>(c) < 0x21 || static_cast<unsigned char>(c) > 0x7E) return false;
    }
    std::wstring wide = widen_ascii(url);
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &components)) return false;
    if (components.nScheme != INTERNET_SCHEME_HTTPS && components.nScheme != INTERNET_SCHEME_HTTP) return false;
    parts.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    parts.host.assign(components.lpszHostName, components.dwHostNameLength);
    parts.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.lpszExtraInfo) parts.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    if (parts.path.empty()) parts.path = L"/";
    parts.port = components.nPort;
    return !parts.host.empty();
}

}  // namespace

WinHttpClient::WinHttpClient() {
    session_ = WinHttpOpen(L"AURA-Aoraton/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);
    if (session_) {
        WinHttpSetTimeouts(static_cast<HINTERNET>(session_), 10000, 10000, 15000, 15000);
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
        WinHttpSetOption(static_cast<HINTERNET>(session_), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                         sizeof(protocols));
    }
}

WinHttpClient::~WinHttpClient() {
    if (session_) WinHttpCloseHandle(static_cast<HINTERNET>(session_));
}

AoratonHttpResponse WinHttpClient::send(const AoratonHttpRequest& request) {
    AoratonHttpResponse response;
    UrlParts parts;
    if (!session_ || !crack(request.url, parts)) return response;
    Handle connection(WinHttpConnect(static_cast<HINTERNET>(session_), parts.host.c_str(), parts.port, 0));
    if (!connection) return response;
    const std::wstring method = widen_ascii(request.method);
    Handle handle(WinHttpOpenRequest(connection, method.c_str(), parts.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, parts.secure ? WINHTTP_FLAG_SECURE : 0));
    if (!handle) return response;
    DWORD disable = WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(handle, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));
    if (request.timeout_ms != 0) {
        const int wait = static_cast<int>(request.timeout_ms);
        WinHttpSetTimeouts(handle, 10000, 10000, wait, wait);
    }
    std::wstring headers = L"Accept: application/json\r\nAccept-Encoding: identity\r\n";
    for (const auto& header : request.headers) {
        headers += widen_ascii(header.first) + L": " + widen_ascii(header.second) + L"\r\n";
    }
    const BOOL sent = WinHttpSendRequest(handle, headers.c_str(), static_cast<DWORD>(-1L),
                                         request.body.empty() ? WINHTTP_NO_REQUEST_DATA
                                                              : const_cast<char*>(request.body.data()),
                                         static_cast<DWORD>(request.body.size()),
                                         static_cast<DWORD>(request.body.size()), 0);
    SecureZeroMemory(headers.data(), headers.size() * sizeof(wchar_t));
    if (!sent || !WinHttpReceiveResponse(handle, nullptr)) return response;
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             &status, &size, WINHTTP_NO_HEADER_INDEX)) return response;
    std::string body;
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(handle, &available)) return response;
        if (available == 0) break;
        if (body.size() + available > kMaxResponseBytes) return response;
        const std::size_t old_size = body.size();
        body.resize(old_size + available);
        DWORD read = 0;
        if (!WinHttpReadData(handle, body.data() + old_size, available, &read)) return response;
        body.resize(old_size + read);
    }
    response.status = static_cast<int>(status);
    response.body = std::move(body);
    return response;
}

int WinHttpClient::fetch_following_redirects(const std::string& url) {
    std::string current = url;
    for (int hop = 0; hop < 6; ++hop) {
        UrlParts parts;
        if (!session_ || !crack(current, parts)) return 0;
        Handle connection(WinHttpConnect(static_cast<HINTERNET>(session_), parts.host.c_str(), parts.port, 0));
        if (!connection) return 0;
        Handle handle(WinHttpOpenRequest(connection, L"GET", parts.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, parts.secure ? WINHTTP_FLAG_SECURE : 0));
        if (!handle) return 0;
        DWORD disable = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(handle, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));
        if (!WinHttpSendRequest(handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(handle, nullptr)) return 0;
        DWORD status = 0;
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status < 300 || status >= 400) return static_cast<int>(status);
        wchar_t location[2048] = {};
        DWORD location_size = sizeof(location);
        if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, location,
                                 &location_size, WINHTTP_NO_HEADER_INDEX)) return static_cast<int>(status);
        current.clear();
        for (const wchar_t* c = location; *c; ++c) current.push_back(static_cast<char>(*c & 0x7F));
    }
    return 0;
}

}  // namespace aura::simulator

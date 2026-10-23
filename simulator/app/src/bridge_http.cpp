#include "aura/bridge_http.hpp"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace aura::simulator {
namespace {

void append_utf8(std::string& output, std::uint32_t code) {
    if (code < 0x80) {
        output.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        output.push_back(static_cast<char>(0xC0 | (code >> 6)));
        output.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        output.push_back(static_cast<char>(0xE0 | (code >> 12)));
        output.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | (code >> 18)));
        output.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

bool read_hex4(const std::string& json, std::size_t position, std::uint32_t& value) {
    if (position + 4 > json.size()) return false;
    value = 0;
    for (std::size_t index = position; index < position + 4; ++index) {
        const char ch = json[index];
        value <<= 4U;
        if (ch >= '0' && ch <= '9') value |= static_cast<std::uint32_t>(ch - '0');
        else if (ch >= 'a' && ch <= 'f') value |= static_cast<std::uint32_t>(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F') value |= static_cast<std::uint32_t>(ch - 'A' + 10);
        else return false;
    }
    return true;
}

std::size_t value_position(const std::string& json, const std::string& key) {
    const auto marker = '"' + key + '"';
    auto position = json.find(marker);
    if (position == std::string::npos) return std::string::npos;
    position = json.find(':', position + marker.size());
    if (position == std::string::npos) return std::string::npos;
    return json.find_first_not_of(" \t\r\n", position + 1);
}

}  // namespace

std::string json_string(const std::string& json, const std::string& key) {
    auto position = value_position(json, key);
    if (position == std::string::npos || json[position] != '"') return {};
    ++position;
    std::string value;
    while (position < json.size()) {
        const char ch = json[position++];
        if (ch == '"') break;
        if (ch != '\\' || position >= json.size()) {
            value.push_back(ch);
            continue;
        }
        const char escaped = json[position++];
        switch (escaped) {
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            case 'b':
            case 'f': break;  // display text: drop form controls
            case '"': value.push_back('"'); break;
            case '/': value.push_back('/'); break;
            case '\\': value.push_back('\\'); break;
            case 'u': {
                // \uXXXX, including surrogate pairs; control characters and
                // unpaired surrogates are dropped from untrusted display text.
                std::uint32_t code = 0;
                if (!read_hex4(json, position, code)) break;
                position += 4;
                if (code >= 0xD800 && code <= 0xDBFF) {
                    std::uint32_t low = 0;
                    if (position + 6 <= json.size() && json[position] == '\\' && json[position + 1] == 'u' &&
                        read_hex4(json, position + 2, low) && low >= 0xDC00 && low <= 0xDFFF) {
                        position += 6;
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    } else {
                        break;
                    }
                } else if (code >= 0xDC00 && code <= 0xDFFF) {
                    break;
                }
                if (code >= 0x20 && code != 0x7F) append_utf8(value, code);
                break;
            }
            default: break;
        }
    }
    return value;
}

bool json_bool(const std::string& json, const std::string& key, bool fallback) {
    const auto position = value_position(json, key);
    if (position == std::string::npos) return fallback;
    if (json.compare(position, 4, "true") == 0) return true;
    if (json.compare(position, 5, "false") == 0) return false;
    return fallback;
}

BridgeResponse bridge_request(const wchar_t* method, std::uint16_t port, const std::string& path,
                              const std::string& token, const std::string& body, const char* content_type,
                              int timeout_ms, std::size_t max_bytes) {
    BridgeResponse response;
#ifdef _WIN32
    HINTERNET session = WinHttpOpen(L"AURA-Simulator/0.2", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return response;
    WinHttpSetTimeouts(session, 3000, 3000, 3000, timeout_ms);
    HINTERNET connection = WinHttpConnect(session, L"127.0.0.1", port, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        return response;
    }
    const std::wstring wide_path(path.begin(), path.end());
    HINTERNET request = WinHttpOpenRequest(connection, method, wide_path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }
    std::wstring headers = L"Authorization: Bearer " + std::wstring(token.begin(), token.end()) +
                           L"\r\nOrigin: http://127.0.0.1\r\n";
    if (content_type) {
        const std::string type(content_type);
        headers += L"Content-Type: " + std::wstring(type.begin(), type.end()) + L"\r\n";
    }
    const BOOL sent = WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1L),
        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    if (sent && WinHttpReceiveResponse(request, nullptr)) {
        DWORD status_size = sizeof(response.status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &response.status, &status_size, WINHTTP_NO_HEADER_INDEX);
        wchar_t type_buffer[128] = {};
        DWORD type_size = sizeof(type_buffer);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, type_buffer,
                                &type_size, WINHTTP_NO_HEADER_INDEX)) {
            for (const wchar_t* character = type_buffer; *character; ++character) {
                if (*character < 0x80) response.content_type.push_back(static_cast<char>(*character));
            }
        }
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
            const auto old_size = response.body.size();
            response.body.resize(old_size + available);
            DWORD read = 0;
            if (!WinHttpReadData(request, response.body.data() + old_size, available, &read)) break;
            response.body.resize(old_size + read);
            if (response.body.size() > max_bytes) {
                response.status = 0;
                response.body.clear();
                break;
            }
        }
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
#else
    (void)method; (void)port; (void)path; (void)token; (void)body; (void)content_type; (void)timeout_ms;
    (void)max_bytes;
#endif
    return response;
}

}  // namespace aura::simulator

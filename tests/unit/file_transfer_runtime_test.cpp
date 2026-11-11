#include "aura/file_transfer_runtime.hpp"
#include "aura/photo_runtime.hpp"
#include "aura/text_util.hpp"
#include "aura/transfer_page.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <thread>
using namespace aura;
namespace {
void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
SOCKET connection(unsigned port) {
    auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        closesocket(socket);
        return INVALID_SOCKET;
    }
    DWORD timeout = 2500;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
    return socket;
}
void send_all(SOCKET socket, std::string_view text) {
    std::size_t done = 0;
    while (done < text.size()) {
        const int n = send(socket, text.data() + done, static_cast<int>(text.size() - done), 0);
        expect(n > 0, "test request sent");
        done += static_cast<unsigned>(n);
    }
}
struct Reply {
    int status;
    std::string body, headers;
};
Reply request(unsigned port, std::string_view method, std::string_view path, std::string_view body,
              std::string_view headers = {}) {
    const auto socket = connection(port);
    expect(socket != INVALID_SOCKET, "loopback server accepts connection");
    const auto text = std::string(method) + " " + std::string(path) +
                      " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                      "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n" +
                      std::string(headers) + "\r\n" + std::string(body);
    send_all(socket, text);
    std::string response;
    char data[4096];
    int n;
    while ((n = recv(socket, data, sizeof(data), 0)) > 0 && response.size() < 65536)
        response.append(data, static_cast<std::size_t>(n));
    closesocket(socket);
    const auto split = response.find("\r\n\r\n");
    expect(split != std::string::npos && response.size() > 12, "complete bounded HTTP response");
    return {std::atoi(response.c_str() + 9), response.substr(split + 4), response.substr(0, split)};
}
template <class Predicate>
void until(Predicate condition, const char* message) {
    for (unsigned i = 0; i < 300 && !condition(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    expect(condition(), message);
}
std::string encode(std::string_view text) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.')
            out += static_cast<char>(c);
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}
}  // namespace
int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    const auto parent = fs::absolute(fs::current_path() / "artifacts").lexically_normal();
    if (argc == 3 && std::string_view(argv[1]) == "--browser-qa") {
        const auto qa_root = fs::absolute(fs::u8path(argv[2])).lexically_normal();
        expect(qa_root.parent_path() == parent && qa_root.filename().string().find("AURA-transfer-browser-") == 0 &&
                   !fs::exists(qa_root),
               "browser fixture must use a fresh explicit workspace directory");
        fs::create_directories(qa_root);
        simulator::FileTransferRuntime runtime(qa_root.u8string(), false, "127.0.0.1", 0);
        expect(runtime.start(), "browser fixture starts");
        until([&] { return runtime.session().snapshot().state == TransferState::Ready; }, "browser fixture ready");
        const auto s = runtime.session().snapshot();
        // Explicit synthetic fixture mode. The harness consumes this in memory,
        // never writes/logs the code and never uses an owner's storage/account.
        std::cout << "DEMO_TRANSFER " << "{\"address\":\"" << s.address << "\",\"pin\":\"" << s.pin << "\"}"
                  << std::endl;
        std::string command;
        std::getline(std::cin, command);
        runtime.stop();
        return 0;
    }
    const auto root = parent / ("AURA-transfer-test-" + std::to_string(GetCurrentProcessId()));
    expect(root.parent_path() == parent, "test root stays under explicit workspace artifacts");
    fs::create_directories(root);
    WSADATA wsa{};
    expect(WSAStartup(MAKEWORD(2, 2), &wsa) == 0, "test client Winsock");
    unsigned port = 0;
    const std::string filename = "DEMO κείμενο.txt";
    {
        simulator::FileTransferRuntime runtime(root.u8string(), false, "127.0.0.1", 0);
        expect(runtime.session().snapshot().state == TransferState::Off && runtime.session().snapshot().address.empty(),
               "disabled on construction");
        auto activate = [&] {
            expect(runtime.start(), "explicit activation accepted");
            until([&] { return runtime.session().snapshot().state == TransferState::Ready; }, "real listener ready");
            const auto state = runtime.session().snapshot();
            port = static_cast<unsigned>(std::stoi(state.address.substr(state.address.rfind(':') + 1)));
            const auto page = request(port, "GET", "/", "");
            expect(page.status == 200 && page.body == l10n::transfer_page() &&
                       page.headers.find("Cache-Control: no-store") != std::string::npos,
                   "public page is static and has no session credentials");
            expect(page.headers.find("Content-Security-Policy: default-src 'none';") != std::string::npos &&
                       page.headers.find(" img-src data:;") != std::string::npos,
                   "the pages may show images embedded in them, and nothing from elsewhere");
            const auto login = request(port, "POST", "/api/session", "{\"pin\":\"" + state.pin + "\"}",
                                       "Content-Type: application/json\r\n");
            const auto json = parse_flat_json(login.body, 128);
            expect(login.status == 200 && json && json->string("token"), "real PIN authentication");
            return *json->string("token");
        };
        auto token = activate();
        const auto headers = [&](const std::string& name, const char* area) {
            return "Authorization: Bearer " + token +
                   "\r\nContent-Type: application/octet-stream\r\nX-Aura-Filename: " + encode(name) +
                   "\r\nX-Aura-Area: " + area + "\r\n";
        };
        const std::string text = "DEMO local transfer\n";
        expect(request(port, "POST", "/api/upload", text, headers(filename, "files")).status == 201,
               "real streamed file uploaded");
        const auto file_path = root / "files" / fs::u8path(filename);
        {
            std::ifstream in(file_path, std::ios::binary);
            const std::string stored{std::istreambuf_iterator<char>(in), {}};
            expect(stored == text, "Unicode destination preserves exact bytes");
        }
        expect(request(port, "POST", "/api/upload", "DEMO replacement", headers(filename, "files")).status == 409,
               "existing file cannot be replaced");
        {
            std::ifstream in(file_path, std::ios::binary);
            const std::string stored{std::istreambuf_iterator<char>(in), {}};
            expect(stored == text, "collision leaves old bytes untouched");
        }
        std::ifstream fixture(std::string(AURA_PHOTO_FIXTURES) + "/DEMO-image.png", std::ios::binary);
        const std::string photo{std::istreambuf_iterator<char>(fixture), {}};
        expect(request(port, "POST", "/api/upload", photo, headers("DEMO photo.png", "photos")).status == 201,
               "real PNG upload validated and committed");
        simulator::DesktopPhotoStorage gallery(root / "photos");
        std::vector<std::string> names;
        expect(gallery.scan(names) == PhotoError::None && names.size() == 1 &&
                   runtime.session().snapshot().photos_revision == 1,
               "uploaded image immediately available to existing gallery");
        expect(
            request(port, "POST", "/api/upload", "DEMO damaged image", headers("DEMO broken.png", "photos")).status ==
                400,
            "bad image rejected");
        expect(!fs::exists(root / "photos" / "DEMO broken.png") && !fs::exists(root / ".transfer" / "upload.part"),
               "failed image leaves no partial file");
        const auto json_headers = "Authorization: Bearer " + token + "\r\nContent-Type: application/json\r\n";
        expect(request(port, "POST", "/api/files", "{\"area\":\"files\",\"after\":\"\"}",
                       "Content-Type: application/json\r\n")
                       .status == 401,
               "listing requires the bearer");
        auto listed = request(port, "POST", "/api/files", "{\"area\":\"files\",\"after\":\"\"}", json_headers);
        expect(listed.status == 200 && listed.body.find(filename) != std::string::npos,
               "authenticated listing preserves Unicode names");
        const auto delete_body = "{\"area\":\"files\",\"name\":\"" + filename + "\",\"confirm\":true}";
        expect(request(port, "POST", "/api/delete", delete_body, "Content-Type: application/json\r\n").status == 401 &&
                   fs::exists(file_path),
               "unauthorized deletion has no effect");
        expect(request(port, "POST", "/api/delete",
                       "{\"area\":\"files\",\"name\":\"" + filename + "\",\"confirm\":false}", json_headers)
                           .status == 400 &&
                   fs::exists(file_path),
               "deletion requires explicit confirmation");
        expect(request(port, "POST", "/api/delete", "{\"area\":\"files\",\"name\":\"../escaped.txt\",\"confirm\":true}",
                       json_headers)
                       .status == 400,
               "delete traversal rejected");
        fs::create_directory(root / "files" / "DEMO directory.txt");
        expect(request(port, "POST", "/api/delete",
                       "{\"area\":\"files\",\"name\":\"DEMO directory.txt\",\"confirm\":true}", json_headers)
                           .status == 507 &&
                   fs::is_directory(root / "files" / "DEMO directory.txt"),
               "directories cannot be deleted");
        fs::remove(root / "files" / "DEMO directory.txt");
        expect(request(port, "POST", "/api/delete", delete_body, json_headers).status == 200 && !fs::exists(file_path),
               "confirmed exact Unicode file deleted");
        expect(request(port, "POST", "/api/delete", delete_body, json_headers).status == 404,
               "missing file has a distinct result");
        expect(request(port, "POST", "/api/delete",
                       "{\"area\":\"photos\",\"name\":\"DEMO photo.png\",\"confirm\":true}", json_headers)
                           .status == 200 &&
                   !fs::exists(root / "photos" / "DEMO photo.png") &&
                   runtime.session().snapshot().removals_revision == 1,
               "photo deletion publishes cache invalidation");
        expect(request(port, "POST", "/api/upload", text, headers("../escaped.txt", "files")).status == 400 &&
                   !fs::exists(root / "escaped.txt"),
               "encoded traversal cannot reach companion stores");
        {
            simulator::DesktopTransferStorage first(root), second(root);
            expect(first.begin(TransferArea::Files, "DEMO first.txt") == TransferError::None && first.seal(),
                   "first stage owns its lock through validation");
            expect(second.begin(TransferArea::Files, "DEMO second.txt") == TransferError::Storage,
                   "another simulator cannot truncate the active stage");
            first.cancel();
            expect(second.begin(TransferArea::Files, "DEMO second.txt") == TransferError::None,
                   "staging lock releases after cancellation");
            second.cancel();
        }
        expect(request(port, "POST", "/api/session", "{\"pin\":\"000000\"}",
                       "Content-Type: application/json\r\nOrigin: http://attacker.invalid\r\n")
                       .status == 403,
               "cross-origin login rejected");
        const auto partial = connection(port);
        expect(partial != INVALID_SOCKET, "partial stream opened");
        const auto partial_headers = "POST /api/upload HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                                     "\r\nContent-Length: 100000\r\n" + headers("DEMO interrupted.txt", "files") +
                                     "\r\nDEMO";
        send_all(partial, partial_headers);
        until([&] { return runtime.session().snapshot().state == TransferState::Receiving; },
              "receiving state published without UI access");
        runtime.stop();
        until([&] { return !fs::exists(root / ".transfer" / "upload.part"); }, "Stop removes partial upload");
        closesocket(partial);
        expect(!fs::exists(root / "files" / "DEMO interrupted.txt"), "cancelled upload not committed");
        until(
            [&] {
                const auto s = connection(port);
                if (s == INVALID_SOCKET) return true;
                closesocket(s);
                return false;
            },
            "Stop closes listener");
        token = activate();
        runtime.session().tick(std::numeric_limits<std::uint64_t>::max() - 1);
        until(
            [&] {
                const auto s = connection(port);
                if (s == INVALID_SOCKET) return true;
                closesocket(s);
                return false;
            },
            "expiry closes actual listener");
        expect(runtime.session().snapshot().state == TransferState::Expired && runtime.session().snapshot().pin.empty(),
               "expiry clears code");
    }
    // Remove exact synthetic paths only; never recursively traverse user files.
    fs::remove(root / "files" / fs::u8path(filename));
    fs::remove(root / "photos" / "DEMO photo.png");
    fs::remove(root / ".transfer" / "upload.part");
    fs::remove(root / ".transfer");
    fs::remove(root / "files");
    fs::remove(root / "photos");
    fs::remove(root);
    WSACleanup();
    std::cout
        << "Loopback transfer, Unicode files, photo validation, collisions, cancellation and socket expiry passed\n";
}

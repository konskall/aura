// Loopback only: a fake bridge on 127.0.0.1 streams a long answer to the
// simulator's provider. No real bridge, model provider or network service is
// contacted.
#include "aura/local_codex_provider.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// Answers one request on `listener` with `body` as a 200 NDJSON stream, sent
// in a single write so the client may receive chunks larger than 16 KiB.
void serve_once(SOCKET listener, std::string body) {
    const SOCKET client = accept(listener, nullptr, nullptr);
    if (client == INVALID_SOCKET) return;
    std::string request;
    char buffer[4096];
    for (;;) {
        const int read = recv(client, buffer, sizeof(buffer), 0);
        if (read <= 0) break;
        request.append(buffer, static_cast<std::size_t>(read));
        const auto end = request.find("\r\n\r\n");
        if (end == std::string::npos) continue;
        const auto length_at = request.find("Content-Length: ");
        const std::size_t length =
            length_at == std::string::npos ? 0 : std::strtoul(request.c_str() + length_at + 16, nullptr, 10);
        if (request.size() >= end + 4 + length) break;
    }
    const std::string response =
        "HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\nConnection: close\r\n\r\n" + body;
    send(client, response.data(), static_cast<int>(response.size()), 0);
    shutdown(client, SD_SEND);
    closesocket(client);
}

}  // namespace

int main() {
    WSADATA data;
    expect(WSAStartup(MAKEWORD(2, 2), &data) == 0, "winsock starts");
    const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    expect(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 && listen(listener, 1) == 0,
           "a loopback listener");
    int size = sizeof(address);
    expect(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) == 0, "an ephemeral port");
    const auto port = ntohs(address.sin_port);

    // A long answer streamed a word at a time, as Groq does: the deltas plus
    // the final "done" line are several times larger than the text itself.
    std::string text;
    std::string body;
    for (int index = 0; index < 1100; ++index) {
        text += "Λέξη ";
        body += "{\"type\":\"delta\",\"text\":\"Λέξη \"}\n";
    }
    body += "{\"type\":\"done\",\"text\":\"" + text + "\"}\n";
    expect(body.size() > 40000, "the stream is longer than 32 KB");
    std::thread server(serve_once, listener, body);

    aura::simulator::LocalCodexProvider provider("loopback-test-token", port, "codex");
    aura::TurnRequest request;
    request.prompt = "DEMO";
    const auto reply = provider.start_turn(std::move(request)).get();
    server.join();
    closesocket(listener);
    WSACleanup();
    expect(reply.ok, "a long streamed answer arrives");
    // Sanitizing trims only the final space.
    expect(reply.text.size() + 1 == text.size(), "nothing of the answer is cut");
    std::cout << "aura_bridge_stream_tests passed\n";
    return 0;
}

#include "aura/file_transfer_runtime.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <algorithm>
#include <chrono>

namespace aura::simulator {
namespace {
std::uint64_t now_ms() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
bool directory(const std::filesystem::path& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    for (auto p = std::filesystem::absolute(dir, ec).lexically_normal(); !p.empty();) {
        const auto a = GetFileAttributesW(p.c_str());
        if (a == INVALID_FILE_ATTRIBUTES) {
            const auto error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return false;
        } else if ((a & FILE_ATTRIBUTE_REPARSE_POINT) || !(a & FILE_ATTRIBUTE_DIRECTORY))
            return false;
        auto parent = p.parent_path();
        if (parent == p) break;
        p = std::move(parent);
    }
    if (ec) return false;
    std::filesystem::create_directories(dir, ec);
    return !ec;
}
bool normal_file(HANDLE file) {
    BY_HANDLE_FILE_INFORMATION info{};
    return GetFileInformationByHandle(file, &info) &&
           !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY));
}
std::string local_address() {
    ULONG size = 16384;
    std::vector<unsigned char> data(size);
    auto* first = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(data.data());
    auto result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, first, &size);
    if (result == ERROR_BUFFER_OVERFLOW && size <= 262144) {
        data.resize(size);
        first = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(data.data());
        result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, first, &size);
    }
    std::string selected = "127.0.0.1";
    unsigned best = 99;
    if (result != NO_ERROR) return selected;
    for (auto* adapter = first; adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || !adapter->FirstGatewayAddress ||
            (adapter->IfType != IF_TYPE_IEEE80211 && adapter->IfType != IF_TYPE_ETHERNET_CSMACD))
            continue;
        const unsigned score = adapter->IfType == IF_TYPE_IEEE80211 ? 0 : 1;
        for (auto* entry = adapter->FirstUnicastAddress; entry; entry = entry->Next) {
            char address[64]{};
            if (getnameinfo(entry->Address.lpSockaddr, entry->Address.iSockaddrLength, address, sizeof(address),
                            nullptr, 0, NI_NUMERICHOST) != 0 ||
                !transfer_private_ipv4(address) || std::string_view(address) == "127.0.0.1")
                continue;
            if (score < best) {
                selected = address;
                best = score;
            }
        }
    }
    return selected;
}
class SocketReader final : public ITransferReader {
public:
    SocketReader(SOCKET socket, std::string prefix) : socket_(socket), prefix_(std::move(prefix)) {}
    int read(std::uint8_t* data, std::size_t capacity) override {
        if (position_ < prefix_.size()) {
            const auto n = std::min(capacity, prefix_.size() - position_);
            std::copy_n(reinterpret_cast<const std::uint8_t*>(prefix_.data() + position_), n, data);
            position_ += n;
            consumed_ += n;
            return static_cast<int>(n);
        }
        const int n = recv(socket_, reinterpret_cast<char*>(data), static_cast<int>(capacity), 0);
        if (n > 0) consumed_ += static_cast<unsigned>(n);
        return n;
    }
    std::uint64_t consumed() const { return consumed_; }

private:
    SOCKET socket_;
    std::string prefix_;
    std::size_t position_{0};
    std::uint64_t consumed_{0};
};
void respond(SOCKET socket, const TransferResponse& response) {
    const auto body = response.body;
    const auto message =
        "HTTP/1.1 " + std::to_string(response.status) + " Response\r\nContent-Type: " +
        std::string(response.html ? "text/html; charset=utf-8" : "application/json; charset=utf-8") +
        "\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'\r\n\r\n" +
        body;
    std::size_t done = 0;
    while (done < message.size()) {
        const int n = send(socket, message.data() + done, static_cast<int>(message.size() - done), 0);
        if (n <= 0) break;
        done += static_cast<unsigned>(n);
    }
}
void serve(SOCKET socket, TransferSession& session, DesktopTransferStorage& storage) {
    const DWORD timeout = 1000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    std::string bytes;
    std::size_t end = std::string::npos;
    std::array<char, 1024> chunk{};
    while (bytes.size() <= 8192 && (end = bytes.find("\r\n\r\n")) == std::string::npos) {
        const int n = recv(socket, chunk.data(), static_cast<int>(chunk.size()), 0);
        if (n <= 0) return;
        bytes.append(chunk.data(), static_cast<std::size_t>(n));
    }
    TransferRequest request;
    if (end == std::string::npos || !parse_transfer_headers(std::string_view(bytes).substr(0, end + 4), request)) {
        respond(socket, {400, "{\"error\":\"name\"}", false});
        return;
    }
    SocketReader reader(socket, bytes.substr(end + 4));
    sockaddr_in peer{};
    int peer_size = sizeof(peer);
    char peer_address[64]{};
    request.local_peer = getpeername(socket, reinterpret_cast<sockaddr*>(&peer), &peer_size) == 0 &&
                         inet_ntop(AF_INET, &peer.sin_addr, peer_address, sizeof(peer_address)) &&
                         transfer_private_ipv4(peer_address);
    TransferProtocol protocol(session, storage, now_ms);
    respond(socket, protocol.handle(request, reader));
    // On an early rejection the browser may still be sending its body. Send
    // a graceful FIN and briefly drain it so a TCP reset cannot hide the JSON
    // error response (notably an existing filename) from XMLHttpRequest.
    shutdown(socket, SD_SEND);
    const DWORD drain_timeout = 100;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&drain_timeout), sizeof(drain_timeout));
    std::uint64_t remaining = request.length > reader.consumed()
                                  ? std::min<std::uint64_t>(request.length - reader.consumed(), kTransferFileLimit)
                                  : 0;
    const auto deadline = now_ms() + 500;
    std::array<std::uint8_t, 4096> discarded{};
    while (remaining && now_ms() < deadline) {
        const int n = reader.read(discarded.data(),
                                  static_cast<std::size_t>(std::min<std::uint64_t>(remaining, discarded.size())));
        if (n <= 0) break;
        remaining -= static_cast<unsigned>(n);
    }
}
}  // namespace
DesktopTransferStorage::~DesktopTransferStorage() { cancel(); }
TransferError DesktopTransferStorage::list(TransferArea area, const std::string& after,
                                           std::vector<TransferFile>& files, bool& more) {
    files.clear();
    more = false;
    const auto folder = root_ / (area == TransferArea::Photos ? "photos" : "files");
    if (root_.empty() || !directory(folder)) return TransferError::Storage;
    std::error_code ec;
    unsigned inspected = 0;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end && inspected < 2048;
         it.increment(ec), ++inspected) {
        const auto name = it->path().filename().u8string();
        if (name <= after || !transfer_filename_valid(name) ||
            (area == TransferArea::Photos && !photo_filename_valid(name)))
            continue;
        const auto file = CreateFileW(it->path().c_str(), FILE_READ_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE) continue;
        LARGE_INTEGER size{};
        if (normal_file(file) && GetFileSizeEx(file, &size) && size.QuadPart >= 0)
            transfer_list_add(files, {name, static_cast<std::uint64_t>(size.QuadPart)}, more);
        CloseHandle(file);
    }
    return ec ? TransferError::Storage : TransferError::None;
}
TransferError DesktopTransferStorage::erase(TransferArea area, const std::string& name) {
    if (!transfer_filename_valid(name) || (area == TransferArea::Photos && !photo_filename_valid(name)))
        return TransferError::Name;
    const auto folder = root_ / (area == TransferArea::Photos ? "photos" : "files");
    if (root_.empty() || !directory(folder)) return TransferError::Storage;
    const auto path = folder / std::filesystem::u8path(name);
    const auto file =
        CreateFileW(path.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND ? TransferError::Missing : TransferError::Storage;
    FILE_DISPOSITION_INFO disposition{TRUE};
    const bool ok =
        normal_file(file) && SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
    CloseHandle(file);
    return ok ? TransferError::None : TransferError::Storage;
}
TransferError DesktopTransferStorage::begin(TransferArea area, const std::string& name) {
    cancel();
    if (!transfer_filename_valid(name) || root_.empty() || !directory(root_ / ".transfer"))
        return TransferError::Storage;
    const auto folder = root_ / (area == TransferArea::Photos ? "photos" : "files");
    if (!directory(folder)) return TransferError::Storage;
    destination_ = folder / std::filesystem::u8path(name);
    if (GetFileAttributesW(destination_.c_str()) != INVALID_FILE_ATTRIBUTES) return TransferError::Exists;
    if (area == TransferArea::Photos) {
        std::error_code ec;
        std::size_t count = 0, inspected = 0;
        for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end && inspected < 2048;
             it.increment(ec), ++inspected) {
            const auto attributes = GetFileAttributesW(it->path().c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES &&
                !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
                photo_filename_valid(it->path().filename().u8string()))
                ++count;
        }
        if (ec) return TransferError::Storage;
        if (count >= kMaxPhotos) return TransferError::Limit;
    }
    stage_ = root_ / ".transfer" / "upload.part";
    const auto lock_path = root_ / ".transfer" / "active.lock";
    const auto lock = CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                                  FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return TransferError::Storage;
    if (!normal_file(lock)) {
        CloseHandle(lock);
        return TransferError::Storage;
    }
    lock_ = lock;
    const auto file = CreateFileW(stage_.c_str(), GENERIC_WRITE | GENERIC_READ, 0, nullptr, OPEN_ALWAYS,
                                  FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        cancel();
        return TransferError::Storage;
    }
    if (!normal_file(file) || SetFilePointer(file, 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
        !SetEndOfFile(file)) {
        CloseHandle(file);
        cancel();
        return TransferError::Storage;
    }
    handle_ = file;
    owned_ = true;
    return TransferError::None;
}
bool DesktopTransferStorage::write(const std::uint8_t* data, std::size_t size) {
    DWORD written = 0;
    return handle_ && WriteFile(static_cast<HANDLE>(handle_), data, static_cast<DWORD>(size), &written, nullptr) &&
           written == size;
}
bool DesktopTransferStorage::seal() {
    if (!handle_) return false;
    const bool ok = FlushFileBuffers(static_cast<HANDLE>(handle_)) != FALSE;
    CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
    return ok;
}
bool DesktopTransferStorage::read_staged(PhotoBytes& bytes) {
    const auto file = CreateFileW(stage_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = normal_file(file) && GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
              size.QuadPart <= static_cast<LONGLONG>(kMaxPhotoFileBytes) &&
              bytes.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    if (ok) ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size();
    CloseHandle(file);
    if (!ok) bytes.clear();
    return ok;
}
TransferError DesktopTransferStorage::commit() {
    if (!owned_ || handle_) return TransferError::Storage;
    if (!MoveFileExW(stage_.c_str(), destination_.c_str(), 0)) {
        const auto error = GetLastError();
        return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS ? TransferError::Exists
                                                                           : TransferError::Storage;
    }
    owned_ = false;
    return TransferError::None;
}
void DesktopTransferStorage::cancel() {
    if (handle_) {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
    if (owned_) DeleteFileW(stage_.c_str());
    owned_ = false;
    if (lock_) {
        CloseHandle(static_cast<HANDLE>(lock_));
        lock_ = nullptr;
    }
}
FileTransferRuntime::FileTransferRuntime(const std::string& root, bool deterministic, std::string bind_address,
                                         std::uint16_t port)
    : storage_(std::filesystem::u8path(root)),
      deterministic_(deterministic),
      has_root_(!root.empty()),
      bind_address_(std::move(bind_address)),
      port_(port) {
    if (!deterministic_ && has_root_) thread_ = std::thread([this] { run(); });
}
FileTransferRuntime::~FileTransferRuntime() {
    stopping_ = true;
    stop();
    if (thread_.joinable()) thread_.join();
}
bool FileTransferRuntime::start() {
    if (deterministic_) {
        session_.seed_demo();
        return true;
    }
    if (!has_root_) return false;
    std::array<std::uint8_t, 36> seed{};
    if (BCryptGenRandom(nullptr, seed.data(), static_cast<ULONG>(seed.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        return false;
    const bool ok = session_.start(seed, now_ms());
    SecureZeroMemory(seed.data(), seed.size());
    return ok;
}
void FileTransferRuntime::stop() { session_.stop(); }
void FileTransferRuntime::poll() {
    if (!deterministic_) session_.tick(now_ms());
}
bool FileTransferRuntime::open_browser() {
    const auto s = session_.snapshot();
    if (s.demo || !transfer_address_valid(s.address) ||
        (s.state != TransferState::Ready && s.state != TransferState::Receiving))
        return false;
    std::wstring address(s.address.begin(), s.address.end());
    return reinterpret_cast<std::intptr_t>(
               ShellExecuteW(nullptr, L"open", address.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}
void FileTransferRuntime::run() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
    while (!stopping_) {
        session_.tick(now_ms());
        const auto state = session_.snapshot();
        if (state.state != TransferState::Starting) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            continue;
        }
        const auto address = bind_address_.empty() ? local_address() : bind_address_;
        if (!transfer_private_ipv4(address)) {
            session_.fail(state.generation, TransferError::Network);
            continue;
        }
        const auto listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET) {
            session_.fail(state.generation, TransferError::Network);
            continue;
        }
        sockaddr_in endpoint{};
        endpoint.sin_family = AF_INET;
        endpoint.sin_port = htons(port_);
        const bool parsed = inet_pton(AF_INET, address.c_str(), &endpoint.sin_addr) == 1;
        if (!parsed || bind(listener, reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)) == SOCKET_ERROR ||
            listen(listener, 2) == SOCKET_ERROR) {
            closesocket(listener);
            session_.fail(state.generation, TransferError::Network);
            continue;
        }
        int size = sizeof(endpoint);
        getsockname(listener, reinterpret_cast<sockaddr*>(&endpoint), &size);
        session_.ready(state.generation, "http://" + address + ":" + std::to_string(ntohs(endpoint.sin_port)),
                       now_ms());
        while (!stopping_ && session_.valid(state.generation, now_ms())) {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(listener, &readable);
            timeval wait{0, 100000};
            const auto selected = select(0, &readable, nullptr, nullptr, &wait);
            if (selected == SOCKET_ERROR) break;
            if (selected > 0) {
                const auto client = accept(listener, nullptr, nullptr);
                if (client != INVALID_SOCKET) {
                    serve(client, session_, storage_);
                    shutdown(client, SD_BOTH);
                    closesocket(client);
                }
            }
            session_.tick(now_ms());
        }
        closesocket(listener);
        storage_.cancel();
        if (!stopping_ && session_.valid(state.generation, now_ms()))
            session_.fail(state.generation, TransferError::Network);
    }
    WSACleanup();
}
}  // namespace aura::simulator

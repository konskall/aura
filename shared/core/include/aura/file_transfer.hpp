#pragma once
#include "aura/cloud_keys.hpp"
#include "aura/photos.hpp"
#include <array>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace aura {
constexpr std::uint64_t kTransferLeaseMs = 600000;
constexpr std::uint64_t kTransferFileLimit = 16U * 1024U * 1024U;
enum class TransferState { Off, Starting, Ready, Receiving, Expired, Locked, Failed };
enum class TransferError {
    None,
    Network,
    Storage,
    Name,
    Size,
    Exists,
    Image,
    Interrupted,
    Auth,
    Limit,
    Dimensions,
    Missing
};
enum class TransferArea { Photos, Files };
struct TransferFile {
    std::string name;
    std::uint64_t bytes{0};
};
// Retain the first 128 lexicographic names after a cursor, with bounded memory.
void transfer_list_add(std::vector<TransferFile>& files, TransferFile file, bool& more);
bool transfer_filename_valid(std::string_view name);
bool transfer_address_valid(std::string_view address);
bool transfer_private_ipv4(std::string_view address);
std::string transfer_percent_decode(std::string_view value);
const char* transfer_error_code(TransferError error);
struct TransferSnapshot {
    TransferState state{TransferState::Off};
    TransferError error{TransferError::None};
    std::string address, pin, last_name;
    std::uint64_t received{0}, total{0}, photos_revision{0}, removals_revision{0};
    unsigned remaining_seconds{0}, completed{0};
    bool demo{false}, loopback{false};
    std::uint64_t sequence{0}, generation{0};
};
// Bounded shared worker state. Only the UI thread consumes this snapshot in LVGL.
// Session secrets are memory-only and never appear in URLs or logs.
class TransferSession {
public:
    bool start(const std::array<std::uint8_t, 36>& entropy, std::uint64_t now);
    void stop();
    void ready(std::uint64_t generation, std::string address, std::uint64_t now);
    void fail(std::uint64_t generation, TransferError error);
    void tick(std::uint64_t now);
    TransferSnapshot snapshot() const;
    std::string login(std::string_view pin, std::uint64_t now);
    std::uint64_t authorize(std::string_view bearer, std::uint64_t now);
    bool valid(std::uint64_t generation, std::uint64_t now);
    void progress(std::uint64_t generation, std::string_view name, std::uint64_t received, std::uint64_t total);
    void finished(std::uint64_t generation, TransferArea area, TransferError error);
    void removed(TransferArea area);
    void seed_demo();

private:
    bool active_locked(std::uint64_t now);
    void clear_secret();
    mutable std::mutex mutex_;
    TransferSnapshot state_;
    std::string token_;
    std::uint64_t expires_{0};
    unsigned attempts_{0}, displayed_percent_{0};
};
struct TransferRequest {
    std::string method, path, host, origin, authorization, content_type, filename, area;
    std::uint64_t length{0};
    bool local_peer{false};  // set by the transport, never by an HTTP header
};
// Strict bounded framing for the native socket adapter. ESP-IDF supplies its
// parsed request fields to the same application protocol below.
bool parse_transfer_headers(std::string_view headers, TransferRequest& request);
class ITransferReader {
public:
    virtual ~ITransferReader() = default;
    virtual int read(std::uint8_t* data, std::size_t capacity) = 0;
};
class ITransferStorage {
public:
    virtual ~ITransferStorage() = default;
    virtual TransferError begin(TransferArea area, const std::string& name) = 0;
    virtual bool write(const std::uint8_t* bytes, std::size_t size) = 0;
    virtual bool seal() = 0;
    virtual bool read_staged(PhotoBytes& bytes) = 0;
    virtual TransferError commit() = 0;  // atomic, must never overwrite a file
    virtual void cancel() = 0;
    virtual TransferError list(TransferArea, const std::string&, std::vector<TransferFile>&, bool&) {
        return TransferError::Storage;
    }
    virtual TransferError erase(TransferArea, const std::string&) { return TransferError::Storage; }
};
struct TransferResponse {
    int status{200};
    std::string body;
    bool html{false};
};
class TransferProtocol {
public:
    // `keys`, when the platform keeps the assistant's keys (the device), takes
    // them from POST /api/keys within the signed-in session.
    TransferProtocol(TransferSession& session, ITransferStorage& storage, std::function<std::uint64_t()> clock,
                     ICloudKeySink* keys = nullptr)
        : session_(session), storage_(storage), clock_(std::move(clock)), keys_(keys) {}
    TransferResponse handle(const TransferRequest& request, ITransferReader& reader);

private:
    TransferResponse store_keys(const TransferRequest& request, ITransferReader& reader);
    TransferSession& session_;
    ITransferStorage& storage_;
    std::function<std::uint64_t()> clock_;
    ICloudKeySink* keys_;
};
}  // namespace aura

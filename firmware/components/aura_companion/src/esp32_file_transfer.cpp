#include "esp32_file_transfer.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace aura::firmware {
namespace {
constexpr const char* kTag = "aura_transfer";
constexpr const char* kRoot = "/sdcard/aura";
constexpr const char* kStage = "/sdcard/aura/.transfer/upload.part";
std::uint64_t transfer_now() { return static_cast<std::uint64_t>(esp_timer_get_time() / 1000); }
std::string station_address() {
    auto* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info{};
    wifi_ap_record_t access_point{};
    if (!netif || !esp_netif_is_netif_up(netif) || esp_wifi_sta_get_ap_info(&access_point) != ESP_OK ||
        esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0)
        return {};
    char address[32]{};
    std::snprintf(address, sizeof(address), IPSTR, IP2STR(&info.ip));
    return transfer_private_ipv4(address) ? address : std::string{};
}
std::string header(httpd_req_t* req, const char* name) {
    const auto length = httpd_req_get_hdr_value_len(req, name);
    if (!length || length > 512) return {};
    std::string value(length + 1, '\0');
    if (httpd_req_get_hdr_value_str(req, name, value.data(), value.size()) != ESP_OK) return {};
    value.resize(length);
    return value;
}
class RequestReader final : public ITransferReader {
public:
    explicit RequestReader(httpd_req_t* request) : request_(request) {}
    int read(std::uint8_t* data, std::size_t capacity) override {
        return httpd_req_recv(request_, reinterpret_cast<char*>(data), capacity);
    }

private:
    httpd_req_t* request_;
};
// Every response's headers but its type. ESP-IDF keeps the pointers until it
// sends the headers: string literals here, a writer's own strings below.
void security_headers(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_hdr(req, "Content-Security-Policy", kTransferContentSecurityPolicy);
}
// A download (the owner's request, 2026-11-14): its headers, then the file in
// HTTP/1.1 chunks as it is read, since a whole file does not fit in memory. Only
// a complete file ends with HTTP's last chunk; a cut one ends with the
// connection, so the browser reports a failure instead of saving it.
class ResponseWriter final : public ITransferWriter {
public:
    explicit ResponseWriter(httpd_req_t* request) : request_(request) {}
    bool begin(const std::string& name, std::uint64_t bytes) override {
        disposition_ = transfer_attachment_header(name);
        announced_ = bytes;
        started_ms_ = transfer_now();
        httpd_resp_set_status(request_, "200 OK");
        httpd_resp_set_type(request_, "application/octet-stream");
        security_headers(request_);
        httpd_resp_set_hdr(request_, "Content-Disposition", disposition_.c_str());
        return true;
    }
    bool write(const std::uint8_t* data, std::size_t size) override {
        if (httpd_resp_send_chunk(request_, reinterpret_cast<const char*>(data), static_cast<ssize_t>(size)) != ESP_OK)
            return false;
        written_ += size;
        return true;
    }
    // ESP_FAIL makes the server close the connection of an unfinished download.
    esp_err_t finish() {
        ESP_LOGI(kTag, "download: %llu of %llu bytes sent in %llu ms", static_cast<unsigned long long>(written_),
                 static_cast<unsigned long long>(announced_),
                 static_cast<unsigned long long>(transfer_now() - started_ms_));
        return written_ == announced_ ? httpd_resp_send_chunk(request_, nullptr, 0) : ESP_FAIL;
    }

private:
    httpd_req_t* request_;
    std::string disposition_;
    std::uint64_t announced_{0};
    std::uint64_t written_{0};
    std::uint64_t started_ms_{0};
};
bool directory(const std::string& name) {
    mkdir(name.c_str(), 0775);
    struct stat info{};
    return stat(name.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}
}  // namespace
TransferError Esp32TransferStorage::begin(TransferArea area, const std::string& name) {
    cancel();
    if (!transfer_filename_valid(name) || !directory(std::string(kRoot) + "/.transfer")) return TransferError::Storage;
    const auto folder = std::string(kRoot) + (area == TransferArea::Photos ? "/photos" : "/files");
    if (!directory(folder)) return TransferError::Storage;
    destination_ = folder + "/" + name;
    struct stat info{};
    if (stat(destination_.c_str(), &info) == 0) return TransferError::Exists;
    if (area == TransferArea::Photos) {
        DIR* dir = opendir(folder.c_str());
        if (!dir) return TransferError::Storage;
        std::size_t count = 0, inspected = 0;
        while (const auto* entry = readdir(dir)) {
            if (++inspected > 2048) break;
            struct stat entry_info{};
            if (photo_filename_valid(entry->d_name) && stat((folder + "/" + entry->d_name).c_str(), &entry_info) == 0 &&
                S_ISREG(entry_info.st_mode))
                ++count;
        }
        closedir(dir);
        if (count >= kMaxPhotos) return TransferError::Limit;
    }
    file_ = std::fopen(kStage, "wb");
    owned_ = file_ != nullptr;
    return owned_ ? TransferError::None : TransferError::Storage;
}
TransferError Esp32TransferStorage::list(TransferArea area, const std::string& after, std::vector<TransferFile>& files,
                                         bool& more) {
    files.clear();
    more = false;
    const auto folder = std::string(kRoot) + (area == TransferArea::Photos ? "/photos" : "/files");
    if (!directory(folder)) return TransferError::Storage;
    DIR* dir = opendir(folder.c_str());
    if (!dir) return TransferError::Storage;
    unsigned inspected = 0;
    while (const auto* entry = readdir(dir)) {
        if (++inspected > 2048) break;
        const std::string name(entry->d_name);
        if (name <= after || !transfer_filename_valid(name) ||
            (area == TransferArea::Photos && !photo_filename_valid(name)))
            continue;
        struct stat info{};
        if (stat((folder + "/" + name).c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 0)
            transfer_list_add(files, {name, static_cast<std::uint64_t>(info.st_size)}, more);
    }
    closedir(dir);
    return TransferError::None;
}
TransferError Esp32TransferStorage::erase(TransferArea area, const std::string& name) {
    if (!transfer_filename_valid(name) || (area == TransferArea::Photos && !photo_filename_valid(name)))
        return TransferError::Name;
    const auto path = std::string(kRoot) + (area == TransferArea::Photos ? "/photos/" : "/files/") + name;
    struct stat info{};
    if (stat(path.c_str(), &info) != 0) return TransferError::Missing;
    if (!S_ISREG(info.st_mode)) return TransferError::Name;
    return std::remove(path.c_str()) == 0 ? TransferError::None : TransferError::Storage;
}
TransferError Esp32TransferStorage::open_download(TransferArea area, const std::string& name, std::uint64_t& bytes) {
    close_download();
    if (!transfer_filename_valid(name) || (area == TransferArea::Photos && !photo_filename_valid(name)))
        return TransferError::Name;
    const auto path = std::string(kRoot) + (area == TransferArea::Photos ? "/photos/" : "/files/") + name;
    struct stat info{};
    if (stat(path.c_str(), &info) != 0) return TransferError::Missing;
    if (!S_ISREG(info.st_mode) || info.st_size < 0) return TransferError::Name;  // an ordinary file only
    reading_ = std::fopen(path.c_str(), "rb");
    if (!reading_) return TransferError::Storage;
    bytes = static_cast<std::uint64_t>(info.st_size);
    return TransferError::None;
}
int Esp32TransferStorage::read_download(std::uint8_t* data, std::size_t capacity) {
    if (!reading_) return -1;
    const auto n = std::fread(data, 1, capacity, reading_);
    return n == 0 && std::ferror(reading_) ? -1 : static_cast<int>(n);
}
void Esp32TransferStorage::close_download() {
    if (!reading_) return;
    std::fclose(reading_);
    reading_ = nullptr;
}
bool Esp32TransferStorage::write(const std::uint8_t* data, std::size_t size) {
    return file_ && std::fwrite(data, 1, size, file_) == size;
}
bool Esp32TransferStorage::seal() {
    if (!file_) return false;
    const bool flushed = std::fflush(file_) == 0 && fsync(fileno(file_)) == 0;
    const bool closed = std::fclose(file_) == 0;
    file_ = nullptr;
    return flushed && closed;
}
bool Esp32TransferStorage::read_staged(PhotoBytes& bytes) {
    FILE* file = std::fopen(kStage, "rb");
    if (!file) return false;
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return false;
    }
    const auto length = std::ftell(file);
    const bool valid = length >= 0 && static_cast<unsigned long>(length) <= kMaxPhotoFileBytes &&
                       bytes.resize(static_cast<std::size_t>(length));
    std::rewind(file);
    const bool ok = valid && std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    std::fclose(file);
    if (!ok) bytes.clear();
    return ok;
}
TransferError Esp32TransferStorage::commit() {
    if (!owned_ || file_) return TransferError::Storage;
    struct stat info{};
    if (stat(destination_.c_str(), &info) == 0) return TransferError::Exists;
    // FatFS f_rename rejects an existing target; the HTTP worker is the only writer.
    if (std::rename(kStage, destination_.c_str()) != 0) return TransferError::Storage;
    owned_ = false;
    return TransferError::None;
}
void Esp32TransferStorage::cancel() {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
    if (owned_) std::remove(kStage);
    owned_ = false;
}
// Google's answer to the phone sign-in carries its code in the address: the
// shared parser allows 512 characters, and httpd_req_t::uri must hold them.
static_assert(CONFIG_HTTPD_MAX_URI_LEN >= 512, "the phone sign-in's answer needs a 512-character URI");
void Esp32FileTransfer::initialize() {
    TaskHandle_t task = nullptr;
    available_ = xTaskCreate(manager_entry, "aura_transfer", 6144, this, 2, &task) == pdPASS;
}
bool Esp32FileTransfer::start() {
    if (!available_) return false;
    std::array<std::uint8_t, 36> seed{};
    esp_fill_random(seed.data(), seed.size());
    const bool ok = session_.start(seed, transfer_now());
    volatile std::uint8_t* bytes = seed.data();
    for (std::size_t i = 0; i < seed.size(); ++i)
        bytes[i] = 0;
    return ok;
}
void Esp32FileTransfer::poll() { session_.tick(transfer_now()); }
void Esp32FileTransfer::shutdown() {
    stop();
    for (unsigned i = 0; i < 400 && listening_.load(); ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
}
void Esp32FileTransfer::manager_entry(void* context) { static_cast<Esp32FileTransfer*>(context)->manage(); }
esp_err_t Esp32FileTransfer::request_entry(httpd_req_t* request) {
    return static_cast<Esp32FileTransfer*>(request->user_ctx)->request(request);
}
esp_err_t Esp32FileTransfer::request(httpd_req_t* req) {
    TransferRequest r;
    r.method = req->method == HTTP_GET ? "GET" : "POST";
    r.path = req->uri;
    r.host = header(req, "Host");
    r.origin = header(req, "Origin");
    r.authorization = header(req, "Authorization");
    r.content_type = header(req, "Content-Type");
    r.filename = header(req, "X-Aura-Filename");
    r.area = header(req, "X-Aura-Area");
    r.length = req->content_len;
    // The server's socket is IPv6 (dual-stack, CONFIG_LWIP_IPV6), so an IPv4
    // client arrives as ::ffff:a.b.c.d. Read as a plain IPv4 address it was
    // nonsense, and every request from the owner's network was refused as "auth"
    // (the first flash, 2026-11-14).
    sockaddr_storage peer{};
    socklen_t size = sizeof(peer);
    std::string address;
    if (getpeername(httpd_req_to_sockfd(req), reinterpret_cast<sockaddr*>(&peer), &size) == 0) {
        if (peer.ss_family == AF_INET) {
            char text[INET_ADDRSTRLEN]{};
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(&peer);
            if (inet_ntop(AF_INET, &ipv4->sin_addr, text, sizeof(text))) address = text;
        } else if (peer.ss_family == AF_INET6) {
            const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(&peer);
            std::array<std::uint8_t, 16> bytes{};
            std::memcpy(bytes.data(), &ipv6->sin6_addr, bytes.size());
            if (!transfer_mapped_ipv4(bytes, address)) address.clear();
        }
    }
    r.local_peer = !address.empty() && transfer_private_ipv4(address);
    RequestReader reader(req);
    TransferProtocol protocol(session_, storage_, transfer_now, key_sink_, aoraton_link_);
    bool framing =
        httpd_req_get_hdr_value_len(req, "Transfer-Encoding") == 0 && httpd_req_get_hdr_value_len(req, "Expect") == 0;
    for (const char* name : {"Host", "Origin", "Authorization", "Content-Type", "X-Aura-Filename", "X-Aura-Area"})
        framing = framing && httpd_req_get_hdr_value_len(req, name) <= 512;
    ResponseWriter writer(req);
    const auto result =
        framing ? protocol.handle(r, reader, &writer) : TransferResponse{400, "{\"error\":\"name\"}", false};
    if (result.sent) return writer.finish();
    const auto status = std::to_string(result.status) + " Response";
    httpd_resp_set_status(req, status.c_str());
    httpd_resp_set_type(req, result.html ? "text/html; charset=utf-8" : "application/json; charset=utf-8");
    security_headers(req);
    return httpd_resp_send(req, result.body.data(), static_cast<ssize_t>(result.body.size()));
}
void Esp32FileTransfer::manage() {
    for (;;) {
        session_.tick(transfer_now());
        const auto state = session_.snapshot();
        if (state.state != TransferState::Starting) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        const auto address = station_address();
        if (address.empty()) {
            session_.fail(state.generation, TransferError::Network);
            continue;
        }
        struct stat info{};
        if (stat(kRoot, &info) != 0 || !S_ISDIR(info.st_mode)) {
            session_.fail(state.generation, TransferError::Storage);
            continue;
        }
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 8788;
        config.ctrl_port = 32778;
        config.stack_size = 16384;
        // In PSRAM: internal RAM ran out on the first flash (2026-11-14). The
        // handlers touch NVS only through the key store and the Aoraton link,
        // which run it on an internal stack (aura_board::run_on_internal_stack).
        config.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
        config.max_open_sockets = 2;
        // With both connections taken (a browser keeps idle ones open, and its
        // fetch(credentials: 'omit') opens another), the server stops listening
        // unless it may close the least recently used one: the login's request
        // then waited forever (the first flash, 2026-11-14).
        config.lru_purge_enable = true;
        config.max_uri_handlers = 14;
        config.max_resp_headers = 8;
        config.max_req_hdr_len = 8192;
        config.max_uri_len = 512;
        config.recv_wait_timeout = 1;
        // A download waits out a lost packet, which lwIP resends after 1.5 s and
        // then 3 s (CONFIG_LWIP_TCP_RTO_TIME); a second here would cut the file.
        config.send_wait_timeout = 5;
        httpd_handle_t server = nullptr;
        if (httpd_start(&server, &config) != ESP_OK) {
            session_.fail(state.generation, TransferError::Network);
            continue;
        }
        listening_ = true;
        httpd_uri_t uri{};
        uri.handler = request_entry;
        uri.user_ctx = this;
        uri.uri = "/";
        uri.method = HTTP_GET;
        auto ok = httpd_register_uri_handler(server, &uri);
        uri.uri = "/api/session";
        uri.method = HTTP_POST;
        ok |= httpd_register_uri_handler(server, &uri);
        uri.uri = "/api/upload";
        ok |= httpd_register_uri_handler(server, &uri);
        uri.uri = "/api/files";
        ok |= httpd_register_uri_handler(server, &uri);
        uri.uri = "/api/download";
        ok |= httpd_register_uri_handler(server, &uri);
        uri.uri = "/api/delete";
        ok |= httpd_register_uri_handler(server, &uri);
        uri.uri = "/api/keys";
        ok |= httpd_register_uri_handler(server, &uri);
        if (aoraton_link_) {
            for (const char* path : {"/api/aoraton/config", "/api/aoraton/sign-in", "/api/aoraton/status",
                                     "/api/aoraton/code", "/api/aoraton/invite"}) {
                uri.uri = path;
                ok |= httpd_register_uri_handler(server, &uri);
            }
            // The phone's way (ADR 0010): the QR's page and Google's answer;
            // the handler sees the whole address, query included.
            uri.method = HTTP_GET;
            for (const char* path : {"/aoraton/phone", "/aoraton/callback"}) {
                uri.uri = path;
                ok |= httpd_register_uri_handler(server, &uri);
            }
        }
        if (ok == ESP_OK)
            session_.ready(state.generation, "http://" + address + ":8788", transfer_now());
        else
            session_.fail(state.generation, TransferError::Network);
        while (session_.valid(state.generation, transfer_now())) {
            vTaskDelay(pdMS_TO_TICKS(100));
            session_.tick(transfer_now());
            if (station_address() != address) session_.fail(state.generation, TransferError::Network);
        }
        httpd_stop(server);
        storage_.cancel();
        listening_ = false;
    }
}
}  // namespace aura::firmware

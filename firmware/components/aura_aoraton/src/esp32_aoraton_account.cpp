#include "aura/esp32_aoraton_account.hpp"

#include "aura/aoraton_account.hpp"
#include "aura/aoraton_realtime.hpp"
#include "aura/aoraton_self_test.hpp"

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_pthread.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "esp_transport_ws.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/gcm.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include "mbedtls/sha256.h"
#include "nvs.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <utility>

namespace aura::firmware {
namespace {

constexpr char kTag[] = "aura_aoraton";
constexpr char kNamespace[] = "aura_aoraton";
constexpr std::uint32_t kPbkdf2Slice = 8192;  // rounds between yields

void wipe(std::string& value) {
    if (!value.empty()) mbedtls_platform_zeroize(&value[0], value.size());
    value.clear();
}

void wipe(std::vector<std::uint8_t>& value) {
    if (!value.empty()) mbedtls_platform_zeroize(value.data(), value.size());
    value.clear();
}

std::int64_t steady_ms() { return esp_timer_get_time() / 1000; }

std::int64_t unix_seconds() { return static_cast<std::int64_t>(std::time(nullptr)); }

// This device's offset from UTC at `utc_unix` (its TZ setting, daylight saving included).
int local_utc_offset_minutes(std::int64_t utc_unix) {
    const std::time_t when = static_cast<std::time_t>(utc_unix);
    std::tm local{};
    std::tm utc{};
    if (!localtime_r(&when, &local) || !gmtime_r(&when, &utc)) return 0;
    int days = local.tm_yday - utc.tm_yday;
    if (local.tm_year != utc.tm_year) days = local.tm_year > utc.tm_year ? 1 : -1;
    return days * 1440 + (local.tm_hour - utc.tm_hour) * 60 + (local.tm_min - utc.tm_min);
}

// Starts `body` on a std::thread with its own FreeRTOS settings, then puts
// back the calling task's settings for later threads.
std::thread spawn(const char* name, std::size_t stack, std::size_t priority, int core, std::function<void()> body) {
    esp_pthread_cfg_t previous = esp_pthread_get_default_config();
    const bool had = esp_pthread_get_cfg(&previous) == ESP_OK;
    esp_pthread_cfg_t config = esp_pthread_get_default_config();
    config.thread_name = name;
    config.stack_size = stack;
    config.prio = priority;
    config.pin_to_core = core;
    config.stack_alloc_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;  // NVS writes need an internal stack
    esp_pthread_set_cfg(&config);
    std::thread thread(std::move(body));
    if (had) {
        esp_pthread_set_cfg(&previous);
    } else {
        const esp_pthread_cfg_t defaults = esp_pthread_get_default_config();
        esp_pthread_set_cfg(&defaults);
    }
    return thread;
}

// "https://host[:port]" into its parts; anything else is refused.
bool split_origin(const std::string& base, std::string& host, int& port) {
    constexpr char kScheme[] = "https://";
    if (base.rfind(kScheme, 0) != 0) return false;
    std::string rest = base.substr(sizeof(kScheme) - 1);
    if (!rest.empty() && rest.back() == '/') rest.pop_back();
    if (rest.empty() || rest.find('/') != std::string::npos) return false;
    const auto colon = rest.find(':');
    port = 443;
    if (colon != std::string::npos) {
        port = std::atoi(rest.c_str() + colon + 1);
        rest.resize(colon);
        if (port <= 0 || port > 65535) return false;
    }
    host = rest;
    return !host.empty();
}

esp_http_client_method_t method_of(const std::string& name, bool& known) {
    known = true;
    if (name == "GET") return HTTP_METHOD_GET;
    if (name == "POST") return HTTP_METHOD_POST;
    if (name == "PATCH") return HTTP_METHOD_PATCH;
    if (name == "PUT") return HTTP_METHOD_PUT;
    if (name == "DELETE") return HTTP_METHOD_DELETE;
    if (name == "HEAD") return HTTP_METHOD_HEAD;
    known = false;
    return HTTP_METHOD_GET;
}

}  // namespace

// ---- crypto -----------------------------------------------------------------

bool Esp32AoratonAccountCrypto::random_bytes(std::uint8_t* destination, std::size_t size) {
    if (destination == nullptr && size != 0) return false;
    esp_fill_random(destination, size);
    return true;
}

bool Esp32AoratonAccountCrypto::sha256(const std::uint8_t* data, std::size_t size,
                                       std::array<std::uint8_t, 32>& digest) {
    return mbedtls_sha256(data, size, digest.data(), 0) == 0;
}

bool Esp32AoratonAccountCrypto::pbkdf2_sha256(const std::string& passphrase, const std::vector<std::uint8_t>& salt,
                                              std::uint32_t iterations, std::array<std::uint8_t, 32>& key) {
    if (iterations == 0 || salt.size() > 1024 || passphrase.size() > 4096) return false;
    const mbedtls_md_info_t* sha = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (sha == nullptr) return false;
    mbedtls_md_context_t context;
    mbedtls_md_init(&context);
    std::array<std::uint8_t, 32> block{};
    std::array<std::uint8_t, 32> sum{};
    const std::uint8_t index[4] = {0, 0, 0, 1};
    const auto* password = reinterpret_cast<const unsigned char*>(passphrase.data());
    bool ok = mbedtls_md_setup(&context, sha, 1) == 0 &&
              mbedtls_md_hmac_starts(&context, password, passphrase.size()) == 0 &&
              mbedtls_md_hmac_update(&context, salt.data(), salt.size()) == 0 &&
              mbedtls_md_hmac_update(&context, index, sizeof(index)) == 0 &&
              mbedtls_md_hmac_finish(&context, block.data()) == 0;
    sum = block;
    for (std::uint32_t round = 1; ok && round < iterations; ++round) {
        ok = mbedtls_md_hmac_reset(&context) == 0 && mbedtls_md_hmac_update(&context, block.data(), block.size()) == 0 &&
             mbedtls_md_hmac_finish(&context, block.data()) == 0;
        for (std::size_t byte = 0; byte < sum.size(); ++byte) sum[byte] ^= block[byte];
        if (round % kPbkdf2Slice == 0) vTaskDelay(1);  // let the idle task and the UI run
    }
    mbedtls_md_free(&context);
    if (ok) key = sum;
    mbedtls_platform_zeroize(block.data(), block.size());
    mbedtls_platform_zeroize(sum.data(), sum.size());
    return ok;
}

bool Esp32AoratonAccountCrypto::aes_gcm_open(const std::array<std::uint8_t, 32>& key, const std::string& wire,
                                             std::string& plaintext) {
    plaintext.clear();
    AoratonV2Envelope envelope;
    AoratonCryptoError error = AoratonCryptoError::None;
    if (!aoraton_parse_v2_envelope(wire, envelope, error) || envelope.ciphertext_and_tag.size() < kAoratonGcmTagBytes)
        return false;
    const std::size_t length = envelope.ciphertext_and_tag.size() - kAoratonGcmTagBytes;
    std::vector<std::uint8_t> bytes(length);
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    const bool ok = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key.data(), 256) == 0 &&
                    mbedtls_gcm_auth_decrypt(&gcm, length, envelope.iv.data(), envelope.iv.size(), nullptr, 0,
                                             envelope.ciphertext_and_tag.data() + length, kAoratonGcmTagBytes,
                                             envelope.ciphertext_and_tag.data(), bytes.data()) == 0;
    mbedtls_gcm_free(&gcm);
    if (ok) plaintext.assign(bytes.begin(), bytes.end());
    wipe(bytes);
    return ok;
}

// ---- HTTP -------------------------------------------------------------------

AoratonHttpResponse Esp32AoratonHttp::send(const AoratonHttpRequest& request) {
    AoratonHttpResponse response;
    bool known = false;
    const auto method = method_of(request.method, known);
    if (!known || request.url.rfind("https://", 0) != 0) return response;
    esp_http_client_config_t config{};
    config.url = request.url.c_str();
    config.method = method;
    config.timeout_ms = request.timeout_ms != 0 ? static_cast<int>(request.timeout_ms) : 15000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.buffer_size = 4096;
    config.buffer_size_tx = 6144;  // a session JWT and the anon key travel in headers
    config.keep_alive_enable = false;
    config.disable_auto_redirect = true;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) return response;
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    for (const auto& header : request.headers) {
        esp_http_client_set_header(client, header.first.c_str(), header.second.c_str());
    }
    const auto finish = [&](bool failed) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (failed) {
            response.status = 0;
            wipe(response.body);
        }
        return response;
    };
    if (esp_http_client_open(client, static_cast<int>(request.body.size())) != ESP_OK) return finish(true);
    for (std::size_t sent = 0; sent < request.body.size();) {
        const int written =
            esp_http_client_write(client, request.body.data() + sent, static_cast<int>(request.body.size() - sent));
        if (written <= 0) return finish(true);
        sent += static_cast<std::size_t>(written);
    }
    const std::int64_t length = esp_http_client_fetch_headers(client);
    if (length < 0) return finish(true);
    response.status = esp_http_client_get_status_code(client);
    const std::size_t limit = request.max_response_bytes != 0
                                  ? std::min(request.max_response_bytes, kMaxDownloadBytes)
                                  : kMaxResponseBytes;
    // A failed allocation would abort (no exceptions): check the heap first.
    if (length > 0) {
        const auto expected = static_cast<std::size_t>(length);
        if (expected > limit || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < expected + 64 * 1024)
            return finish(true);
        response.body.reserve(expected);
    }
    char buffer[2048];
    for (;;) {
        const int read = esp_http_client_read(client, buffer, sizeof(buffer));
        if (read == 0) break;
        if (read < 0 || response.body.size() + static_cast<std::size_t>(read) > limit ||
            (response.body.size() + static_cast<std::size_t>(read) > response.body.capacity() &&
             heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 2 * response.body.size() + 64 * 1024)) {
            mbedtls_platform_zeroize(buffer, sizeof(buffer));
            return finish(true);
        }
        response.body.append(buffer, static_cast<std::size_t>(read));
    }
    mbedtls_platform_zeroize(buffer, sizeof(buffer));
    return finish(!esp_http_client_is_complete_data_received(client));
}

// ---- microSD cache -------------------------------------------------------------

namespace {

constexpr const char* kCardRoot = "/sdcard/aura";  // made by the companion when it mounts the card
constexpr const char* kCacheParent = "/sdcard/aura/.cache";
constexpr const char* kCacheFolder = "/sdcard/aura/.cache/aoraton";

std::string cache_path(const std::string& name, const char* extension) {
    return std::string(kCacheFolder) + "/" + name + extension;
}

}  // namespace

bool Esp32AoratonCacheFiles::list(std::vector<AoratonCacheEntry>& entries) {
    entries.clear();
    struct stat card {};
    if (stat(kCardRoot, &card) != 0 || !S_ISDIR(card.st_mode)) return false;  // no card: the cache is off
    DIR* folder = opendir(kCacheFolder);
    if (folder == nullptr) return true;  // nothing kept yet
    while (const dirent* item = readdir(folder)) {
        const std::string file = item->d_name;
        if (file.size() != 68) continue;
        const std::string name = file.substr(0, 64);
        if (!aoraton_cache_name_valid(name)) continue;
        if (file.compare(64, 4, ".tmp") == 0) {  // a write cut short by a restart
            unlink(cache_path(name, ".tmp").c_str());
            continue;
        }
        struct stat info {};
        if (file.compare(64, 4, ".enc") != 0 || stat(cache_path(name, ".enc").c_str(), &info) != 0 ||
            !S_ISREG(info.st_mode))
            continue;
        entries.push_back({name, static_cast<std::uint64_t>(info.st_size), static_cast<std::int64_t>(info.st_mtime)});
    }
    closedir(folder);
    return true;
}

bool Esp32AoratonCacheFiles::read(const std::string& name, std::size_t max_bytes, std::string& data) {
    data.clear();
    if (!aoraton_cache_name_valid(name)) return false;
    const std::string path = cache_path(name, ".enc");
    struct stat info {};
    if (stat(path.c_str(), &info) != 0 || info.st_size <= 0 || static_cast<std::uint64_t>(info.st_size) > max_bytes)
        return false;
    const auto size = static_cast<std::size_t>(info.st_size);
    // A failed allocation would abort (no exceptions): check the heap first.
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < size + 64 * 1024) return false;
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return false;
    data.resize(size);
    const bool ok = std::fread(data.data(), 1, size, file) == size && std::fgetc(file) == EOF;
    std::fclose(file);
    if (!ok) data.clear();
    return ok;
}

bool Esp32AoratonCacheFiles::write(const std::string& name, const std::string& data) {
    if (!aoraton_cache_name_valid(name) || data.empty()) return false;
    mkdir(kCacheParent, 0775);  // already there: EEXIST
    mkdir(kCacheFolder, 0775);
    const std::string temporary = cache_path(name, ".tmp");
    const std::string path = cache_path(name, ".enc");
    FILE* file = std::fopen(temporary.c_str(), "wb");
    if (file == nullptr) return false;
    const bool written = std::fwrite(data.data(), 1, data.size(), file) == data.size();
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed) {
        unlink(temporary.c_str());
        return false;
    }
    unlink(path.c_str());  // FAT's rename does not replace a file
    if (rename(temporary.c_str(), path.c_str()) != 0) {
        unlink(temporary.c_str());
        return false;
    }
    return true;
}

void Esp32AoratonCacheFiles::remove(const std::string& name) {
    if (aoraton_cache_name_valid(name)) unlink(cache_path(name, ".enc").c_str());
}

// ---- NVS --------------------------------------------------------------------

bool Esp32AoratonSecretStore::load(AoratonStoredSecrets& secrets) {
    secrets.clear();
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
    std::size_t size = 0;
    bool ok = nvs_get_blob(handle, "secrets", nullptr, &size) == ESP_OK && size > 0 && size <= 16384;
    std::vector<std::uint8_t> record(ok ? size : 0);
    ok = ok && nvs_get_blob(handle, "secrets", record.data(), &size) == ESP_OK && size == record.size() &&
         aoraton_decode_secrets(record, secrets);
    nvs_close(handle);
    wipe(record);
    return ok;
}

bool Esp32AoratonSecretStore::save(const AoratonStoredSecrets& secrets) {
    std::vector<std::uint8_t> record;
    if (!aoraton_encode_secrets(secrets, record)) return false;
    nvs_handle_t handle = 0;
    bool ok = nvs_open(kNamespace, NVS_READWRITE, &handle) == ESP_OK;
    if (ok) {
        ok = nvs_set_blob(handle, "secrets", record.data(), record.size()) == ESP_OK && nvs_commit(handle) == ESP_OK;
        nvs_close(handle);
    }
    wipe(record);
    return ok;
}

bool Esp32AoratonSecretStore::erase() {
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t erased = nvs_erase_key(handle, "secrets");
    const bool ok = (erased == ESP_OK || erased == ESP_ERR_NVS_NOT_FOUND) && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

bool esp32_aoraton_load_config(AoratonLinkConfig& config) {
    config = {};
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
    const auto read = [&](const char* key, std::string& value) {
        std::size_t size = 0;
        if (nvs_get_str(handle, key, nullptr, &size) != ESP_OK || size == 0 || size > 4000) return false;
        std::string text(size, '\0');
        if (nvs_get_str(handle, key, &text[0], &size) != ESP_OK) return false;
        text.resize(size - 1);
        value = std::move(text);
        return true;
    };
    std::uint8_t inco = 0;
    const bool ok = read("url", config.base_url) && read("anon", config.anon_key) &&
                    nvs_get_u8(handle, "inco", &inco) == ESP_OK;
    nvs_close(handle);
    config.inco_enabled = inco == 1;
    if (!ok || !aoraton_link_config_valid(config)) {
        wipe(config.anon_key);
        config = {};
        return false;
    }
    return true;
}

bool esp32_aoraton_save_config(const AoratonLinkConfig& config) {
    if (!aoraton_link_config_valid(config)) return false;
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    const bool ok = nvs_set_str(handle, "url", config.base_url.c_str()) == ESP_OK &&
                    nvs_set_str(handle, "anon", config.anon_key.c_str()) == ESP_OK &&
                    nvs_set_u8(handle, "inco", config.inco_enabled ? 1 : 0) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

// ---- realtime ---------------------------------------------------------------

namespace {
struct RealtimeConfig {
    std::string base, key, token;
    std::vector<std::string> rooms;
    ~RealtimeConfig() { clear(); }
    void clear() {
        wipe(base);
        wipe(key);
        wipe(token);
        rooms.clear();
    }
    bool operator==(const RealtimeConfig& other) const {
        return base == other.base && key == other.key && token == other.token && rooms == other.rooms;
    }
};

bool send_text(esp_transport_handle_t ws, std::string text) {
    const int sent = esp_transport_ws_send_raw(
        ws, static_cast<ws_transport_opcodes_t>(WS_TRANSPORT_OPCODES_TEXT | WS_TRANSPORT_OPCODES_FIN), text.data(),
        static_cast<int>(text.size()), 5000);
    const bool ok = sent == static_cast<int>(text.size());
    wipe(text);
    return ok;
}
}  // namespace

struct Esp32AoratonRealtime::Impl {
    using Clock = std::chrono::steady_clock;
    std::mutex mutex;
    std::condition_variable changed;
    RealtimeConfig desired;
    std::uint64_t generation{0};
    bool stopping{false};
    bool enabled{false};
    bool reported_active{false};
    AoratonRealtimeUpdate update;
    std::thread owner;

    Impl() : owner(spawn("aoraton_rt", 8192, 3, 0, [this] { run(); })) {}
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

    void connected(const RealtimeConfig& config, std::uint64_t version) {
        std::string host;
        int port = 443;
        if (!split_origin(config.base, host, port)) return;
        esp_transport_handle_t ssl = esp_transport_ssl_init();
        if (ssl == nullptr) return;
        esp_transport_ssl_crt_bundle_attach(ssl, esp_crt_bundle_attach);
        esp_transport_handle_t ws = esp_transport_ws_init(ssl);
        if (ws == nullptr) {
            esp_transport_destroy(ssl);
            return;
        }
        std::string path = "/realtime/v1/websocket?apikey=" + aoraton_url_encode(config.key) + "&vsn=1.0.0";
        esp_transport_ws_set_path(ws, path.c_str());
        wipe(path);
        const auto close = [&] {
            esp_transport_close(ws);
            esp_transport_destroy(ws);
            esp_transport_destroy(ssl);
        };
        if (esp_transport_connect(ws, host.c_str(), port, 10000) < 0 || !current(version)) {
            close();
            return;
        }
        AoratonRealtimeProtocol protocol(config.rooms);
        auto joins = protocol.joins(config.token);
        bool sent = joins.size() == config.rooms.size();
        for (auto& frame : joins) {
            if (sent) sent = send_text(ws, frame);
            wipe(frame);
        }
        if (!sent) {
            close();
            return;
        }
        const auto opened = Clock::now();
        auto last_heartbeat = opened;
        std::uint64_t heartbeat = 0;
        std::uint64_t acknowledged = 0;
        bool ready = false;
        std::string frame;
        int remaining = 0;
        bool fin = false;
        std::array<char, 2048> buffer{};
        while (current(version)) {
            const bool new_frame = remaining <= 0;
            const int read = esp_transport_read(ws, buffer.data(), static_cast<int>(buffer.size()), 200);
            if (read < 0) break;
            if (read > 0) {
                if (new_frame) {
                    const int opcode = static_cast<int>(esp_transport_ws_get_read_opcode(ws)) & 0x0F;
                    if (opcode != WS_TRANSPORT_OPCODES_TEXT && opcode != WS_TRANSPORT_OPCODES_CONT) break;
                    remaining = esp_transport_ws_get_read_payload_len(ws);
                    fin = esp_transport_ws_get_fin_flag(ws);
                }
                remaining -= read;
                if (frame.size() + static_cast<std::size_t>(read) > AoratonRealtimeProtocol::kMaxFrameBytes) break;
                frame.append(buffer.data(), static_cast<std::size_t>(read));
                mbedtls_platform_zeroize(buffer.data(), buffer.size());
                if (remaining <= 0 && fin) {
                    if (!protocol.receive(frame)) break;
                    wipe(frame);
                    const auto& reply = protocol.heartbeat_reply();
                    if (reply.size() > 1 && reply[0] == 'h') {
                        char* end = nullptr;
                        const auto number = std::strtoull(reply.c_str() + 1, &end, 10);
                        if (end != nullptr && *end == '\0') acknowledged = number;
                    }
                    ready = protocol.active();
                    publish(version, ready, protocol.take_refresh());
                }
            }
            const auto now = Clock::now();
            if ((!ready && now - opened > std::chrono::seconds(15)) ||
                (heartbeat != acknowledged && now - last_heartbeat > std::chrono::seconds(10)))
                break;
            if (now - last_heartbeat >= std::chrono::seconds(20)) {
                ++heartbeat;
                if (!send_text(ws, protocol.heartbeat("h" + std::to_string(heartbeat)))) break;
                last_heartbeat = now;
            }
        }
        wipe(frame);
        mbedtls_platform_zeroize(buffer.data(), buffer.size());
        close();
    }

    void run() {
        unsigned failures = 0;
        std::uint64_t previous = 0;
        for (;;) {
            RealtimeConfig config;
            std::uint64_t version = 0;
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
            const auto delay = std::chrono::seconds(std::min(60U, 2U << std::min(failures++, 5U)));
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait_for(lock, delay, [&] { return stopping || generation != version; });
        }
    }
};

Esp32AoratonRealtime::Esp32AoratonRealtime() : impl_(std::make_unique<Impl>()) {}
Esp32AoratonRealtime::~Esp32AoratonRealtime() = default;

void Esp32AoratonRealtime::configure(const std::string& base_url, const std::string& anon_key,
                                     const std::string& access_token, const std::vector<std::string>& rooms) {
    if (rooms.empty() || rooms.size() > AoratonRealtimeProtocol::kMaxRooms || access_token.empty() ||
        access_token.size() > 16384 ||
        std::any_of(rooms.begin(), rooms.end(),
                    [](const std::string& room) { return !AoratonRealtimeProtocol::valid_room(room); })) {
        stop();
        return;
    }
    RealtimeConfig next;
    next.base = base_url;
    next.key = anon_key;
    next.token = access_token;
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

AoratonRealtimeUpdate Esp32AoratonRealtime::poll() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto update = impl_->update;
    impl_->update.refresh_required = false;
    if (update.active != impl_->reported_active) {
        impl_->reported_active = update.active;
        // A status code only: no endpoint, rooms, token or frame data.
        ESP_LOGI(kTag, "realtime %s", update.active ? "SUBSCRIBED" : "POLLING_FALLBACK");
    }
    return update;
}

void Esp32AoratonRealtime::stop() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->enabled = false;
        ++impl_->generation;
        impl_->desired.clear();
        impl_->update = {};
    }
    impl_->changed.notify_all();
}

// ---- the account ------------------------------------------------------------

AoratonAccountConfig Esp32AoratonAccount::account_config(const AoratonLinkConfig& link_config,
                                                         Esp32AoratonAccount* self) {
    AoratonAccountConfig config;
    config.base_url = link_config.base_url;
    config.anon_key = link_config.anon_key;
    config.redirect_url = kAoratonPcRedirectUrl;
    config.inco.enabled = link_config.inco_enabled;
    config.inco.utc_offset_minutes = local_utc_offset_minutes;
    config.inco.location_city = [self] {
        std::lock_guard<std::mutex> lock(self->city_mutex_);
        return self->city_;
    };
    // Ids and codes only: the worker never passes text here.
    config.inco.log = [](const std::string& line) { ESP_LOGI(kTag, "%s", line.c_str()); };
    return config;
}

Esp32AoratonAccount::Esp32AoratonAccount(AoratonMailbox& mailbox, const AoratonLinkConfig& link_config,
                                         AoratonDeviceLink& link)
    : worker_(mailbox, account_config(link_config, this), http_, account_crypto_, message_crypto_, store_, link,
              &realtime_, &voice_decoder_, &attachment_cache_) {}

Esp32AoratonAccount::~Esp32AoratonAccount() { stop(); }

bool Esp32AoratonAccount::start() {
    if (thread_.joinable()) return true;
    const auto result = aoraton_crypto_self_test(account_crypto_, message_crypto_);
    ESP_LOGI(kTag, "crypto self-test %s", aoraton_self_test_name(result));
    if (result != AoratonSelfTestResult::Passed) return false;
    stopping_ = false;
    thread_ = spawn("aoraton", 16384, 2, 1, [this] { run(); });
    return true;
}

void Esp32AoratonAccount::stop() {
    stopping_ = true;
    realtime_.stop();
    if (thread_.joinable()) thread_.join();
    realtime_.stop();
}

void Esp32AoratonAccount::set_location_city(std::string city) {
    std::lock_guard<std::mutex> lock(city_mutex_);
    city_ = std::move(city);
}

void Esp32AoratonAccount::run() {
    worker_.start(steady_ms(), unix_seconds());
    while (!stopping_) {
        worker_.step(steady_ms(), unix_seconds());
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

}  // namespace aura::firmware

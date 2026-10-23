#include "esp32_ota.hpp"

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "nvs.h"

#include <cctype>
#include <cstdio>

namespace aura::firmware {
namespace {

constexpr const char* kTag = "aura-ota";
constexpr std::size_t kManifestLimit = 2048;

esp_http_client_handle_t open_https(const std::string& url) {
    esp_http_client_config_t config{};
    config.url = url.c_str();
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = 15000;
    config.keep_alive_enable = true;
    auto client = esp_http_client_init(&config);
    if (!client) return nullptr;
    if (esp_http_client_open(client, 0) != ESP_OK || esp_http_client_fetch_headers(client) < 0 ||
        esp_http_client_get_status_code(client) != 200) {
        esp_http_client_cleanup(client);
        return nullptr;
    }
    return client;
}

}  // namespace

bool Esp32Ota::initialize() {
    nvs_handle_t handle = 0;
    if (nvs_open("aura_ota", NVS_READONLY, &handle) == ESP_OK) {
        char url[257]{};
        std::size_t size = sizeof(url);
        if (nvs_get_str(handle, "manifest_url", url, &size) == ESP_OK && std::string(url).rfind("https://", 0) == 0) {
            manifest_url_ = url;
        }
        nvs_close(handle);
    }
    ESP_LOGI(kTag, "running %s; update source %s", running_version().c_str(), configured() ? "configured" : "none");
    return true;
}

std::string Esp32Ota::running_version() const {
    const auto* description = esp_app_get_description();
    return description && version_valid(description->version) ? description->version : "0.0.0";
}

bool Esp32Ota::request_check() {
    if (!configured() || busy_) return false;
    busy_ = true;
    if (xTaskCreate(&Esp32Ota::check_task, "aura_ota_check", 6144, this, 3, nullptr) != pdPASS) {
        busy_ = false;
        return false;
    }
    return true;
}

bool Esp32Ota::request_install(const UpdateManifest& manifest) {
    if (busy_) return false;
    busy_ = true;
    pending_ = manifest;
    if (xTaskCreate(&Esp32Ota::install_task, "aura_ota_install", 8192, this, 3, nullptr) != pdPASS) {
        busy_ = false;
        return false;
    }
    return true;
}

void Esp32Ota::check_task(void* argument) {
    auto* self = static_cast<Esp32Ota*>(argument);
    OtaEvent event;
    auto client = open_https(self->manifest_url_);
    if (!client) {
        event.error_code = "MANIFEST_UNREACHABLE";
    } else {
        std::string body(kManifestLimit + 1, '\0');
        const int read = esp_http_client_read_response(client, body.data(), static_cast<int>(body.size()));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (read <= 0 || static_cast<std::size_t>(read) > kManifestLimit) {
            event.error_code = "MANIFEST_INVALID";
        } else {
            body.resize(static_cast<std::size_t>(read));
            std::string error;
            event.manifest = parse_update_manifest(body, &error);
            if (event.manifest) event.kind = OtaEvent::Kind::CheckDone;
            else event.error_code = error;
        }
    }
    self->busy_ = false;
    self->push(std::move(event));
    vTaskDelete(nullptr);
}

void Esp32Ota::install_task(void* argument) {
    auto* self = static_cast<Esp32Ota*>(argument);
    const char* error = self->install(self->pending_);
    OtaEvent event;
    event.kind = error ? OtaEvent::Kind::Failed : OtaEvent::Kind::Ready;
    if (error) event.error_code = error;
    self->busy_ = false;
    self->push(std::move(event));
    vTaskDelete(nullptr);
}

// Streams the image into the inactive slot; returns an error code or nullptr.
const char* Esp32Ota::install(const UpdateManifest& manifest) {
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (!target || target->size < manifest.size_bytes) return "OTA_NO_SLOT";
    auto client = open_https(manifest.url);
    if (!client) return "OTA_DOWNLOAD_FAILED";
    esp_ota_handle_t ota = 0;
    if (esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
        esp_http_client_cleanup(client);
        return "OTA_BEGIN_FAILED";
    }
    mbedtls_sha256_context sha{};
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    static std::uint8_t buffer[4096];
    std::uint32_t total = 0;
    int last_percent = -1;
    bool ok = true;
    for (;;) {
        const int read = esp_http_client_read(client, reinterpret_cast<char*>(buffer), sizeof(buffer));
        if (read < 0) { ok = false; break; }
        if (read == 0) {
            if (esp_http_client_is_complete_data_received(client)) break;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        total += static_cast<std::uint32_t>(read);
        if (total > manifest.size_bytes || esp_ota_write(ota, buffer, static_cast<std::size_t>(read)) != ESP_OK) {
            ok = false;
            break;
        }
        mbedtls_sha256_update(&sha, buffer, static_cast<std::size_t>(read));
        const int percent = static_cast<int>(static_cast<std::uint64_t>(total) * 100U / manifest.size_bytes);
        if (percent != last_percent) {
            last_percent = percent;
            OtaEvent progress;
            progress.kind = OtaEvent::Kind::Progress;
            progress.progress = static_cast<float>(percent) / 100.0F;
            push(std::move(progress));
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    std::uint8_t digest[32]{};
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    OtaEvent verifying;
    verifying.kind = OtaEvent::Kind::Verifying;
    push(std::move(verifying));
    char hex[65]{};
    for (int index = 0; index < 32; ++index) std::snprintf(hex + index * 2, 3, "%02x", digest[index]);
    bool hash_ok = ok && total == manifest.size_bytes;
    for (std::size_t index = 0; hash_ok && index < 64; ++index) {
        hash_ok = std::tolower(static_cast<unsigned char>(manifest.sha256[index])) == hex[index];
    }
    if (!hash_ok) {
        esp_ota_abort(ota);
        return ok ? "OTA_HASH_MISMATCH" : "OTA_DOWNLOAD_FAILED";
    }
    // esp_ota_end validates the image header and segments before it is bootable.
    if (esp_ota_end(ota) != ESP_OK || esp_ota_set_boot_partition(target) != ESP_OK) return "OTA_IMAGE_INVALID";
    return nullptr;
}

void Esp32Ota::restart() { esp_restart(); }

void Esp32Ota::confirm_running_image() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state{};
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(kTag, "new image confirmed");
    }
}

void Esp32Ota::push(OtaEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(std::move(event));
}

bool Esp32Ota::poll(OtaEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.empty()) return false;
    event = std::move(events_.front());
    events_.erase(events_.begin());
    return true;
}

}  // namespace aura::firmware

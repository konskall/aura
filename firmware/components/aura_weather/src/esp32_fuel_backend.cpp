#include "esp32_fuel_backend.hpp"

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/task.h"

#include <cstdint>
#include <string>
#include <utility>

namespace aura::firmware {
namespace {

constexpr const char* kTag = "aura_fuel";
// The page is 112 KB and the workbook 17 KB (2026-11-18).
constexpr std::size_t kPageLimit = 256U * 1024U;
constexpr std::size_t kWorkbookLimit = 128U * 1024U;
// Unpacking the workbook takes about 7 KB of stack, TLS the rest; in PSRAM.
constexpr std::uint32_t kStackBytes = 16U * 1024U;

struct Download {
    std::size_t limit{0};
    std::string body;
    bool overflow{false};
};

esp_err_t download_event(esp_http_client_event_t* event) {
    auto* download = static_cast<Download*>(event->user_data);
    if (!download || event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) return ESP_OK;
    if (esp_http_client_get_status_code(event->client) != 200) return ESP_OK;  // a redirect's own body
    if (download->body.size() + static_cast<std::size_t>(event->data_len) > download->limit) {
        download->overflow = true;
        return ESP_FAIL;
    }
    download->body.append(static_cast<const char*>(event->data), static_cast<std::size_t>(event->data_len));
    return ESP_OK;
}

bool https_get(const char* url, Download& download) {
    esp_http_client_config_t config{};
    config.url = url;
    config.event_handler = download_event;
    config.user_data = &download;
    config.timeout_ms = 30000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.buffer_size = 2048;
    config.buffer_size_tx = 1536;  // the workbook's address is long
    config.user_agent = "Mozilla/5.0";
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    const esp_err_t performed = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (performed != ESP_OK || status != 200 || download.overflow) {
        ESP_LOGW(kTag, "download failed: %s, status %d, %u bytes", esp_err_to_name(performed), status,
                 static_cast<unsigned>(download.body.size()));
        return false;
    }
    return true;
}

std::optional<FuelPriceData> fetch_prices() {
    std::string link;
    {
        Download page;
        page.limit = kPageLimit;
        if (!https_get(kFuelBulletinPage, page)) return std::nullopt;
        link = fuel_bulletin_link(page.body);
    }
    if (link.empty()) {
        ESP_LOGW(kTag, "the bulletin's page names no workbook");
        return std::nullopt;
    }
    Download workbook;
    workbook.limit = kWorkbookLimit;
    if (!https_get(link.c_str(), workbook)) return std::nullopt;
    auto prices = parse_fuel_bulletin(reinterpret_cast<const std::uint8_t*>(workbook.body.data()), workbook.body.size());
    if (!prices) ESP_LOGW(kTag, "the workbook could not be read");
    return prices;
}

}  // namespace

bool Esp32FuelBackend::initialize() {
    result_mutex_ = xSemaphoreCreateMutex();
    return result_mutex_ != nullptr;
}

bool Esp32FuelBackend::request() {
    bool expected = false;
    if (!result_mutex_ || !busy_.compare_exchange_strong(expected, true)) return false;
    if (xTaskCreateWithCaps(worker_entry, "aura-fuel", kStackBytes, this, 3, nullptr,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        busy_ = false;
        return false;
    }
    return true;
}

void Esp32FuelBackend::worker_entry(void* context) {
    auto* self = static_cast<Esp32FuelBackend*>(context);
    auto prices = fetch_prices();
    if (xSemaphoreTake(self->result_mutex_, portMAX_DELAY) == pdTRUE) {
        self->result_ = std::move(prices);
        self->finished_ = true;
        xSemaphoreGive(self->result_mutex_);
    }
    self->busy_ = false;
    vTaskDeleteWithCaps(nullptr);
}

bool Esp32FuelBackend::poll(std::optional<FuelPriceData>& prices) {
    if (!result_mutex_ || xSemaphoreTake(result_mutex_, 0) != pdTRUE) return false;
    const bool finished = finished_;
    if (finished) {
        prices = std::move(result_);
        result_.reset();
        finished_ = false;
    }
    xSemaphoreGive(result_mutex_);
    return finished;
}

}  // namespace aura::firmware

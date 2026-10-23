#include "esp32_weather_backend.hpp"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "freertos/task.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <utility>

namespace aura::firmware {

namespace {

constexpr const char* kTag = "aura_weather";
constexpr const char* kNamespace = "aura_weather";
constexpr std::size_t kMaximumResponseBytes = 96 * 1024;
constexpr std::size_t kMaximumUrlBytes = 512;

struct ResponseBuffer {
    std::string body;
    bool overflow{false};
};

WeatherFetchResult failure(const char* code, bool offline = false) {
    WeatherFetchResult result;
    result.error_code = code;
    result.offline = offline;
    return result;
}

bool valid_server_url(const std::string& value) {
    return value.rfind("https://", 0) == 0 && value.size() <= kMaximumUrlBytes &&
           value.find('@') == std::string::npos && value.find('?') == std::string::npos &&
           value.find('#') == std::string::npos && !value.empty() && value.back() != '/';
}

bool read_nvs_string(nvs_handle_t handle, const char* key, std::string& output,
                     std::size_t maximum) {
    std::size_t size = 0;
    if (nvs_get_str(handle, key, nullptr, &size) != ESP_OK || size < 2 || size > maximum + 1) {
        return false;
    }
    output.resize(size);
    if (nvs_get_str(handle, key, output.data(), &size) != ESP_OK) return false;
    if (!output.empty() && output.back() == '\0') output.pop_back();
    return true;
}

bool write_nvs_string(nvs_handle_t handle, const char* key, const std::string& value) {
    return nvs_set_str(handle, key, value.c_str()) == ESP_OK;
}

esp_err_t http_event(esp_http_client_event_t* event) {
    auto* buffer = static_cast<ResponseBuffer*>(event->user_data);
    if (!buffer || event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) return ESP_OK;
    if (buffer->body.size() + static_cast<std::size_t>(event->data_len) > kMaximumResponseBytes) {
        buffer->overflow = true;
        return ESP_FAIL;
    }
    buffer->body.append(static_cast<const char*>(event->data), static_cast<std::size_t>(event->data_len));
    return ESP_OK;
}

bool get_json(const std::string& url, ResponseBuffer& response, int& status) {
    response.body.clear();
    response.overflow = false;
    response.body.reserve(12 * 1024);
    esp_http_client_config_t config{};
    config.url = url.c_str();
    config.event_handler = http_event;
    config.user_data = &response;
    config.timeout_ms = 28000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.buffer_size = 2048;
    config.buffer_size_tx = 1024;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    const esp_err_t performed = esp_http_client_perform(client);
    status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return performed == ESP_OK && !response.overflow;
}

const cJSON* object(const cJSON* parent, const char* key) {
    const auto* value = parent ? cJSON_GetObjectItemCaseSensitive(parent, key) : nullptr;
    return cJSON_IsObject(value) ? value : nullptr;
}

const cJSON* array(const cJSON* parent, const char* key) {
    const auto* value = parent ? cJSON_GetObjectItemCaseSensitive(parent, key) : nullptr;
    return cJSON_IsArray(value) ? value : nullptr;
}

bool number(const cJSON* parent, const char* key, double& output) {
    const auto* value = parent ? cJSON_GetObjectItemCaseSensitive(parent, key) : nullptr;
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble)) return false;
    output = value->valuedouble;
    return true;
}

std::string text(const cJSON* parent, const char* key) {
    const auto* value = parent ? cJSON_GetObjectItemCaseSensitive(parent, key) : nullptr;
    return cJSON_IsString(value) && value->valuestring ? value->valuestring : std::string{};
}

bool boolean(const cJSON* parent, const char* key, bool& output) {
    const auto* value = parent ? cJSON_GetObjectItemCaseSensitive(parent, key) : nullptr;
    if (!cJSON_IsBool(value)) return false;
    output = cJSON_IsTrue(value);
    return true;
}

bool status_available(const cJSON* value) {
    const auto status = text(value, "status");
    return status == "available" || status == "stale";
}

WeatherFetchResult parse_payload(const std::string& json, const WeatherLocation& location) {
    cJSON* root = cJSON_ParseWithLength(json.data(), json.size());
    if (!root) return failure("INVALID_TLESDETECTOR_RESPONSE");
    if (!status_available(root)) {
        cJSON_Delete(root);
        return failure("TLESDETECTOR_WEATHER_UNAVAILABLE");
    }
    const auto* current = object(root, "current");
    const auto* hours = array(root, "hours");
    const auto* days = array(root, "days");
    if (!current || !hours || !days) {
        cJSON_Delete(root);
        return failure("INVALID_TLESDETECTOR_RESPONSE");
    }

    WeatherData data;
    data.location = location;
    data.provider = WeatherProvider::TlesDetector;
    data.origin = WeatherDataOrigin::Live;
    double temperature = 0.0;
    double apparent = 0.0;
    double humidity = 0.0;
    double wind = 0.0;
    double code = 0.0;
    if (!number(current, "temperatureC", temperature) ||
        !number(current, "apparentTemperatureC", apparent) ||
        !number(current, "relativeHumidity", humidity) ||
        !number(current, "windSpeedKmh", wind) || !number(current, "weatherCode", code)) {
        cJSON_Delete(root);
        return failure("INVALID_TLESDETECTOR_RESPONSE");
    }
    data.temperature_c = static_cast<float>(temperature);
    data.apparent_temperature_c = static_cast<float>(apparent);
    data.humidity_percent = static_cast<int>(std::lround(humidity));
    data.wind_kmh = static_cast<float>(wind);
    data.weather_code = static_cast<int>(std::lround(code));
    data.condition = weather_condition_from_wmo(data.weather_code);
    if (data.wind_kmh >= 40.0F && (data.condition == WeatherCondition::Clear ||
        data.condition == WeatherCondition::PartlyCloudy || data.condition == WeatherCondition::Cloudy)) {
        data.condition = WeatherCondition::Windy;
    }
    boolean(current, "isDay", data.is_day);
    data.observed_at = text(current, "time");

    const auto metric = [&](const char* key, WeatherMetric kind, float& output, float scale = 1.0F) {
        double value = 0.0;
        if (!number(current, key, value)) return;
        output = static_cast<float>(value) * scale;
        data.metrics.set(kind);
    };
    metric("pressureHpa", WeatherMetric::Pressure, data.metrics.pressure_hpa);
    metric("windGustsKmh", WeatherMetric::WindGust, data.metrics.wind_gust_kmh);
    metric("dewPointC", WeatherMetric::DewPoint, data.metrics.dew_point_c);
    metric("visibilityM", WeatherMetric::Visibility, data.metrics.visibility_km, 0.001F);
    metric("uvIndex", WeatherMetric::UvIndex, data.metrics.uv_index);
    metric("cloudCover", WeatherMetric::CloudCover, data.metrics.cloud_cover_percent);
    metric("precipitationMm", WeatherMetric::Precipitation, data.metrics.precipitation_mm);

    const auto* air = object(root, "airQuality");
    const auto* air_current = object(air, "current");
    double aqi = 0.0;
    if (status_available(air) && number(air_current, "europeanAqi", aqi)) {
        data.metrics.european_aqi = static_cast<int>(std::lround(aqi));
        data.metrics.set(WeatherMetric::AirQuality);
    }

    const int hour_count = cJSON_GetArraySize(hours);
    for (int index = 0; index < hour_count && data.hourly_count < kWeatherHourlyCapacity; index += 3) {
        const auto* item = cJSON_GetArrayItem(hours, index);
        double value = 0.0;
        double hour_code = -1.0;
        double rain = 0.0;
        const auto time = text(item, "time");
        if (!cJSON_IsObject(item) || time.empty() || !number(item, "temperatureC", value) ||
            !number(item, "weatherCode", hour_code)) continue;
        number(item, "precipitationProbability", rain);
        const int normalized = static_cast<int>(std::lround(hour_code));
        data.hourly[data.hourly_count++] = {
            time, static_cast<float>(value), weather_condition_from_wmo(normalized), normalized,
            std::clamp(static_cast<int>(std::lround(rain)), 0, 100),
        };
    }
    if (data.hourly_count > 0) data.rain_probability_percent = data.hourly[0].rain_probability_percent;

    const int day_count = cJSON_GetArraySize(days);
    for (int index = 0; index < day_count && data.daily_count < kWeatherDailyCapacity; ++index) {
        const auto* item = cJSON_GetArrayItem(days, index);
        double minimum = 0.0;
        double maximum = 0.0;
        double day_code = -1.0;
        double rain = 0.0;
        const auto date = text(item, "date");
        if (!cJSON_IsObject(item) || date.empty() || !number(item, "temperatureMinC", minimum) ||
            !number(item, "temperatureMaxC", maximum) || !number(item, "weatherCode", day_code)) continue;
        number(item, "precipitationProbability", rain);
        const int normalized = static_cast<int>(std::lround(day_code));
        data.daily[data.daily_count++] = {
            date, static_cast<float>(minimum), static_cast<float>(maximum),
            weather_condition_from_wmo(normalized), normalized,
            std::clamp(static_cast<int>(std::lround(rain)), 0, 100),
        };
    }
    if (data.daily_count > 0) {
        data.daily_minimum_c = data.daily[0].minimum_c;
        data.daily_maximum_c = data.daily[0].maximum_c;
    }

    data.stale = text(root, "status") == "stale";
    cJSON_Delete(root);
    if (!data.valid()) return failure("INVALID_TLESDETECTOR_DATA");
    WeatherFetchResult result;
    result.ok = true;
    result.data = std::move(data);
    return result;
}

bool apply_opportunities(const std::string& json, WeatherData& data) {
    cJSON* root = cJSON_ParseWithLength(json.data(), json.size());
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    WeatherHazards hazards;
    const auto* alerts = object(root, "weatherAlerts");
    if (alerts && status_available(alerts)) {
        double active = 0.0;
        double nearby = 0.0;
        if (!number(alerts, "totalCount", active) || !number(alerts, "nearbyCount", nearby)) {
            cJSON_Delete(root);
            return false;
        }
        hazards.alerts_available = true;
        hazards.active_alert_count = std::clamp(static_cast<int>(std::lround(active)), 0, 1000);
        hazards.nearby_alert_count = std::clamp(static_cast<int>(std::lround(nearby)),
                                                0, hazards.active_alert_count);
        int highest = static_cast<int>(WeatherAlertLevel::None);
        const auto* items = array(alerts, "alerts");
        const int count = items ? cJSON_GetArraySize(items) : 0;
        for (int index = 0; index < count; ++index) {
            std::string severity = text(cJSON_GetArrayItem(items, index), "severity");
            std::transform(severity.begin(), severity.end(), severity.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            const auto level = severity == "minor" ? WeatherAlertLevel::Minor :
                severity == "moderate" ? WeatherAlertLevel::Moderate :
                severity == "severe" ? WeatherAlertLevel::Severe :
                severity == "extreme" ? WeatherAlertLevel::Extreme : WeatherAlertLevel::Unknown;
            if (level != WeatherAlertLevel::Unknown) highest = std::max(highest, static_cast<int>(level));
        }
        hazards.highest_alert = static_cast<WeatherAlertLevel>(highest);
    }

    const auto* lightning = object(root, "lightning");
    if (lightning && status_available(lightning)) {
        hazards.lightning_available = true;
        const auto* candidates = array(root, "candidates");
        const cJSON* nearest = nullptr;
        double nearest_distance = 2001.0;
        const int count = candidates ? cJSON_GetArraySize(candidates) : 0;
        for (int index = 0; index < count; ++index) {
            const auto* item = cJSON_GetArrayItem(candidates, index);
            double distance = 0.0;
            if (text(item, "source") != "observed" || !object(item, "observedLightning") ||
                !number(item, "distanceKm", distance) || distance < 0.0 || distance > 2000.0) continue;
            if (distance < nearest_distance) {
                nearest_distance = distance;
                nearest = item;
            }
        }
        if (nearest) {
            const auto* observed = object(nearest, "observedLightning");
            double bearing = 0.0;
            double age = 0.0;
            double flashes = 0.0;
            if (number(nearest, "bearingDeg", bearing) && number(observed, "ageMinutes", age) &&
                number(observed, "maxFlashesPer5Min", flashes)) {
                hazards.lightning_observed = true;
                hazards.nearest_lightning_km = static_cast<float>(nearest_distance);
                hazards.lightning_bearing_deg = std::clamp(static_cast<int>(std::lround(bearing)), 0, 360);
                hazards.lightning_age_minutes = std::clamp(static_cast<int>(std::lround(age)), 0, 1440);
                hazards.max_flashes_per_5_min = std::clamp(static_cast<int>(std::lround(flashes)), 0, 100000);
            }
            const auto* cg = object(observed, "cg");
            double positive = 0.0;
            if (cg && number(cg, "positiveCount", positive)) {
                hazards.xweather_available = true;
                hazards.positive_cg_count = std::clamp(static_cast<int>(std::lround(positive)), 0, 1000000);
                double peak = 0.0;
                if (number(cg, "maxPositiveKa", peak)) {
                    hazards.max_positive_ka = std::clamp(static_cast<int>(std::lround(std::abs(peak))), 0, 10000);
                }
            }
        }
    }
    const auto previous = data.hazards;
    data.hazards = hazards;
    const bool valid = data.valid();
    if (!valid) data.hazards = previous;
    cJSON_Delete(root);
    return valid;
}

}  // namespace

bool Esp32WeatherBackend::initialize() {
    const esp_err_t init = nvs_flash_init();
    if (init != ESP_OK) {
        ESP_LOGW(kTag, "NVS unavailable: %s", esp_err_to_name(init));
        return false;
    }
    result_mutex_ = xSemaphoreCreateMutex();
    if (!result_mutex_) return false;
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return true;
    read_nvs_string(handle, "url", server_url_, kMaximumUrlBytes);
    configured_ = valid_server_url(server_url_);

    WeatherLocation location;
    std::int64_t latitude_e6 = 0;
    std::int64_t longitude_e6 = 0;
    if (read_nvs_string(handle, "city", location.name, 96) &&
        read_nvs_string(handle, "region", location.region, 128) &&
        read_nvs_string(handle, "country", location.country, 128) &&
        read_nvs_string(handle, "cc", location.country_code, 3) &&
        read_nvs_string(handle, "tz", location.timezone, 128) &&
        nvs_get_i64(handle, "lat_e6", &latitude_e6) == ESP_OK &&
        nvs_get_i64(handle, "lon_e6", &longitude_e6) == ESP_OK) {
        location.latitude = static_cast<double>(latitude_e6) / 1'000'000.0;
        location.longitude = static_cast<double>(longitude_e6) / 1'000'000.0;
        if (location.valid()) saved_location_ = std::move(location);
    }
    nvs_close(handle);
    ESP_LOGI(kTag, "tlesdetector server: %s; saved city: %s",
             configured_ ? "configured" : "not configured",
             saved_location_ ? "present" : "not configured");
    return true;
}

bool Esp32WeatherBackend::save_location(const WeatherLocation& location) {
    if (!location.valid()) return false;
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    const auto latitude_e6 = static_cast<std::int64_t>(std::llround(location.latitude * 1'000'000.0));
    const auto longitude_e6 = static_cast<std::int64_t>(std::llround(location.longitude * 1'000'000.0));
    const bool ok = write_nvs_string(handle, "city", location.name) &&
        write_nvs_string(handle, "region", location.region) &&
        write_nvs_string(handle, "country", location.country) &&
        write_nvs_string(handle, "cc", location.country_code) &&
        write_nvs_string(handle, "tz", location.timezone) &&
        nvs_set_i64(handle, "lat_e6", latitude_e6) == ESP_OK &&
        nvs_set_i64(handle, "lon_e6", longitude_e6) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    if (ok) saved_location_ = location;
    return ok;
}

bool Esp32WeatherBackend::request(const WeatherLocation& location) {
    bool expected = false;
    if (!configured_ || !location.valid() || !busy_.compare_exchange_strong(expected, true)) return false;
    pending_location_ = location;
    if (xTaskCreate(worker_entry, "aura-weather", 8192, this, 4, nullptr) != pdPASS) {
        busy_ = false;
        return false;
    }
    return true;
}

void Esp32WeatherBackend::worker_entry(void* context) {
    auto* self = static_cast<Esp32WeatherBackend*>(context);
    auto result = self->fetch(self->pending_location_);
    if (xSemaphoreTake(self->result_mutex_, portMAX_DELAY) == pdTRUE) {
        self->completed_ = std::move(result);
        xSemaphoreGive(self->result_mutex_);
    }
    self->busy_ = false;
    vTaskDelete(nullptr);
}

WeatherFetchResult Esp32WeatherBackend::fetch(const WeatherLocation& location) const {
    const std::string query = "?lat=" + std::to_string(location.latitude) +
                              "&lon=" + std::to_string(location.longitude);
    ResponseBuffer response;
    int status = 0;
    if (!get_json(server_url_ + "/api/weather" + query, response, status)) {
        return failure("NETWORK_UNAVAILABLE", true);
    }
    if (status != 200) return failure("TLESDETECTOR_WEATHER_ERROR");
    auto result = parse_payload(response.body, location);
    if (!result.ok) return result;

    if (get_json(server_url_ + "/api/opportunities" + query, response, status) && status == 200) {
        apply_opportunities(response.body, result.data);
    }
    return result;
}

bool Esp32WeatherBackend::poll(WeatherFetchResult& result) {
    if (!result_mutex_ || xSemaphoreTake(result_mutex_, 0) != pdTRUE) return false;
    if (!completed_) {
        xSemaphoreGive(result_mutex_);
        return false;
    }
    result = std::move(*completed_);
    completed_.reset();
    xSemaphoreGive(result_mutex_);
    return true;
}

}  // namespace aura::firmware

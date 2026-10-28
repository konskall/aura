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
#include <string_view>
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

WeatherGeocodingResult geocoding_failure(const char* code, bool offline = false) {
    WeatherGeocodingResult result;
    result.error_code = code;
    result.offline = offline;
    return result;
}

std::string percent_encode(std::string_view value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size() * 3U);
    for (const unsigned char ch : value) {
        const bool unreserved = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                                (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
                                ch == '.' || ch == '~';
        if (unreserved) {
            encoded.push_back(static_cast<char>(ch));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[ch >> 4U]);
            encoded.push_back(hex[ch & 0x0FU]);
        }
    }
    return encoded;
}

bool inferred_daylight(const std::string& iso_time) {
    const auto separator = iso_time.find('T');
    if (separator == std::string::npos || separator + 3 > iso_time.size()) return true;
    const char a = iso_time[separator + 1];
    const char b = iso_time[separator + 2];
    if (a < '0' || a > '2' || b < '0' || b > '9') return true;
    const int hour = (a - '0') * 10 + (b - '0');
    return hour >= 7 && hour < 19;
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

bool array_number_at(const cJSON* values, int index, double& output) {
    const auto* value = values && index >= 0 ? cJSON_GetArrayItem(values, index) : nullptr;
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble)) return false;
    output = value->valuedouble;
    return true;
}

std::string array_text_at(const cJSON* values, int index) {
    const auto* value = values && index >= 0 ? cJSON_GetArrayItem(values, index) : nullptr;
    return cJSON_IsString(value) && value->valuestring ? value->valuestring : std::string{};
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
        bool hour_is_day = inferred_daylight(time);
        boolean(item, "isDay", hour_is_day);
        const int normalized = static_cast<int>(std::lround(hour_code));
        data.hourly[data.hourly_count++] = {
            time, static_cast<float>(value), weather_condition_from_wmo(normalized), normalized,
            std::clamp(static_cast<int>(std::lround(rain)), 0, 100), hour_is_day,
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

WeatherFetchResult parse_open_meteo_payload(const std::string& json,
                                            const WeatherLocation& location) {
    cJSON* root = cJSON_ParseWithLength(json.data(), json.size());
    if (!root) return failure("INVALID_WEATHER_RESPONSE");
    const auto* current = object(root, "current");
    const auto* hourly = object(root, "hourly");
    const auto* daily = object(root, "daily");
    if (!current || !hourly || !daily) {
        cJSON_Delete(root);
        return failure("INVALID_WEATHER_RESPONSE");
    }

    WeatherData data;
    data.location = location;
    data.provider = WeatherProvider::OpenMeteo;
    data.origin = WeatherDataOrigin::Live;
    double temperature = 0.0;
    double apparent = 0.0;
    double humidity = 0.0;
    double wind = 0.0;
    double code = 0.0;
    double is_day = 1.0;
    if (!number(current, "temperature_2m", temperature) ||
        !number(current, "apparent_temperature", apparent) ||
        !number(current, "relative_humidity_2m", humidity) ||
        !number(current, "wind_speed_10m", wind) ||
        !number(current, "weather_code", code)) {
        cJSON_Delete(root);
        return failure("INVALID_WEATHER_RESPONSE");
    }
    number(current, "is_day", is_day);
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
    data.is_day = is_day >= 0.5;
    data.observed_at = text(current, "time");

    const auto metric = [&](const char* key, WeatherMetric kind, float& output, float scale = 1.0F) {
        double value = 0.0;
        if (!number(current, key, value)) return;
        output = static_cast<float>(value) * scale;
        data.metrics.set(kind);
    };
    metric("pressure_msl", WeatherMetric::Pressure, data.metrics.pressure_hpa);
    metric("wind_gusts_10m", WeatherMetric::WindGust, data.metrics.wind_gust_kmh);
    metric("dew_point_2m", WeatherMetric::DewPoint, data.metrics.dew_point_c);
    metric("visibility", WeatherMetric::Visibility, data.metrics.visibility_km, 0.001F);
    metric("uv_index", WeatherMetric::UvIndex, data.metrics.uv_index);
    metric("cloud_cover", WeatherMetric::CloudCover, data.metrics.cloud_cover_percent);
    metric("precipitation", WeatherMetric::Precipitation, data.metrics.precipitation_mm);

    const auto* daily_time = array(daily, "time");
    const auto* daily_min = array(daily, "temperature_2m_min");
    const auto* daily_max = array(daily, "temperature_2m_max");
    const auto* daily_codes = array(daily, "weather_code");
    const auto* daily_rain = array(daily, "precipitation_probability_max");
    if (!daily_time || !daily_min || !daily_max || !daily_codes) {
        cJSON_Delete(root);
        return failure("INVALID_WEATHER_RESPONSE");
    }
    const int day_count = std::min({cJSON_GetArraySize(daily_time), cJSON_GetArraySize(daily_min),
                                    cJSON_GetArraySize(daily_max), cJSON_GetArraySize(daily_codes),
                                    static_cast<int>(kWeatherDailyCapacity)});
    for (int index = 0; index < day_count; ++index) {
        const auto date = array_text_at(daily_time, index);
        double minimum = 0.0;
        double maximum = 0.0;
        double day_code = -1.0;
        double rain = 0.0;
        if (date.empty() || !array_number_at(daily_min, index, minimum) ||
            !array_number_at(daily_max, index, maximum) ||
            !array_number_at(daily_codes, index, day_code)) continue;
        array_number_at(daily_rain, index, rain);
        const int normalized = static_cast<int>(std::lround(day_code));
        data.daily[data.daily_count++] = {
            date, static_cast<float>(minimum), static_cast<float>(maximum),
            weather_condition_from_wmo(normalized), normalized,
            std::clamp(static_cast<int>(std::lround(rain)), 0, 100),
        };
    }
    if (data.daily_count == 0) {
        cJSON_Delete(root);
        return failure("INVALID_WEATHER_RESPONSE");
    }
    data.daily_minimum_c = data.daily[0].minimum_c;
    data.daily_maximum_c = data.daily[0].maximum_c;

    const auto* hourly_time = array(hourly, "time");
    const auto* hourly_temp = array(hourly, "temperature_2m");
    const auto* hourly_code = array(hourly, "weather_code");
    const auto* hourly_rain = array(hourly, "precipitation_probability");
    const auto* hourly_day = array(hourly, "is_day");
    if (!hourly_time || !hourly_temp || !hourly_code) {
        cJSON_Delete(root);
        return failure("INVALID_WEATHER_RESPONSE");
    }
    const int available = std::min({cJSON_GetArraySize(hourly_time), cJSON_GetArraySize(hourly_temp),
                                    cJSON_GetArraySize(hourly_code)});
    int start = 0;
    while (start + 1 < available && array_text_at(hourly_time, start) < data.observed_at) ++start;
    for (int index = start; index < available && data.hourly_count < kWeatherHourlyCapacity; index += 3) {
        const auto time = array_text_at(hourly_time, index);
        double value = 0.0;
        double hour_code = -1.0;
        double rain = 0.0;
        double hour_day = inferred_daylight(time) ? 1.0 : 0.0;
        if (time.empty() || !array_number_at(hourly_temp, index, value) ||
            !array_number_at(hourly_code, index, hour_code)) continue;
        array_number_at(hourly_rain, index, rain);
        array_number_at(hourly_day, index, hour_day);
        const int normalized = static_cast<int>(std::lround(hour_code));
        data.hourly[data.hourly_count++] = {
            time, static_cast<float>(value), weather_condition_from_wmo(normalized), normalized,
            std::clamp(static_cast<int>(std::lround(rain)), 0, 100), hour_day >= 0.5,
        };
    }
    double current_rain = 0.0;
    if (array_number_at(hourly_rain, start, current_rain)) {
        data.rain_probability_percent =
            std::clamp(static_cast<int>(std::lround(current_rain)), 0, 100);
    }

    cJSON_Delete(root);
    if (!data.valid()) return failure("INVALID_WEATHER_DATA");
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
    initialized_ = true;
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return true;
    read_nvs_string(handle, "url", server_url_, kMaximumUrlBytes);
    tles_configured_ = valid_server_url(server_url_);

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
    std::uint16_t refresh_minutes = 0;
    if (nvs_get_u16(handle, "refresh_min", &refresh_minutes) == ESP_OK) {
        const auto interval = static_cast<WeatherRefreshInterval>(refresh_minutes);
        if (weather_refresh_interval_valid(interval)) refresh_interval_ = interval;
    }
    nvs_close(handle);
    ESP_LOGI(kTag, "tlesdetector server: %s; saved city: %s",
             tles_configured_ ? "configured" : "not configured; using direct Open-Meteo",
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

bool Esp32WeatherBackend::save_refresh_interval(WeatherRefreshInterval interval) {
    if (!weather_refresh_interval_valid(interval)) return false;
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    const bool ok = nvs_set_u16(handle, "refresh_min",
                                static_cast<std::uint16_t>(weather_refresh_interval_minutes(interval))) == ESP_OK &&
                    nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    if (ok) refresh_interval_ = interval;
    return ok;
}

bool Esp32WeatherBackend::request(const WeatherLocation& location) {
    bool expected = false;
    if (!initialized_ || !result_mutex_ || !location.valid() ||
        !busy_.compare_exchange_strong(expected, true)) return false;
    if (xSemaphoreTake(result_mutex_, portMAX_DELAY) != pdTRUE) {
        busy_ = false;
        return false;
    }
    // A new explicit request supersedes a completed result that the UI has not
    // consumed yet. Clearing both tagged mailboxes prevents an old fetch from
    // replacing a new search (or vice versa) on the following UI tick.
    completed_.reset();
    completed_search_.reset();
    xSemaphoreGive(result_mutex_);
    pending_operation_ = Operation::Fetch;
    pending_location_ = location;
    if (xTaskCreate(worker_entry, "aura-weather", 8192, this, 4, nullptr) != pdPASS) {
        busy_ = false;
        return false;
    }
    return true;
}

bool Esp32WeatherBackend::request_search(std::string query, InterfaceLanguage language) {
    if (!result_mutex_ || query.size() < 2 || query.size() > 96 ||
        std::any_of(query.begin(), query.end(), [](unsigned char ch) { return ch < 0x20 || ch == 0x7F; })) {
        return false;
    }
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) return false;
    if (xSemaphoreTake(result_mutex_, portMAX_DELAY) != pdTRUE) {
        busy_ = false;
        return false;
    }
    completed_.reset();
    completed_search_.reset();
    xSemaphoreGive(result_mutex_);
    pending_operation_ = Operation::Search;
    pending_query_ = std::move(query);
    pending_language_ = language;
    if (xTaskCreate(worker_entry, "aura-geocode", 8192, this, 4, nullptr) != pdPASS) {
        busy_ = false;
        return false;
    }
    return true;
}

void Esp32WeatherBackend::worker_entry(void* context) {
    auto* self = static_cast<Esp32WeatherBackend*>(context);
    if (self->pending_operation_ == Operation::Search) {
        auto result = self->search(self->pending_query_, self->pending_language_);
        if (xSemaphoreTake(self->result_mutex_, portMAX_DELAY) == pdTRUE) {
            self->completed_search_ = std::move(result);
            xSemaphoreGive(self->result_mutex_);
        }
    } else {
        auto result = self->fetch(self->pending_location_);
        if (xSemaphoreTake(self->result_mutex_, portMAX_DELAY) == pdTRUE) {
            self->completed_ = std::move(result);
            xSemaphoreGive(self->result_mutex_);
        }
    }
    self->busy_ = false;
    vTaskDelete(nullptr);
}

WeatherGeocodingResult Esp32WeatherBackend::search(const std::string& query,
                                                   InterfaceLanguage language) const {
    const std::string url = "https://geocoding-api.open-meteo.com/v1/search?name=" +
        percent_encode(query) + "&count=" + std::to_string(kWeatherSuggestionCapacity) +
        "&language=" + interface_language_code(language) + "&format=json";
    ResponseBuffer response;
    int status = 0;
    if (!get_json(url, response, status)) return geocoding_failure("NETWORK_UNAVAILABLE", true);
    if (status != 200) return geocoding_failure("GEOCODING_HTTP_ERROR");

    cJSON* root = cJSON_ParseWithLength(response.body.data(), response.body.size());
    if (!root) return geocoding_failure("INVALID_GEOCODING_RESPONSE");
    const auto* results = array(root, "results");
    if (!results) {
        cJSON_Delete(root);
        return geocoding_failure("CITY_NOT_FOUND");
    }
    WeatherGeocodingResult result;
    const int count = cJSON_GetArraySize(results);
    for (int index = 0; index < count && result.locations.size() < kWeatherSuggestionCapacity; ++index) {
        const auto* item = cJSON_GetArrayItem(results, index);
        if (!cJSON_IsObject(item)) continue;
        WeatherLocation location;
        location.name = text(item, "name");
        location.region = text(item, "admin1");
        location.country = text(item, "country");
        location.country_code = text(item, "country_code");
        location.timezone = text(item, "timezone");
        if (!number(item, "latitude", location.latitude) ||
            !number(item, "longitude", location.longitude) || !location.valid()) continue;
        result.locations.push_back(std::move(location));
    }
    cJSON_Delete(root);
    if (result.locations.empty()) return geocoding_failure("CITY_NOT_FOUND");
    result.ok = true;
    return result;
}

WeatherFetchResult Esp32WeatherBackend::fetch(const WeatherLocation& location) const {
    const std::string query = "?lat=" + std::to_string(location.latitude) +
                              "&lon=" + std::to_string(location.longitude);
    ResponseBuffer response;
    int status = 0;
    if (tles_configured_ && get_json(server_url_ + "/api/weather" + query, response, status) &&
        status == 200) {
        auto result = parse_payload(response.body, location);
        if (result.ok) {
            if (get_json(server_url_ + "/api/opportunities" + query, response, status) && status == 200) {
                apply_opportunities(response.body, result.data);
            }
            return result;
        }
    }

    const std::string forecast_url =
        "https://api.open-meteo.com/v1/forecast?latitude=" + std::to_string(location.latitude) +
        "&longitude=" + std::to_string(location.longitude) +
        "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,wind_speed_10m,wind_gusts_10m,dew_point_2m,pressure_msl,visibility,uv_index,cloud_cover,precipitation"
        "&hourly=temperature_2m,weather_code,precipitation_probability,is_day"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
        "&timezone=auto&forecast_days=5";
    if (!get_json(forecast_url, response, status)) return failure("NETWORK_UNAVAILABLE", true);
    if (status != 200) return failure("WEATHER_HTTP_ERROR");
    return parse_open_meteo_payload(response.body, location);
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

bool Esp32WeatherBackend::poll_search(WeatherGeocodingResult& result) {
    if (!result_mutex_ || xSemaphoreTake(result_mutex_, 0) != pdTRUE) return false;
    if (!completed_search_) {
        xSemaphoreGive(result_mutex_);
        return false;
    }
    result = std::move(*completed_search_);
    completed_search_.reset();
    xSemaphoreGive(result_mutex_);
    return true;
}

}  // namespace aura::firmware

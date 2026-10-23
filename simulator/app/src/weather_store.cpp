#include "aura/weather_store.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace aura::simulator {

namespace {

constexpr std::size_t kMaximumStoreBytes = 32 * 1024;

std::string encode(std::string_view value) {
    std::ostringstream output;
    output << std::uppercase << std::hex;
    for (const unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == ':' || ch == '/') {
            output << static_cast<char>(ch);
        } else {
            output << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
    }
    return output.str();
}

int hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

std::string decode(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2 < value.size()) {
            const int high = hex_value(value[index + 1]);
            const int low = hex_value(value[index + 2]);
            if (high >= 0 && low >= 0) {
                output.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        output.push_back(value[index]);
    }
    return output;
}

std::map<std::string, std::string> read_values(const std::filesystem::path& path, const char* header) {
    std::map<std::string, std::string> values;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > kMaximumStoreBytes) return values;
    std::ifstream input(path, std::ios::binary);
    std::string line;
    if (!std::getline(input, line) || line != header) return values;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        values[line.substr(0, separator)] = decode(line.substr(separator + 1));
    }
    return values;
}

bool atomic_write(const std::filesystem::path& destination, const std::string& contents) {
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) return false;
    auto temporary = destination;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.flush();
        if (!output) return false;
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, error);
        return false;
    }
#else
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
#endif
    return true;
}

void write_location(std::ostringstream& output, const WeatherLocation& location) {
    output << "name=" << encode(location.name) << '\n'
           << "region=" << encode(location.region) << '\n'
           << "country=" << encode(location.country) << '\n'
           << "country_code=" << encode(location.country_code) << '\n'
           << "timezone=" << encode(location.timezone) << '\n'
           << std::setprecision(10)
           << "latitude=" << location.latitude << '\n'
           << "longitude=" << location.longitude << '\n';
}

bool parse_double(const std::map<std::string, std::string>& values, const char* key, double& value) {
    const auto found = values.find(key);
    if (found == values.end()) return false;
    try {
        std::size_t used = 0;
        value = std::stod(found->second, &used);
        return used == found->second.size();
    } catch (...) { return false; }
}

bool parse_int(const std::map<std::string, std::string>& values, const char* key, int& value) {
    const auto found = values.find(key);
    if (found == values.end()) return false;
    try {
        std::size_t used = 0;
        value = std::stoi(found->second, &used);
        return used == found->second.size();
    } catch (...) { return false; }
}

WeatherLocation read_location(const std::map<std::string, std::string>& values) {
    WeatherLocation location;
    const auto get = [&](const char* key) {
        const auto found = values.find(key);
        return found == values.end() ? std::string{} : found->second;
    };
    location.name = get("name");
    location.region = get("region");
    location.country = get("country");
    location.country_code = get("country_code");
    location.timezone = get("timezone");
    parse_double(values, "latitude", location.latitude);
    parse_double(values, "longitude", location.longitude);
    return location;
}

std::vector<std::string> split(std::string_view value, char separator) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const auto end = value.find(separator, start);
        parts.emplace_back(value.substr(start, end == std::string_view::npos ? value.size() - start : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return parts;
}

}  // namespace

WeatherStore::WeatherStore(std::filesystem::path base_path) : base_path_(std::move(base_path)) {}

std::filesystem::path WeatherStore::default_base_path() {
#ifdef _WIN32
    if (const wchar_t* local = _wgetenv(L"LOCALAPPDATA")) {
        return std::filesystem::path(local) / L"AURA" / L"weather-v1";
    }
#endif
    return std::filesystem::temp_directory_path() / "aura" / "weather-v1";
}

std::filesystem::path WeatherStore::location_path() const {
    auto path = base_path_;
    path += ".location";
    return path;
}

std::filesystem::path WeatherStore::cache_path() const {
    auto path = base_path_;
    path += ".cache";
    return path;
}

std::optional<WeatherLocation> WeatherStore::load_location() const {
    const auto values = read_values(location_path(), "AURA_WEATHER_LOCATION_V1");
    if (values.empty()) return std::nullopt;
    auto location = read_location(values);
    if (!location.valid()) return std::nullopt;
    return location;
}

std::optional<WeatherData> WeatherStore::load_cache() const {
    auto values = read_values(cache_path(), "AURA_WEATHER_CACHE_V2");
    const bool version_two = !values.empty();
    if (values.empty()) values = read_values(cache_path(), "AURA_WEATHER_CACHE_V1");
    if (values.empty()) return std::nullopt;
    WeatherData data;
    data.location = read_location(values);
    double number = 0.0;
    int integer = 0;
    if (!parse_double(values, "temperature", number)) return std::nullopt;
    data.temperature_c = static_cast<float>(number);
    if (!parse_double(values, "apparent", number)) return std::nullopt;
    data.apparent_temperature_c = static_cast<float>(number);
    if (!parse_double(values, "wind", number)) return std::nullopt;
    data.wind_kmh = static_cast<float>(number);
    if (!parse_int(values, "humidity", data.humidity_percent)) return std::nullopt;
    if (!parse_double(values, "daily_min", number)) return std::nullopt;
    data.daily_minimum_c = static_cast<float>(number);
    if (!parse_double(values, "daily_max", number)) return std::nullopt;
    data.daily_maximum_c = static_cast<float>(number);
    if (!parse_int(values, "condition", integer)) return std::nullopt;
    data.condition = static_cast<WeatherCondition>(integer);
    if (!parse_int(values, "is_day", integer)) return std::nullopt;
    data.is_day = integer != 0;
    if (parse_int(values, "weather_code", integer)) data.weather_code = integer;
    if (parse_int(values, "rain_probability", integer)) data.rain_probability_percent = integer;
    if (version_two) {
        if (!parse_int(values, "provider", integer)) return std::nullopt;
        data.provider = static_cast<WeatherProvider>(integer);
        if (!parse_int(values, "metrics_available", integer) || integer < 0 || integer > 0xFFFF) {
            return std::nullopt;
        }
        data.metrics.available = static_cast<std::uint16_t>(integer);
        const auto metric_float = [&](const char* key, float& destination) {
            double parsed = 0.0;
            if (!parse_double(values, key, parsed)) return false;
            destination = static_cast<float>(parsed);
            return true;
        };
        if ((data.metrics.has(WeatherMetric::Pressure) && !metric_float("pressure_hpa", data.metrics.pressure_hpa)) ||
            (data.metrics.has(WeatherMetric::WindGust) && !metric_float("wind_gust_kmh", data.metrics.wind_gust_kmh)) ||
            (data.metrics.has(WeatherMetric::DewPoint) && !metric_float("dew_point_c", data.metrics.dew_point_c)) ||
            (data.metrics.has(WeatherMetric::Visibility) && !metric_float("visibility_km", data.metrics.visibility_km)) ||
            (data.metrics.has(WeatherMetric::UvIndex) && !metric_float("uv_index", data.metrics.uv_index)) ||
            (data.metrics.has(WeatherMetric::CloudCover) && !metric_float("cloud_cover", data.metrics.cloud_cover_percent)) ||
            (data.metrics.has(WeatherMetric::Precipitation) && !metric_float("precipitation_mm", data.metrics.precipitation_mm)) ||
            (data.metrics.has(WeatherMetric::AirQuality) && !parse_int(values, "european_aqi", data.metrics.european_aqi))) {
            return std::nullopt;
        }
        if (!parse_int(values, "alerts_available", integer)) return std::nullopt;
        data.hazards.alerts_available = integer != 0;
        if (!parse_int(values, "active_alert_count", data.hazards.active_alert_count) ||
            !parse_int(values, "nearby_alert_count", data.hazards.nearby_alert_count) ||
            !parse_int(values, "highest_alert", integer)) return std::nullopt;
        data.hazards.highest_alert = static_cast<WeatherAlertLevel>(integer);
        if (!parse_int(values, "lightning_available", integer)) return std::nullopt;
        data.hazards.lightning_available = integer != 0;
        if (!parse_int(values, "lightning_observed", integer)) return std::nullopt;
        data.hazards.lightning_observed = integer != 0;
        if (!metric_float("nearest_lightning_km", data.hazards.nearest_lightning_km) ||
            !parse_int(values, "lightning_bearing_deg", data.hazards.lightning_bearing_deg) ||
            !parse_int(values, "lightning_age_minutes", data.hazards.lightning_age_minutes) ||
            !parse_int(values, "max_flashes_per_5_min", data.hazards.max_flashes_per_5_min) ||
            !parse_int(values, "xweather_available", integer)) return std::nullopt;
        data.hazards.xweather_available = integer != 0;
        if (!parse_int(values, "positive_cg_count", data.hazards.positive_cg_count) ||
            !parse_int(values, "max_positive_ka", data.hazards.max_positive_ka)) return std::nullopt;
    }
    const auto observed = values.find("observed_at");
    if (observed != values.end()) data.observed_at = observed->second;
    for (std::size_t index = 0; index < kWeatherHourlyCapacity; ++index) {
        const auto found = values.find("hourly" + std::to_string(index));
        if (found == values.end()) break;
        const auto parts = split(found->second, '|');
        if (parts.size() != 3 && parts.size() != 5) return std::nullopt;
        try {
            data.hourly[data.hourly_count++] = {parts[0], std::stof(parts[1]),
                static_cast<WeatherCondition>(std::stoi(parts[2])),
                parts.size() == 5 ? std::stoi(parts[3]) : -1,
                parts.size() == 5 ? std::stoi(parts[4]) : 0};
        } catch (...) { return std::nullopt; }
    }
    for (std::size_t index = 0; index < kWeatherDailyCapacity; ++index) {
        const auto found = values.find("daily" + std::to_string(index));
        if (found == values.end()) break;
        const auto parts = split(found->second, '|');
        if (parts.size() != 4 && parts.size() != 6) return std::nullopt;
        try {
            data.daily[data.daily_count++] = {parts[0], std::stof(parts[1]), std::stof(parts[2]),
                static_cast<WeatherCondition>(std::stoi(parts[3])),
                parts.size() == 6 ? std::stoi(parts[4]) : -1,
                parts.size() == 6 ? std::stoi(parts[5]) : 0};
        } catch (...) { return std::nullopt; }
    }
    data.origin = WeatherDataOrigin::Cache;
    data.stale = true;
    if (!data.valid()) return std::nullopt;
    return data;
}

bool WeatherStore::save_location(const WeatherLocation& location) const {
    if (!location.valid()) return false;
    std::ostringstream output;
    output << "AURA_WEATHER_LOCATION_V1\n";
    write_location(output, location);
    return atomic_write(location_path(), output.str());
}

bool WeatherStore::save_cache(const WeatherData& data) const {
    if (!data.valid() || data.origin == WeatherDataOrigin::Demo) return false;
    std::ostringstream output;
    output << "AURA_WEATHER_CACHE_V2\n";
    write_location(output, data.location);
    output << "temperature=" << data.temperature_c << '\n'
           << "apparent=" << data.apparent_temperature_c << '\n'
           << "wind=" << data.wind_kmh << '\n'
           << "humidity=" << data.humidity_percent << '\n'
           << "daily_min=" << data.daily_minimum_c << '\n'
           << "daily_max=" << data.daily_maximum_c << '\n'
           << "condition=" << static_cast<int>(data.condition) << '\n'
           << "is_day=" << (data.is_day ? 1 : 0) << '\n'
           << "weather_code=" << data.weather_code << '\n'
           << "rain_probability=" << data.rain_probability_percent << '\n'
           << "observed_at=" << encode(data.observed_at) << '\n'
           << "provider=" << static_cast<int>(data.provider) << '\n'
           << "metrics_available=" << data.metrics.available << '\n'
           << "pressure_hpa=" << data.metrics.pressure_hpa << '\n'
           << "wind_gust_kmh=" << data.metrics.wind_gust_kmh << '\n'
           << "dew_point_c=" << data.metrics.dew_point_c << '\n'
           << "visibility_km=" << data.metrics.visibility_km << '\n'
           << "uv_index=" << data.metrics.uv_index << '\n'
           << "cloud_cover=" << data.metrics.cloud_cover_percent << '\n'
           << "precipitation_mm=" << data.metrics.precipitation_mm << '\n'
           << "european_aqi=" << data.metrics.european_aqi << '\n'
           << "alerts_available=" << (data.hazards.alerts_available ? 1 : 0) << '\n'
           << "active_alert_count=" << data.hazards.active_alert_count << '\n'
           << "nearby_alert_count=" << data.hazards.nearby_alert_count << '\n'
           << "highest_alert=" << static_cast<int>(data.hazards.highest_alert) << '\n'
           << "lightning_available=" << (data.hazards.lightning_available ? 1 : 0) << '\n'
           << "lightning_observed=" << (data.hazards.lightning_observed ? 1 : 0) << '\n'
           << "nearest_lightning_km=" << data.hazards.nearest_lightning_km << '\n'
           << "lightning_bearing_deg=" << data.hazards.lightning_bearing_deg << '\n'
           << "lightning_age_minutes=" << data.hazards.lightning_age_minutes << '\n'
           << "max_flashes_per_5_min=" << data.hazards.max_flashes_per_5_min << '\n'
           << "xweather_available=" << (data.hazards.xweather_available ? 1 : 0) << '\n'
           << "positive_cg_count=" << data.hazards.positive_cg_count << '\n'
           << "max_positive_ka=" << data.hazards.max_positive_ka << '\n';
    for (std::size_t index = 0; index < data.hourly_count; ++index) {
        output << "hourly" << index << '=' << encode(data.hourly[index].time + '|' +
            std::to_string(data.hourly[index].temperature_c) + '|' +
            std::to_string(static_cast<int>(data.hourly[index].condition)) + '|' +
            std::to_string(data.hourly[index].weather_code) + '|' +
            std::to_string(data.hourly[index].rain_probability_percent)) << '\n';
    }
    for (std::size_t index = 0; index < data.daily_count; ++index) {
        output << "daily" << index << '=' << encode(data.daily[index].date + '|' +
            std::to_string(data.daily[index].minimum_c) + '|' +
            std::to_string(data.daily[index].maximum_c) + '|' +
            std::to_string(static_cast<int>(data.daily[index].condition)) + '|' +
            std::to_string(data.daily[index].weather_code) + '|' +
            std::to_string(data.daily[index].rain_probability_max_percent)) << '\n';
    }
    return atomic_write(cache_path(), output.str());
}

bool WeatherStore::clear_location() const {
    std::error_code error;
    const bool removed_location = !std::filesystem::exists(location_path()) ||
                                  std::filesystem::remove(location_path(), error);
    error.clear();
    const bool removed_cache = !std::filesystem::exists(cache_path()) ||
                               std::filesystem::remove(cache_path(), error);
    return removed_location && removed_cache;
}

}  // namespace aura::simulator

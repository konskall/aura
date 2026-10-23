#include "aura/open_meteo_weather_service.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace aura::simulator {

namespace {

constexpr std::size_t kMaximumResponseBytes = 512 * 1024;
constexpr int kMaximumJsonDepth = 20;

struct JsonValue {
    enum class Type { Null, Boolean, Number, String, Array, Object };
    Type type{Type::Null};
    bool boolean{false};
    double number{0.0};
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    const JsonValue* member(std::string_view key) const {
        if (type != Type::Object) return nullptr;
        for (const auto& entry : object) {
            if (entry.first == key) return &entry.second;
        }
        return nullptr;
    }
};

void append_utf8(std::string& output, std::uint32_t codepoint) {
    if (codepoint <= 0x7F) output.push_back(static_cast<char>(codepoint));
    else if (codepoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF && !(codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    bool parse(JsonValue& value) {
        skip_space();
        if (!parse_value(value, 0)) return false;
        skip_space();
        return position_ == input_.size();
    }

private:
    void skip_space() {
        while (position_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[position_]))) ++position_;
    }

    bool consume(char expected) {
        skip_space();
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    int hex_digit(char value) const {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    }

    bool unicode_escape(std::uint32_t& value) {
        if (position_ + 4 > input_.size()) return false;
        value = 0;
        for (int index = 0; index < 4; ++index) {
            const int digit = hex_digit(input_[position_++]);
            if (digit < 0) return false;
            value = (value << 4U) | static_cast<std::uint32_t>(digit);
        }
        return true;
    }

    bool parse_string(std::string& value) {
        skip_space();
        if (position_ >= input_.size() || input_[position_++] != '"') return false;
        value.clear();
        while (position_ < input_.size()) {
            const unsigned char ch = static_cast<unsigned char>(input_[position_++]);
            if (ch == '"') return true;
            if (ch < 0x20) return false;
            if (ch != '\\') {
                value.push_back(static_cast<char>(ch));
                continue;
            }
            if (position_ >= input_.size()) return false;
            const char escaped = input_[position_++];
            switch (escaped) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case 'u': {
                    std::uint32_t codepoint = 0;
                    if (!unicode_escape(codepoint)) return false;
                    if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                        if (position_ + 6 > input_.size() || input_[position_] != '\\' ||
                            input_[position_ + 1] != 'u') return false;
                        position_ += 2;
                        std::uint32_t low = 0;
                        if (!unicode_escape(low) || low < 0xDC00 || low > 0xDFFF) return false;
                        codepoint = 0x10000 + ((codepoint - 0xD800) << 10U) + (low - 0xDC00);
                    }
                    append_utf8(value, codepoint);
                    break;
                }
                default: return false;
            }
            if (value.size() > kMaximumResponseBytes) return false;
        }
        return false;
    }

    bool parse_number(JsonValue& value) {
        const auto start = position_;
        if (position_ < input_.size() && input_[position_] == '-') ++position_;
        if (position_ >= input_.size()) return false;
        if (input_[position_] == '0') ++position_;
        else {
            if (!std::isdigit(static_cast<unsigned char>(input_[position_]))) return false;
            while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
        }
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            if (position_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[position_]))) return false;
            while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            if (position_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[position_]))) return false;
            while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
        }
        const std::string token(input_.substr(start, position_ - start));
        char* end = nullptr;
        value.number = std::strtod(token.c_str(), &end);
        value.type = JsonValue::Type::Number;
        return end && *end == '\0' && std::isfinite(value.number);
    }

    bool literal(std::string_view token) {
        if (input_.substr(position_, token.size()) != token) return false;
        position_ += token.size();
        return true;
    }

    bool parse_value(JsonValue& value, int depth) {
        if (depth > kMaximumJsonDepth) return false;
        skip_space();
        if (position_ >= input_.size()) return false;
        const char ch = input_[position_];
        if (ch == '"') {
            value.type = JsonValue::Type::String;
            return parse_string(value.string);
        }
        if (ch == '{') {
            value.type = JsonValue::Type::Object;
            ++position_;
            skip_space();
            if (position_ < input_.size() && input_[position_] == '}') { ++position_; return true; }
            for (;;) {
                std::string key;
                if (!parse_string(key) || !consume(':')) return false;
                JsonValue child;
                if (!parse_value(child, depth + 1)) return false;
                value.object.emplace_back(std::move(key), std::move(child));
                skip_space();
                if (position_ < input_.size() && input_[position_] == '}') { ++position_; return true; }
                if (!consume(',')) return false;
            }
        }
        if (ch == '[') {
            value.type = JsonValue::Type::Array;
            ++position_;
            skip_space();
            if (position_ < input_.size() && input_[position_] == ']') { ++position_; return true; }
            for (;;) {
                JsonValue child;
                if (!parse_value(child, depth + 1)) return false;
                value.array.push_back(std::move(child));
                skip_space();
                if (position_ < input_.size() && input_[position_] == ']') { ++position_; return true; }
                if (!consume(',')) return false;
            }
        }
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) return parse_number(value);
        if (literal("true")) { value.type = JsonValue::Type::Boolean; value.boolean = true; return true; }
        if (literal("false")) { value.type = JsonValue::Type::Boolean; value.boolean = false; return true; }
        if (literal("null")) { value.type = JsonValue::Type::Null; return true; }
        return false;
    }

    std::string_view input_;
    std::size_t position_{0};
};

const JsonValue* object_member(const JsonValue* value, std::string_view key, JsonValue::Type type) {
    const auto* result = value ? value->member(key) : nullptr;
    return result && result->type == type ? result : nullptr;
}

bool number_member(const JsonValue* value, std::string_view key, double& output) {
    const auto* member = object_member(value, key, JsonValue::Type::Number);
    if (!member) return false;
    output = member->number;
    return true;
}

std::string string_member(const JsonValue* value, std::string_view key) {
    const auto* member = object_member(value, key, JsonValue::Type::String);
    return member ? member->string : std::string{};
}

bool boolean_member(const JsonValue* value, std::string_view key, bool& output) {
    const auto* member = object_member(value, key, JsonValue::Type::Boolean);
    if (!member) return false;
    output = member->boolean;
    return true;
}

bool available_status(const JsonValue* value, std::string_view key = "status") {
    const auto status = string_member(value, key);
    return status == "available" || status == "stale";
}

double array_number(const JsonValue* value, std::size_t index, double fallback) {
    if (!value || value->type != JsonValue::Type::Array || index >= value->array.size() ||
        value->array[index].type != JsonValue::Type::Number) return fallback;
    return value->array[index].number;
}

std::string array_string(const JsonValue* value, std::size_t index) {
    if (!value || value->type != JsonValue::Type::Array || index >= value->array.size() ||
        value->array[index].type != JsonValue::Type::String) return {};
    return value->array[index].string;
}

std::string url_encode(std::string_view value) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex;
    for (const unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded << static_cast<char>(ch);
        } else {
            encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
    }
    return encoded.str();
}

#ifdef _WIN32
struct HttpResponse {
    int status{0};
    std::string body;
    bool transport_error{false};
};

bool safe_header_value(std::string_view value) {
    if (value.size() < 12 || value.size() > 512) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch >= 0x21 && ch <= 0x7E;
    });
}

HttpResponse http_get(const wchar_t* host, INTERNET_PORT port, bool secure,
                      const std::string& path, std::string_view bearer = {},
                      int receive_timeout_ms = 12000) {
    HttpResponse response;
    HINTERNET session = WinHttpOpen(L"AURA-Weather/10.3", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { response.transport_error = true; return response; }
    WinHttpSetTimeouts(session, 5000, 5000, 5000, receive_timeout_ms);
    HINTERNET connection = WinHttpConnect(session, host, port, 0);
    if (!connection) {
        response.transport_error = true;
        WinHttpCloseHandle(session);
        return response;
    }
    const std::wstring wide_path(path.begin(), path.end());
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", wide_path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
        response.transport_error = true;
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }
    std::wstring headers = L"Accept: application/json\r\nAccept-Encoding: identity\r\n";
    if (!bearer.empty()) {
        if (!safe_header_value(bearer)) {
            response.transport_error = true;
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return response;
        }
        headers += L"Authorization: Bearer ";
        headers.append(bearer.begin(), bearer.end());
        headers += L"\r\n";
    }
    const BOOL sent = WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1L),
                                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
        response.transport_error = true;
    } else {
        DWORD status_size = sizeof(response.status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &response.status, &status_size,
                            WINHTTP_NO_HEADER_INDEX);
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
            if (response.body.size() + available > kMaximumResponseBytes) {
                response.body.clear();
                response.status = 0;
                break;
            }
            const auto old_size = response.body.size();
            response.body.resize(old_size + available);
            DWORD read = 0;
            if (!WinHttpReadData(request, response.body.data() + old_size, available, &read)) {
                response.transport_error = true;
                response.body.clear();
                break;
            }
            response.body.resize(old_size + read);
        }
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return response;
}

HttpResponse https_get(const wchar_t* host, const std::string& path) {
    return http_get(host, INTERNET_DEFAULT_HTTPS_PORT, true, path);
}

struct TlesDetectorEndpoint {
    bool secure{true};
    std::wstring host;
    INTERNET_PORT port{INTERNET_DEFAULT_HTTPS_PORT};
    std::string path_prefix;
};

bool loopback_host(std::string_view host) {
    return host == "127.0.0.1" || host == "localhost";
}

bool parse_tlesdetector_endpoint(TlesDetectorEndpoint& endpoint) {
    const char* raw_url = std::getenv("AURA_TLESDETECTOR_URL");
    if (!raw_url) return false;
    const std::string url(raw_url);
    if (url.empty() || url.size() > 1024 || url.find('@') != std::string::npos ||
        url.find('#') != std::string::npos || url.find('?') != std::string::npos) return false;
    std::size_t offset = 0;
    if (url.rfind("https://", 0) == 0) {
        endpoint.secure = true;
        endpoint.port = INTERNET_DEFAULT_HTTPS_PORT;
        offset = 8;
    } else if (url.rfind("http://", 0) == 0) {
        endpoint.secure = false;
        endpoint.port = INTERNET_DEFAULT_HTTP_PORT;
        offset = 7;
    } else {
        return false;
    }
    const auto slash = url.find('/', offset);
    const auto authority = url.substr(offset, slash == std::string::npos ? std::string::npos : slash - offset);
    endpoint.path_prefix = slash == std::string::npos ? "" : url.substr(slash);
    while (!endpoint.path_prefix.empty() && endpoint.path_prefix.back() == '/') {
        endpoint.path_prefix.pop_back();
    }
    if (authority.empty() || (!endpoint.path_prefix.empty() && endpoint.path_prefix.front() != '/')) {
        return false;
    }
    const auto colon = authority.rfind(':');
    const std::string host = colon == std::string::npos ? authority : authority.substr(0, colon);
    if (host.empty() || !std::all_of(host.begin(), host.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '.' || ch == '-';
    })) return false;
    if (colon != std::string::npos) {
        try {
            const auto value = std::stoul(authority.substr(colon + 1));
            if (value == 0 || value > 65535) return false;
            endpoint.port = static_cast<INTERNET_PORT>(value);
        } catch (...) { return false; }
    }
    if (!endpoint.secure && !loopback_host(host)) return false;
    endpoint.host.assign(host.begin(), host.end());
    return true;
}
#endif

WeatherGeocodingResult geocoding_error(std::string code, bool offline = false) {
    WeatherGeocodingResult result;
    result.error_code = std::move(code);
    result.offline = offline;
    return result;
}

WeatherFetchResult forecast_error(std::string code, bool offline = false) {
    WeatherFetchResult result;
    result.error_code = std::move(code);
    result.offline = offline;
    return result;
}

}  // namespace

WeatherGeocodingResult parse_open_meteo_geocoding(const std::string& json) {
    if (json.empty() || json.size() > kMaximumResponseBytes) return geocoding_error("INVALID_GEOCODING_RESPONSE");
    JsonValue root;
    JsonParser parser(json);
    if (!parser.parse(root)) return geocoding_error("INVALID_GEOCODING_RESPONSE");
    const auto* results = object_member(&root, "results", JsonValue::Type::Array);
    if (!results) return geocoding_error("CITY_NOT_FOUND");
    WeatherGeocodingResult response;
    for (const auto& item : results->array) {
        if (item.type != JsonValue::Type::Object) continue;
        WeatherLocation location;
        location.name = string_member(&item, "name");
        location.region = string_member(&item, "admin1");
        location.country = string_member(&item, "country");
        location.country_code = string_member(&item, "country_code");
        location.timezone = string_member(&item, "timezone");
        if (!number_member(&item, "latitude", location.latitude) ||
            !number_member(&item, "longitude", location.longitude) || !location.valid()) continue;
        response.locations.push_back(std::move(location));
        if (response.locations.size() == kWeatherSuggestionCapacity) break;
    }
    if (response.locations.empty()) return geocoding_error("CITY_NOT_FOUND");
    response.ok = true;
    return response;
}

WeatherFetchResult parse_open_meteo_forecast(const std::string& json,
                                             const WeatherLocation& location) {
    if (!location.valid() || json.empty() || json.size() > kMaximumResponseBytes) {
        return forecast_error("INVALID_WEATHER_RESPONSE");
    }
    JsonValue root;
    JsonParser parser(json);
    if (!parser.parse(root)) return forecast_error("INVALID_WEATHER_RESPONSE");
    const auto* current = object_member(&root, "current", JsonValue::Type::Object);
    const auto* hourly = object_member(&root, "hourly", JsonValue::Type::Object);
    const auto* daily = object_member(&root, "daily", JsonValue::Type::Object);
    if (!current || !hourly || !daily) return forecast_error("INVALID_WEATHER_RESPONSE");

    WeatherData data;
    data.location = location;
    double temperature = 0.0;
    double apparent = 0.0;
    double humidity = 0.0;
    double wind = 0.0;
    double code = 0.0;
    double is_day = 1.0;
    if (!number_member(current, "temperature_2m", temperature) ||
        !number_member(current, "apparent_temperature", apparent) ||
        !number_member(current, "relative_humidity_2m", humidity) ||
        !number_member(current, "wind_speed_10m", wind) ||
        !number_member(current, "weather_code", code)) {
        return forecast_error("INVALID_WEATHER_RESPONSE");
    }
    number_member(current, "is_day", is_day);
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
    data.observed_at = string_member(current, "time");
    const auto read_metric = [&](const char* key, WeatherMetric metric, float& output,
                                 float scale = 1.0F) {
        double value = 0.0;
        if (!number_member(current, key, value) || !std::isfinite(value)) return;
        output = static_cast<float>(value) * scale;
        data.metrics.set(metric);
    };
    read_metric("pressure_msl", WeatherMetric::Pressure, data.metrics.pressure_hpa);
    read_metric("wind_gusts_10m", WeatherMetric::WindGust, data.metrics.wind_gust_kmh);
    read_metric("dew_point_2m", WeatherMetric::DewPoint, data.metrics.dew_point_c);
    read_metric("visibility", WeatherMetric::Visibility, data.metrics.visibility_km, 0.001F);
    read_metric("uv_index", WeatherMetric::UvIndex, data.metrics.uv_index);
    read_metric("cloud_cover", WeatherMetric::CloudCover, data.metrics.cloud_cover_percent);
    read_metric("precipitation", WeatherMetric::Precipitation, data.metrics.precipitation_mm);

    const auto* daily_time = object_member(daily, "time", JsonValue::Type::Array);
    const auto* daily_min = object_member(daily, "temperature_2m_min", JsonValue::Type::Array);
    const auto* daily_max = object_member(daily, "temperature_2m_max", JsonValue::Type::Array);
    const auto* daily_code = object_member(daily, "weather_code", JsonValue::Type::Array);
    const auto* daily_rain = object_member(daily, "precipitation_probability_max", JsonValue::Type::Array);
    if (!daily_time || !daily_min || !daily_max || !daily_code || daily_time->array.empty()) {
        return forecast_error("INVALID_WEATHER_RESPONSE");
    }
    const std::size_t daily_count = std::min({daily_time->array.size(), daily_min->array.size(),
                                              daily_max->array.size(), daily_code->array.size(),
                                              kWeatherDailyCapacity});
    for (std::size_t index = 0; index < daily_count; ++index) {
        const auto date = array_string(daily_time, index);
        const auto minimum = array_number(daily_min, index, std::numeric_limits<double>::quiet_NaN());
        const auto maximum = array_number(daily_max, index, std::numeric_limits<double>::quiet_NaN());
        const auto day_code = array_number(daily_code, index, -1.0);
        if (date.empty() || !std::isfinite(minimum) || !std::isfinite(maximum)) continue;
        const int normalized_code = static_cast<int>(std::lround(day_code));
        const int rain_probability = daily_rain ? static_cast<int>(std::lround(
            array_number(daily_rain, index, 0.0))) : 0;
        data.daily[data.daily_count++] = {date, static_cast<float>(minimum), static_cast<float>(maximum),
                                           weather_condition_from_wmo(normalized_code), normalized_code,
                                           std::clamp(rain_probability, 0, 100)};
    }
    if (data.daily_count == 0) return forecast_error("INVALID_WEATHER_RESPONSE");
    data.daily_minimum_c = data.daily[0].minimum_c;
    data.daily_maximum_c = data.daily[0].maximum_c;

    const auto* hourly_time = object_member(hourly, "time", JsonValue::Type::Array);
    const auto* hourly_temp = object_member(hourly, "temperature_2m", JsonValue::Type::Array);
    const auto* hourly_code = object_member(hourly, "weather_code", JsonValue::Type::Array);
    const auto* hourly_rain = object_member(hourly, "precipitation_probability", JsonValue::Type::Array);
    if (!hourly_time || !hourly_temp || !hourly_code) return forecast_error("INVALID_WEATHER_RESPONSE");
    const std::size_t available = std::min({hourly_time->array.size(), hourly_temp->array.size(), hourly_code->array.size()});
    std::size_t start = 0;
    while (start + 1 < available && array_string(hourly_time, start) < data.observed_at) ++start;
    for (std::size_t index = start; index < available && data.hourly_count < kWeatherHourlyCapacity; index += 3) {
        const auto time = array_string(hourly_time, index);
        const auto value = array_number(hourly_temp, index, std::numeric_limits<double>::quiet_NaN());
        const auto hour_code = array_number(hourly_code, index, -1.0);
        if (time.empty() || !std::isfinite(value)) continue;
        const int normalized_code = static_cast<int>(std::lround(hour_code));
        const int rain_probability = hourly_rain ? static_cast<int>(std::lround(
            array_number(hourly_rain, index, 0.0))) : 0;
        data.hourly[data.hourly_count++] = {time, static_cast<float>(value),
                                             weather_condition_from_wmo(normalized_code), normalized_code,
                                             std::clamp(rain_probability, 0, 100)};
    }
    if (hourly_rain && start < hourly_rain->array.size()) {
        data.rain_probability_percent = std::clamp(static_cast<int>(std::lround(
            array_number(hourly_rain, start, 0.0))), 0, 100);
    }
    data.origin = WeatherDataOrigin::Live;
    if (!data.valid()) return forecast_error("INVALID_WEATHER_DATA");
    WeatherFetchResult response;
    response.ok = true;
    response.data = std::move(data);
    return response;
}

WeatherFetchResult parse_tlesdetector_weather(const std::string& json,
                                              const WeatherLocation& location) {
    if (!location.valid() || json.empty() || json.size() > kMaximumResponseBytes) {
        return forecast_error("INVALID_TLESDETECTOR_RESPONSE");
    }
    JsonValue root;
    JsonParser parser(json);
    if (!parser.parse(root)) return forecast_error("INVALID_TLESDETECTOR_RESPONSE");
    if (!available_status(&root)) return forecast_error("TLESDETECTOR_WEATHER_UNAVAILABLE");
    const auto* current = object_member(&root, "current", JsonValue::Type::Object);
    const auto* hours = object_member(&root, "hours", JsonValue::Type::Array);
    const auto* days = object_member(&root, "days", JsonValue::Type::Array);
    if (!current || !hours || !days) return forecast_error("INVALID_TLESDETECTOR_RESPONSE");

    WeatherData data;
    data.location = location;
    data.provider = WeatherProvider::TlesDetector;
    data.origin = WeatherDataOrigin::Live;
    double temperature = 0.0;
    double apparent = 0.0;
    double humidity = 0.0;
    double wind = 0.0;
    double code = 0.0;
    if (!number_member(current, "temperatureC", temperature) ||
        !number_member(current, "apparentTemperatureC", apparent) ||
        !number_member(current, "relativeHumidity", humidity) ||
        !number_member(current, "windSpeedKmh", wind) ||
        !number_member(current, "weatherCode", code)) {
        return forecast_error("INVALID_TLESDETECTOR_RESPONSE");
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
    bool is_day = true;
    boolean_member(current, "isDay", is_day);
    data.is_day = is_day;
    data.observed_at = string_member(current, "time");

    const auto read_metric = [&](const char* key, WeatherMetric metric, float& output,
                                 float scale = 1.0F) {
        double value = 0.0;
        if (!number_member(current, key, value) || !std::isfinite(value)) return;
        output = static_cast<float>(value) * scale;
        data.metrics.set(metric);
    };
    read_metric("pressureHpa", WeatherMetric::Pressure, data.metrics.pressure_hpa);
    read_metric("windGustsKmh", WeatherMetric::WindGust, data.metrics.wind_gust_kmh);
    read_metric("dewPointC", WeatherMetric::DewPoint, data.metrics.dew_point_c);
    read_metric("visibilityM", WeatherMetric::Visibility, data.metrics.visibility_km, 0.001F);
    read_metric("uvIndex", WeatherMetric::UvIndex, data.metrics.uv_index);
    read_metric("cloudCover", WeatherMetric::CloudCover, data.metrics.cloud_cover_percent);
    read_metric("precipitationMm", WeatherMetric::Precipitation, data.metrics.precipitation_mm);

    const auto* air = object_member(&root, "airQuality", JsonValue::Type::Object);
    const auto* air_current = object_member(air, "current", JsonValue::Type::Object);
    double aqi = 0.0;
    if (available_status(air) && number_member(air_current, "europeanAqi", aqi)) {
        data.metrics.european_aqi = static_cast<int>(std::lround(aqi));
        data.metrics.set(WeatherMetric::AirQuality);
    }

    for (std::size_t index = 0; index < hours->array.size() &&
         data.hourly_count < kWeatherHourlyCapacity; index += 3) {
        const auto& item = hours->array[index];
        if (item.type != JsonValue::Type::Object) continue;
        double value = 0.0;
        double hour_code = -1.0;
        double rain = 0.0;
        const auto time = string_member(&item, "time");
        if (time.empty() || !number_member(&item, "temperatureC", value) ||
            !number_member(&item, "weatherCode", hour_code)) continue;
        number_member(&item, "precipitationProbability", rain);
        const int normalized_code = static_cast<int>(std::lround(hour_code));
        data.hourly[data.hourly_count++] = {
            time, static_cast<float>(value), weather_condition_from_wmo(normalized_code),
            normalized_code, std::clamp(static_cast<int>(std::lround(rain)), 0, 100),
        };
    }
    if (data.hourly_count > 0) {
        data.rain_probability_percent = data.hourly[0].rain_probability_percent;
    }

    for (const auto& item : days->array) {
        if (item.type != JsonValue::Type::Object || data.daily_count == kWeatherDailyCapacity) break;
        double minimum = 0.0;
        double maximum = 0.0;
        double day_code = -1.0;
        double rain = 0.0;
        const auto date = string_member(&item, "date");
        if (date.empty() || !number_member(&item, "temperatureMinC", minimum) ||
            !number_member(&item, "temperatureMaxC", maximum) ||
            !number_member(&item, "weatherCode", day_code)) continue;
        number_member(&item, "precipitationProbability", rain);
        const int normalized_code = static_cast<int>(std::lround(day_code));
        data.daily[data.daily_count++] = {
            date, static_cast<float>(minimum), static_cast<float>(maximum),
            weather_condition_from_wmo(normalized_code), normalized_code,
            std::clamp(static_cast<int>(std::lround(rain)), 0, 100),
        };
    }
    if (data.daily_count == 0) return forecast_error("INVALID_TLESDETECTOR_RESPONSE");
    data.daily_minimum_c = data.daily[0].minimum_c;
    data.daily_maximum_c = data.daily[0].maximum_c;
    data.stale = string_member(&root, "status") == "stale";
    if (!data.valid()) return forecast_error("INVALID_TLESDETECTOR_DATA");
    WeatherFetchResult response;
    response.ok = true;
    response.data = std::move(data);
    return response;
}

bool apply_tlesdetector_opportunities(const std::string& json, WeatherData& data) {
    if (!data.valid() || json.empty() || json.size() > kMaximumResponseBytes) return false;
    JsonValue root;
    JsonParser parser(json);
    if (!parser.parse(root) || root.type != JsonValue::Type::Object) return false;

    WeatherHazards hazards;
    const auto* alerts = object_member(&root, "weatherAlerts", JsonValue::Type::Object);
    if (alerts && available_status(alerts)) {
        double active = 0.0;
        double nearby = 0.0;
        if (!number_member(alerts, "totalCount", active) ||
            !number_member(alerts, "nearbyCount", nearby)) return false;
        hazards.alerts_available = true;
        hazards.active_alert_count = std::clamp(static_cast<int>(std::lround(active)), 0, 1000);
        hazards.nearby_alert_count = std::clamp(static_cast<int>(std::lround(nearby)),
                                                0, hazards.active_alert_count);
        const auto* items = object_member(alerts, "alerts", JsonValue::Type::Array);
        int highest = static_cast<int>(WeatherAlertLevel::None);
        if (items) {
            for (const auto& item : items->array) {
                std::string severity = string_member(&item, "severity");
                std::transform(severity.begin(), severity.end(), severity.begin(),
                               [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                const auto level = severity == "minor" ? WeatherAlertLevel::Minor :
                    severity == "moderate" ? WeatherAlertLevel::Moderate :
                    severity == "severe" ? WeatherAlertLevel::Severe :
                    severity == "extreme" ? WeatherAlertLevel::Extreme : WeatherAlertLevel::Unknown;
                if (level != WeatherAlertLevel::Unknown) highest = std::max(highest, static_cast<int>(level));
            }
        }
        hazards.highest_alert = static_cast<WeatherAlertLevel>(highest);
    }

    const auto* lightning = object_member(&root, "lightning", JsonValue::Type::Object);
    if (lightning && available_status(lightning)) {
        hazards.lightning_available = true;
        const auto* candidates = object_member(&root, "candidates", JsonValue::Type::Array);
        const JsonValue* nearest = nullptr;
        double nearest_distance = std::numeric_limits<double>::infinity();
        if (candidates) {
            for (const auto& item : candidates->array) {
                if (item.type != JsonValue::Type::Object || string_member(&item, "source") != "observed") continue;
                double distance = 0.0;
                if (!number_member(&item, "distanceKm", distance) || distance < 0.0 || distance > 2000.0) continue;
                if (distance < nearest_distance && object_member(&item, "observedLightning", JsonValue::Type::Object)) {
                    nearest_distance = distance;
                    nearest = &item;
                }
            }
        }
        if (nearest) {
            const auto* observed = object_member(nearest, "observedLightning", JsonValue::Type::Object);
            double bearing = 0.0;
            double age = 0.0;
            double flashes = 0.0;
            if (number_member(nearest, "bearingDeg", bearing) &&
                number_member(observed, "ageMinutes", age) &&
                number_member(observed, "maxFlashesPer5Min", flashes)) {
                hazards.lightning_observed = true;
                hazards.nearest_lightning_km = static_cast<float>(nearest_distance);
                hazards.lightning_bearing_deg = std::clamp(static_cast<int>(std::lround(bearing)), 0, 360);
                hazards.lightning_age_minutes = std::clamp(static_cast<int>(std::lround(age)), 0, 1440);
                hazards.max_flashes_per_5_min = std::clamp(static_cast<int>(std::lround(flashes)), 0, 100000);
            }
            const auto* cg = object_member(observed, "cg", JsonValue::Type::Object);
            double positive = 0.0;
            if (cg && number_member(cg, "positiveCount", positive)) {
                hazards.xweather_available = true;
                hazards.positive_cg_count = std::clamp(static_cast<int>(std::lround(positive)), 0, 1000000);
                double peak = 0.0;
                if (number_member(cg, "maxPositiveKa", peak)) {
                    hazards.max_positive_ka = std::clamp(static_cast<int>(std::lround(std::abs(peak))), 0, 10000);
                }
            }
        }
    }

    const auto previous = data.hazards;
    data.hazards = hazards;
    if (!data.valid()) {
        data.hazards = previous;
        return false;
    }
    return true;
}

std::future<WeatherGeocodingResult> OpenMeteoWeatherService::search(std::string query) {
    const auto generation = ++generation_;
    return std::async(std::launch::async, [this, generation, query = std::move(query)] {
#ifdef _WIN32
        if (query.size() < 2 || query.size() > 96) return geocoding_error("INVALID_CITY_QUERY");
        const std::string path = "/v1/search?name=" + url_encode(query) + "&count=5&language=el&format=json";
        const auto http = https_get(L"geocoding-api.open-meteo.com", path);
        if (generation_.load() != generation) return geocoding_error("CANCELLED");
        if (http.transport_error || http.status == 0) return geocoding_error("NETWORK_UNAVAILABLE", true);
        if (http.status != 200) return geocoding_error("GEOCODING_HTTP_" + std::to_string(http.status));
        return parse_open_meteo_geocoding(http.body);
#else
        (void)query;
        return geocoding_error("PLATFORM_NOT_SUPPORTED", true);
#endif
    });
}

std::future<WeatherFetchResult> OpenMeteoWeatherService::fetch(WeatherLocation location) {
    const auto generation = ++generation_;
    return std::async(std::launch::async, [this, generation, location = std::move(location)] {
#ifdef _WIN32
        if (!location.valid()) return forecast_error("INVALID_LOCATION");
        TlesDetectorEndpoint tlesdetector;
        if (parse_tlesdetector_endpoint(tlesdetector)) {
            std::ostringstream weather_path;
            weather_path << tlesdetector.path_prefix << "/api/weather?lat=" << std::fixed
                         << std::setprecision(6) << location.latitude << "&lon=" << location.longitude;
            const auto weather_http = http_get(tlesdetector.host.c_str(), tlesdetector.port,
                                               tlesdetector.secure, weather_path.str(), {}, 28000);
            if (generation_.load() != generation) return forecast_error("CANCELLED");
            if (!weather_http.transport_error && weather_http.status == 200) {
                auto enhanced = parse_tlesdetector_weather(weather_http.body, location);
                if (enhanced.ok) {
                    std::ostringstream opportunities_path;
                    opportunities_path << tlesdetector.path_prefix << "/api/opportunities?lat="
                                       << std::fixed << std::setprecision(6) << location.latitude
                                       << "&lon=" << location.longitude;
                    const auto opportunities_http = http_get(
                        tlesdetector.host.c_str(), tlesdetector.port, tlesdetector.secure,
                        opportunities_path.str(), {}, 28000);
                    if (generation_.load() != generation) return forecast_error("CANCELLED");
                    if (!opportunities_http.transport_error && opportunities_http.status == 200) {
                        apply_tlesdetector_opportunities(opportunities_http.body, enhanced.data);
                    }
                    return enhanced;
                }
            }
        }
        std::ostringstream path;
        path << std::fixed << std::setprecision(6)
             << "/v1/forecast?latitude=" << location.latitude
             << "&longitude=" << location.longitude
             << "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,wind_speed_10m,wind_gusts_10m,dew_point_2m,pressure_msl,visibility,uv_index,cloud_cover,precipitation"
             << "&hourly=temperature_2m,weather_code,precipitation_probability"
             << "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
             << "&timezone=auto&forecast_days=5";
        const auto http = https_get(L"api.open-meteo.com", path.str());
        if (generation_.load() != generation) return forecast_error("CANCELLED");
        if (http.transport_error || http.status == 0) return forecast_error("NETWORK_UNAVAILABLE", true);
        if (http.status != 200) return forecast_error("WEATHER_HTTP_" + std::to_string(http.status));
        return parse_open_meteo_forecast(http.body, location);
#else
        (void)location;
        return forecast_error("PLATFORM_NOT_SUPPORTED", true);
#endif
    });
}

void OpenMeteoWeatherService::cancel() noexcept {
    ++generation_;
}

}  // namespace aura::simulator

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aura {

constexpr std::size_t kWeatherHourlyCapacity = 4;
constexpr std::size_t kWeatherDailyCapacity = 4;
constexpr std::size_t kWeatherSuggestionCapacity = 5;

enum class WeatherCondition {
    Clear,
    PartlyCloudy,
    Cloudy,
    Fog,
    Drizzle,
    Rain,
    Snow,
    Thunderstorm,
    Windy,
    Unknown,
};

enum class WeatherState {
    Unconfigured,
    Loading,
    Ready,
    Searching,
    SearchResults,
    Offline,
    Error,
};

enum class WeatherDataOrigin {
    Live,
    Cache,
    Demo,
};

enum class WeatherProvider {
    OpenMeteo,
    TlesDetector,
};

// A deliberately small, validated set of polling choices. Keeping this in the
// shared model makes the simulator and the device use exactly the same policy.
enum class WeatherRefreshInterval : std::uint16_t {
    Minutes15 = 15,
    Minutes30 = 30,
    Hour1 = 60,
    Hours3 = 180,
};

constexpr WeatherRefreshInterval kDefaultWeatherRefreshInterval = WeatherRefreshInterval::Minutes30;

bool weather_refresh_interval_valid(WeatherRefreshInterval interval) noexcept;
std::uint32_t weather_refresh_interval_minutes(WeatherRefreshInterval interval) noexcept;

enum class WeatherMetric : std::uint16_t {
    Pressure = 1U << 0U,
    WindGust = 1U << 1U,
    DewPoint = 1U << 2U,
    Visibility = 1U << 3U,
    UvIndex = 1U << 4U,
    CloudCover = 1U << 5U,
    Precipitation = 1U << 6U,
    AirQuality = 1U << 7U,
};

struct WeatherMetrics {
    std::uint16_t available{0};
    float pressure_hpa{0.0F};
    float wind_gust_kmh{0.0F};
    float dew_point_c{0.0F};
    float visibility_km{0.0F};
    float uv_index{0.0F};
    float cloud_cover_percent{0.0F};
    float precipitation_mm{0.0F};
    int european_aqi{0};

    bool has(WeatherMetric metric) const noexcept {
        return (available & static_cast<std::uint16_t>(metric)) != 0;
    }
    void set(WeatherMetric metric) noexcept {
        available |= static_cast<std::uint16_t>(metric);
    }
};

enum class WeatherAlertLevel {
    None,
    Minor,
    Moderate,
    Severe,
    Extreme,
    Unknown,
};

struct WeatherHazards {
    bool alerts_available{false};
    int active_alert_count{0};
    int nearby_alert_count{0};
    WeatherAlertLevel highest_alert{WeatherAlertLevel::None};
    bool lightning_available{false};
    bool lightning_observed{false};
    float nearest_lightning_km{0.0F};
    int lightning_bearing_deg{0};
    int lightning_age_minutes{0};
    int max_flashes_per_5_min{0};
    bool xweather_available{false};
    int positive_cg_count{0};
    int max_positive_ka{0};
};

struct WeatherLocation {
    std::string name;
    std::string region;
    std::string country;
    std::string country_code;
    std::string timezone;
    double latitude{0.0};
    double longitude{0.0};

    bool valid() const noexcept;
};

struct HourlyWeather {
    std::string time;
    float temperature_c{0.0F};
    WeatherCondition condition{WeatherCondition::Unknown};
    int weather_code{-1};
    int rain_probability_percent{0};
    bool is_day{true};
};

struct DailyWeather {
    std::string date;
    float minimum_c{0.0F};
    float maximum_c{0.0F};
    WeatherCondition condition{WeatherCondition::Unknown};
    int weather_code{-1};
    int rain_probability_max_percent{0};
};

struct WeatherData {
    WeatherLocation location;
    float temperature_c{0.0F};
    float apparent_temperature_c{0.0F};
    float wind_kmh{0.0F};
    int humidity_percent{0};
    float daily_minimum_c{0.0F};
    float daily_maximum_c{0.0F};
    WeatherCondition condition{WeatherCondition::Unknown};
    bool is_day{true};
    std::string observed_at;
    std::array<HourlyWeather, kWeatherHourlyCapacity> hourly{};
    std::size_t hourly_count{0};
    std::array<DailyWeather, kWeatherDailyCapacity> daily{};
    std::size_t daily_count{0};
    WeatherDataOrigin origin{WeatherDataOrigin::Live};
    WeatherProvider provider{WeatherProvider::OpenMeteo};
    bool stale{false};
    int weather_code{-1};
    int rain_probability_percent{0};
    WeatherMetrics metrics;
    WeatherHazards hazards;

    bool valid() const noexcept;
};

struct WeatherSnapshot {
    WeatherState state{WeatherState::Unconfigured};
    WeatherLocation saved_location;
    bool has_saved_location{false};
    WeatherData data;
    bool has_data{false};
    std::array<WeatherLocation, kWeatherSuggestionCapacity> suggestions{};
    std::size_t suggestion_count{0};
    std::string search_query;
    std::string error_code;
    std::string notice_code;
    WeatherRefreshInterval refresh_interval{kDefaultWeatherRefreshInterval};
    std::uint64_t sequence{0};
};

class WeatherModel {
public:
    const WeatherSnapshot& snapshot() const noexcept { return snapshot_; }

    bool restore_location(const WeatherLocation& location);
    bool restore_cache(const WeatherData& data);
    bool set_refresh_interval(WeatherRefreshInterval interval);
    void clear_location();
    bool begin_fetch(const WeatherLocation& location, bool save_as_default);
    bool fetch_succeeded(const WeatherData& data);
    void fetch_failed(std::string error_code, bool offline);
    bool begin_search(std::string query);
    void search_succeeded(const std::vector<WeatherLocation>& locations);
    void search_failed(std::string error_code, bool offline);

private:
    WeatherSnapshot snapshot_{};
};

struct WeatherGeocodingResult {
    bool ok{false};
    bool offline{false};
    std::vector<WeatherLocation> locations;
    std::string error_code;
};

struct WeatherFetchResult {
    bool ok{false};
    bool offline{false};
    WeatherData data;
    std::string error_code;
};

enum class WeatherIntentKind {
    None,
    ShowSaved,
    QueryCity,
    ChangeCity,
};

struct WeatherIntent {
    WeatherIntentKind kind{WeatherIntentKind::None};
    std::string city_query;
};

WeatherCondition weather_condition_from_wmo(int code) noexcept;
// A shown temperature in whole degrees: never "-0" (-0.4 C reads 0).
int whole_degrees(float celsius) noexcept;
WeatherIntent analyze_weather_intent(std::string_view text);
const char* weather_state_name(WeatherState state) noexcept;

}  // namespace aura

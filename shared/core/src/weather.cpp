#include "aura/weather.hpp"

#include "aura/text_util.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace aura {

namespace {

constexpr std::size_t kNameLimit = 96;
constexpr std::size_t kMetadataLimit = 128;
constexpr std::size_t kQueryLimit = 96;

bool printable_utf8_text(const std::string& value, std::size_t limit, bool allow_empty = false) {
    if ((!allow_empty && value.empty()) || value.size() > limit) return false;
    for (const unsigned char ch : value) {
        if (ch < 0x20 && ch != '\t') return false;
        if (ch == 0x7F) return false;
    }
    return true;
}

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    while (!value.empty() && (value.back() == '?' || value.back() == '!' || value.back() == '.' ||
                              value.back() == ',' || value.back() == ';' || value.back() == ':')) {
        value.pop_back();
    }
    return value;
}

std::string suffix_after(std::string_view value, const std::initializer_list<std::string_view>& markers) {
    for (const auto marker : markers) {
        const auto position = value.find(marker);
        if (position == std::string_view::npos) continue;
        auto result = trim(std::string(value.substr(position + marker.size())));
        if (result.size() <= kQueryLimit) return result;
    }
    return {};
}

}  // namespace

bool weather_refresh_interval_valid(WeatherRefreshInterval interval) noexcept {
    switch (interval) {
        case WeatherRefreshInterval::Minutes15:
        case WeatherRefreshInterval::Minutes30:
        case WeatherRefreshInterval::Hour1:
        case WeatherRefreshInterval::Hours3: return true;
    }
    return false;
}

std::uint32_t weather_refresh_interval_minutes(WeatherRefreshInterval interval) noexcept {
    return weather_refresh_interval_valid(interval) ? static_cast<std::uint32_t>(interval) :
                                                      static_cast<std::uint32_t>(kDefaultWeatherRefreshInterval);
}

bool WeatherLocation::valid() const noexcept {
    return printable_utf8_text(name, kNameLimit) && printable_utf8_text(region, kMetadataLimit, true) &&
           printable_utf8_text(country, kMetadataLimit, true) && country_code.size() <= 3 &&
           printable_utf8_text(timezone, kMetadataLimit, true) && std::isfinite(latitude) &&
           std::isfinite(longitude) && latitude >= -90.0 && latitude <= 90.0 &&
           longitude >= -180.0 && longitude <= 180.0;
}

bool WeatherData::valid() const noexcept {
    constexpr std::uint16_t kKnownMetricMask =
        static_cast<std::uint16_t>(WeatherMetric::Pressure) |
        static_cast<std::uint16_t>(WeatherMetric::WindGust) |
        static_cast<std::uint16_t>(WeatherMetric::DewPoint) |
        static_cast<std::uint16_t>(WeatherMetric::Visibility) |
        static_cast<std::uint16_t>(WeatherMetric::UvIndex) |
        static_cast<std::uint16_t>(WeatherMetric::CloudCover) |
        static_cast<std::uint16_t>(WeatherMetric::Precipitation) |
        static_cast<std::uint16_t>(WeatherMetric::AirQuality);
    if (!location.valid() || !std::isfinite(temperature_c) || !std::isfinite(apparent_temperature_c) ||
        !std::isfinite(wind_kmh) || !std::isfinite(daily_minimum_c) || !std::isfinite(daily_maximum_c) ||
        humidity_percent < 0 || humidity_percent > 100 || hourly_count > hourly.size() ||
        daily_count > daily.size() || observed_at.size() > 32 || weather_code < -1 ||
        weather_code > 99 || rain_probability_percent < 0 || rain_probability_percent > 100 ||
        static_cast<int>(provider) < static_cast<int>(WeatherProvider::OpenMeteo) ||
        static_cast<int>(provider) > static_cast<int>(WeatherProvider::TlesDetector) ||
        (metrics.available & ~kKnownMetricMask) != 0) {
        return false;
    }
    if (temperature_c < -100.0F || temperature_c > 80.0F ||
        apparent_temperature_c < -120.0F || apparent_temperature_c > 90.0F ||
        wind_kmh < 0.0F || wind_kmh > 500.0F) {
        return false;
    }
    const auto metric_valid = [&](WeatherMetric metric, float value, float minimum, float maximum) {
        return !metrics.has(metric) || (std::isfinite(value) && value >= minimum && value <= maximum);
    };
    if (!metric_valid(WeatherMetric::Pressure, metrics.pressure_hpa, 800.0F, 1150.0F) ||
        !metric_valid(WeatherMetric::WindGust, metrics.wind_gust_kmh, 0.0F, 500.0F) ||
        !metric_valid(WeatherMetric::DewPoint, metrics.dew_point_c, -120.0F, 80.0F) ||
        !metric_valid(WeatherMetric::Visibility, metrics.visibility_km, 0.0F, 500.0F) ||
        !metric_valid(WeatherMetric::UvIndex, metrics.uv_index, 0.0F, 30.0F) ||
        !metric_valid(WeatherMetric::CloudCover, metrics.cloud_cover_percent, 0.0F, 100.0F) ||
        !metric_valid(WeatherMetric::Precipitation, metrics.precipitation_mm, 0.0F, 1000.0F) ||
        (metrics.has(WeatherMetric::AirQuality) &&
         (metrics.european_aqi < 0 || metrics.european_aqi > 1000))) return false;
    if (hazards.active_alert_count < 0 || hazards.active_alert_count > 1000 ||
        hazards.nearby_alert_count < 0 || hazards.nearby_alert_count > hazards.active_alert_count ||
        static_cast<int>(hazards.highest_alert) < static_cast<int>(WeatherAlertLevel::None) ||
        static_cast<int>(hazards.highest_alert) > static_cast<int>(WeatherAlertLevel::Unknown) ||
        (hazards.lightning_observed &&
         (!hazards.lightning_available || !std::isfinite(hazards.nearest_lightning_km) ||
          hazards.nearest_lightning_km < 0.0F || hazards.nearest_lightning_km > 2000.0F ||
          hazards.lightning_bearing_deg < 0 || hazards.lightning_bearing_deg > 360 ||
          hazards.lightning_age_minutes < 0 || hazards.lightning_age_minutes > 1440 ||
          hazards.max_flashes_per_5_min < 0 || hazards.max_flashes_per_5_min > 100000)) ||
        (hazards.xweather_available &&
         (hazards.positive_cg_count < 0 || hazards.positive_cg_count > 1000000 ||
          hazards.max_positive_ka < 0 || hazards.max_positive_ka > 10000))) return false;
    for (std::size_t index = 0; index < hourly_count; ++index) {
        if (hourly[index].time.size() > 32 || !std::isfinite(hourly[index].temperature_c) ||
            hourly[index].weather_code < -1 || hourly[index].weather_code > 99 ||
            hourly[index].rain_probability_percent < 0 || hourly[index].rain_probability_percent > 100) return false;
    }
    for (std::size_t index = 0; index < daily_count; ++index) {
        if (daily[index].date.size() > 32 || !std::isfinite(daily[index].minimum_c) ||
            !std::isfinite(daily[index].maximum_c) || daily[index].weather_code < -1 ||
            daily[index].weather_code > 99 || daily[index].rain_probability_max_percent < 0 ||
            daily[index].rain_probability_max_percent > 100) return false;
    }
    return true;
}

bool WeatherModel::restore_location(const WeatherLocation& location) {
    if (!location.valid()) return false;
    if (snapshot_.has_data &&
        (snapshot_.data.location.name != location.name ||
         std::fabs(snapshot_.data.location.latitude - location.latitude) > 0.0001 ||
         std::fabs(snapshot_.data.location.longitude - location.longitude) > 0.0001)) {
        snapshot_.has_data = false;
        snapshot_.data = {};
    }
    snapshot_.saved_location = location;
    snapshot_.has_saved_location = true;
    if (!snapshot_.has_data) snapshot_.state = WeatherState::Loading;
    ++snapshot_.sequence;
    return true;
}

bool WeatherModel::restore_cache(const WeatherData& data) {
    if (!data.valid()) return false;
    snapshot_.data = data;
    snapshot_.data.origin = WeatherDataOrigin::Cache;
    snapshot_.data.stale = true;
    snapshot_.has_data = true;
    snapshot_.saved_location = data.location;
    snapshot_.has_saved_location = true;
    snapshot_.state = WeatherState::Ready;
    snapshot_.notice_code = "CACHED";
    snapshot_.error_code.clear();
    ++snapshot_.sequence;
    return true;
}

bool WeatherModel::set_refresh_interval(WeatherRefreshInterval interval) {
    if (!weather_refresh_interval_valid(interval)) return false;
    if (snapshot_.refresh_interval == interval) return true;
    snapshot_.refresh_interval = interval;
    ++snapshot_.sequence;
    return true;
}

void WeatherModel::clear_location() {
    // Keep the sequence monotonic: the UI skips sequences it has applied.
    const auto sequence = snapshot_.sequence;
    const auto refresh_interval = snapshot_.refresh_interval;
    snapshot_ = {};
    snapshot_.state = WeatherState::Unconfigured;
    snapshot_.refresh_interval = refresh_interval;
    snapshot_.sequence = sequence + 1;
}

bool WeatherModel::begin_fetch(const WeatherLocation& location, bool save_as_default) {
    if (!location.valid()) return false;
    if (save_as_default) {
        snapshot_.saved_location = location;
        snapshot_.has_saved_location = true;
    }
    if (!snapshot_.has_data || snapshot_.data.location.name != location.name ||
        std::fabs(snapshot_.data.location.latitude - location.latitude) > 0.0001 ||
        std::fabs(snapshot_.data.location.longitude - location.longitude) > 0.0001) {
        snapshot_.has_data = false;
    }
    snapshot_.state = WeatherState::Loading;
    snapshot_.error_code.clear();
    snapshot_.notice_code.clear();
    snapshot_.suggestion_count = 0;
    ++snapshot_.sequence;
    return true;
}

bool WeatherModel::fetch_succeeded(const WeatherData& data) {
    if (!data.valid()) {
        fetch_failed("INVALID_WEATHER_DATA", false);
        return false;
    }
    snapshot_.data = data;
    snapshot_.data.origin = data.origin;
    snapshot_.data.stale = false;
    snapshot_.has_data = true;
    snapshot_.state = WeatherState::Ready;
    snapshot_.error_code.clear();
    snapshot_.notice_code.clear();
    ++snapshot_.sequence;
    return true;
}

void WeatherModel::fetch_failed(std::string error_code, bool offline) {
    snapshot_.error_code = std::move(error_code);
    if (snapshot_.has_data) {
        snapshot_.data.stale = true;
        snapshot_.state = WeatherState::Ready;
        snapshot_.notice_code = offline ? "OFFLINE_CACHE" : "STALE_CACHE";
    } else {
        snapshot_.state = offline ? WeatherState::Offline : WeatherState::Error;
    }
    ++snapshot_.sequence;
}

bool WeatherModel::begin_search(std::string query) {
    query = trim(std::move(query));
    if (!printable_utf8_text(query, kQueryLimit) || query.size() < 2) return false;
    snapshot_.state = WeatherState::Searching;
    snapshot_.search_query = std::move(query);
    snapshot_.suggestion_count = 0;
    snapshot_.error_code.clear();
    snapshot_.notice_code.clear();
    ++snapshot_.sequence;
    return true;
}

void WeatherModel::search_succeeded(const std::vector<WeatherLocation>& locations) {
    snapshot_.suggestion_count = 0;
    for (const auto& location : locations) {
        if (!location.valid()) continue;
        snapshot_.suggestions[snapshot_.suggestion_count++] = location;
        if (snapshot_.suggestion_count == snapshot_.suggestions.size()) break;
    }
    if (snapshot_.suggestion_count == 0) {
        snapshot_.state = WeatherState::Error;
        snapshot_.error_code = "CITY_NOT_FOUND";
    } else {
        snapshot_.state = WeatherState::SearchResults;
        snapshot_.error_code.clear();
    }
    ++snapshot_.sequence;
}

void WeatherModel::search_failed(std::string error_code, bool offline) {
    snapshot_.suggestion_count = 0;
    snapshot_.error_code = std::move(error_code);
    snapshot_.state = offline ? WeatherState::Offline : WeatherState::Error;
    ++snapshot_.sequence;
}

WeatherCondition weather_condition_from_wmo(int code) noexcept {
    if (code == 0) return WeatherCondition::Clear;
    if (code == 1 || code == 2) return WeatherCondition::PartlyCloudy;
    if (code == 3) return WeatherCondition::Cloudy;
    if (code == 45 || code == 48) return WeatherCondition::Fog;
    if ((code >= 51 && code <= 57)) return WeatherCondition::Drizzle;
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return WeatherCondition::Rain;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return WeatherCondition::Snow;
    if (code == 95 || code == 96 || code == 99) return WeatherCondition::Thunderstorm;
    return WeatherCondition::Unknown;
}

WeatherIntent analyze_weather_intent(std::string_view text) {
    const auto words = folded_words(fold_text(text));
    const bool change = folded_has_word(words, {"αλλαξε", "αλλαγη", "change"});
    auto city = suffix_after(text, {"πόλη σε ", "Πόλη σε ", "στην ", "στη ", "στο ", "σε ",
                                    "city to ", "in ", "for "});
    // Word starts, so "καιρός" counts and "ευκαιρία" does not.
    const bool forecast = folded_has_word_prefix(words, {"καιρ", "προγνωσ", "βρεξ", "βρεχ", "βροχ", "χιον"}) ||
                          folded_has_word(words, {"weather", "forecast", "rain", "raining", "snow", "snowing"});
    // A temperature is weather only with a time or a place ("έξω", "αύριο",
    // "στη Σπάρτη"); "η θερμοκρασία βρασμού του νερού" is for the assistant.
    const bool temperature =
        (folded_has_word_prefix(words, {"θερμοκρασ"}) || folded_has_word(words, {"temperature"})) &&
        (!city.empty() || folded_has_word(words, {"εξω", "σημερα", "αυριο", "μεθαυριο", "τωρα", "αποψε",
                                                   "outside", "today", "tomorrow", "now", "tonight"}));
    const bool weather = forecast || temperature ||
                         (change && folded_has_word(words, {"πολη", "city"}));
    if (!weather) return {};
    if (change) return {WeatherIntentKind::ChangeCity, std::move(city)};
    if (!city.empty()) return {WeatherIntentKind::QueryCity, std::move(city)};
    return {WeatherIntentKind::ShowSaved, {}};
}

const char* weather_state_name(WeatherState state) noexcept {
    switch (state) {
        case WeatherState::Unconfigured: return "Unconfigured";
        case WeatherState::Loading: return "Loading";
        case WeatherState::Ready: return "Ready";
        case WeatherState::Searching: return "Searching";
        case WeatherState::SearchResults: return "SearchResults";
        case WeatherState::Offline: return "Offline";
        case WeatherState::Error: return "Error";
    }
    return "Unknown";
}

}  // namespace aura

#include "aura/xweather.hpp"

#include "aura/json_reader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace aura {

namespace {

std::string place(double latitude, double longitude) {
    char text[48];
    std::snprintf(text, sizeof(text), "%.3f,%.3f", latitude, longitude);
    return text;
}

double number(const JsonValue* value, double fallback) {
    if (!value || value->type() != JsonType::Number) return fallback;
    char* end = nullptr;
    const double parsed = std::strtod(value->number_text().c_str(), &end);
    return end && *end == '\0' && std::isfinite(parsed) ? parsed : fallback;
}

// The answer's list, for a successful answer; nullptr for an error. Xweather
// says "warn_no_data" with an empty list when there is simply nothing.
const JsonValue* response_list(const JsonValue& root) {
    const auto* success = root.find("success");
    if (!success || success->type() != JsonType::Bool || !success->as_bool()) return nullptr;
    const auto* response = root.find("response");
    if (!response) return nullptr;
    return response->type() == JsonType::Array ? response : nullptr;
}

WeatherAlertLevel alert_level(std::string_view type) {
    // International alerts end in their level: MN, MD, SV, EX.
    if (type.size() >= 3 && type[type.size() - 3] == '.') {
        const auto level = type.substr(type.size() - 2);
        if (level == "MN") return WeatherAlertLevel::Minor;
        if (level == "MD") return WeatherAlertLevel::Moderate;
        if (level == "SV") return WeatherAlertLevel::Severe;
        if (level == "EX") return WeatherAlertLevel::Extreme;
    }
    return WeatherAlertLevel::Unknown;
}

int severity(WeatherAlertLevel level) {
    switch (level) {
        case WeatherAlertLevel::None: return 0;
        case WeatherAlertLevel::Unknown: return 1;
        case WeatherAlertLevel::Minor: return 2;
        case WeatherAlertLevel::Moderate: return 3;
        case WeatherAlertLevel::Severe: return 4;
        case WeatherAlertLevel::Extreme: return 5;
    }
    return 0;
}

constexpr float kRainRateMmH = 0.1F;  // below this, the radar's dust

}  // namespace

std::string xweather_minutely_path(double latitude, double longitude) {
    return "conditions/" + place(latitude, longitude) + "?filter=minutelyprecip&plimit=60";
}

std::string xweather_alerts_path(double latitude, double longitude) {
    return "alerts/" + place(latitude, longitude) + "?limit=10";
}

std::string xweather_lightning_path(double latitude, double longitude) {
    return "lightning/closest?p=" + place(latitude, longitude) +
           "&radius=50km&from=-15minutes&to=now&limit=1&sort=dt:-1";
}

std::string xweather_url(std::string_view path, std::string_view client_id, std::string_view client_secret) {
    std::string url = "https://data.api.xweather.com/";
    url += path;
    url += path.find('?') == std::string_view::npos ? '?' : '&';
    url += "client_id=";
    url += client_id;
    url += "&client_secret=";
    url += client_secret;
    return url;
}

bool parse_xweather_minutely(std::string_view json, XweatherNowcast& out) {
    out.rain_ok = false;
    JsonValue root;
    if (!parse_json(json, root)) return false;
    const auto* list = response_list(root);
    if (!list || list->items().empty()) return false;
    const auto* periods = list->items().front().find("periods");
    if (!periods || periods->type() != JsonType::Array) return false;
    int start = -1;
    int length = 0;
    float peak = 0.0F;
    const auto& items = periods->items();
    for (std::size_t index = 0; index < items.size() && index < 60; ++index) {
        const auto rate = static_cast<float>(number(items[index].find("precipRateMM"), 0.0));
        if (rate >= kRainRateMmH) {
            if (start < 0) start = static_cast<int>(index);
            if (start >= 0 && static_cast<int>(index) == start + length) ++length;
            peak = std::max(peak, rate);
        }
    }
    out.rain_ok = true;
    out.rain_in_minutes = start;
    out.rain_minutes = start < 0 ? 0 : length;
    out.rain_peak_mm_h = std::min(peak, 500.0F);
    return true;
}

bool parse_xweather_alerts(std::string_view json, XweatherNowcast& out) {
    out.alerts_ok = false;
    JsonValue root;
    if (!parse_json(json, root)) return false;
    const auto* list = response_list(root);
    if (!list) return false;
    out.alert_count = 0;
    out.highest = WeatherAlertLevel::None;
    out.alert_name.clear();
    for (const auto& alert : list->items()) {
        const auto* details = alert.find("details");
        if (!details) continue;
        const auto level = alert_level(details->string_at("type"));
        ++out.alert_count;
        if (severity(level) > severity(out.highest)) {
            out.highest = level;
            out.alert_name = details->string_at("name").substr(0, 96);
        }
    }
    out.alerts_ok = true;
    return true;
}

bool parse_xweather_lightning(std::string_view json, std::int64_t now_unix, XweatherNowcast& out) {
    out.lightning_ok = false;
    JsonValue root;
    if (!parse_json(json, root)) return false;
    const auto* list = response_list(root);
    if (!list) return false;
    out.lightning_near = false;
    if (!list->items().empty()) {
        const auto& strike = list->items().front();
        const auto* relative = strike.find("relativeTo");
        const auto* ob = strike.find("ob");
        const double km = number(relative ? relative->find("distanceKM") : nullptr, -1.0);
        const double bearing = number(relative ? relative->find("bearing") : nullptr, 0.0);
        const double when = number(ob ? ob->find("timestamp") : nullptr, 0.0);
        if (km < 0.0 || km > 2000.0 || when <= 0.0) return false;
        out.lightning_near = true;
        out.lightning_km = static_cast<float>(km);
        out.lightning_bearing_deg = std::clamp(static_cast<int>(std::lround(bearing)), 0, 360);
        out.lightning_age_minutes = std::clamp(static_cast<int>((now_unix - static_cast<std::int64_t>(when)) / 60), 0, 1440);
    }
    out.lightning_ok = true;
    return true;
}

bool xweather_rain_plausible(const WeatherData& data) noexcept {
    switch (data.condition) {
        case WeatherCondition::Drizzle:
        case WeatherCondition::Rain:
        case WeatherCondition::Thunderstorm:
        case WeatherCondition::Snow: return true;
        default: break;
    }
    for (std::size_t index = 0; index < std::min<std::size_t>(data.hourly_count, 3); ++index) {
        if (data.hourly[index].rain_probability_percent >= 20) return true;
    }
    return false;
}

bool xweather_storm_plausible(const WeatherData& data) noexcept {
    if (data.condition == WeatherCondition::Thunderstorm) return true;
    for (std::size_t index = 0; index < std::min<std::size_t>(data.hourly_count, 3); ++index) {
        if (data.hourly[index].condition == WeatherCondition::Thunderstorm) return true;
    }
    return data.hazards.lightning_observed;
}

void apply_xweather(WeatherData& data, const XweatherNowcast& now) {
    auto& hazards = data.hazards;
    hazards.xweather_available = true;
    if (now.alerts_ok) {
        hazards.alerts_available = true;
        hazards.active_alert_count = std::clamp(now.alert_count, 0, 1000);
        hazards.nearby_alert_count = hazards.active_alert_count;  // alerts for this very place
        hazards.highest_alert = now.highest;
        hazards.alert_name = now.alert_name;
    }
    if (now.lightning_ok) {
        hazards.lightning_available = true;
        hazards.lightning_observed = now.lightning_near;
        hazards.nearest_lightning_km = now.lightning_near ? now.lightning_km : 0.0F;
        hazards.lightning_age_minutes = now.lightning_near ? now.lightning_age_minutes : 0;
        hazards.lightning_bearing_deg = now.lightning_near ? now.lightning_bearing_deg : 0;
    }
    if (now.rain_ok) {
        hazards.nowcast_available = true;
        hazards.rain_in_minutes = now.rain_in_minutes;
        hazards.rain_minutes = now.rain_minutes;
        hazards.rain_peak_mm_h = now.rain_peak_mm_h;
    }
    if (hazards.lightning_observed && hazards.nearest_lightning_km <= 15.0F && hazards.lightning_age_minutes <= 15) {
        data.condition = WeatherCondition::Thunderstorm;
    } else if (hazards.nowcast_available && hazards.rain_in_minutes == 0) {
        switch (data.condition) {
            case WeatherCondition::Drizzle:
            case WeatherCondition::Rain:
            case WeatherCondition::Thunderstorm:
            case WeatherCondition::Snow: break;
            default: data.condition = hazards.rain_peak_mm_h >= 2.5F ? WeatherCondition::Rain : WeatherCondition::Drizzle;
        }
    }
}

XweatherScheduler::Due XweatherScheduler::due(const WeatherData& data, std::uint64_t now_ms) {
    Due due;
    if (!asked_) {
        asked_ = true;
        rain_asked_ = lightning_asked_ = false;
        due.alerts = true;
        alerts_ms_ = now_ms;
    } else if (now_ms - alerts_ms_ >= kAlertsEveryMs) {
        due.alerts = true;
        alerts_ms_ = now_ms;
    }
    if (xweather_rain_plausible(data) && (!rain_asked_ || now_ms - rain_ms_ >= kRainEveryMs)) {
        due.rain = true;
        rain_asked_ = true;
        rain_ms_ = now_ms;
    }
    if (xweather_storm_plausible(data) && (!lightning_asked_ || now_ms - lightning_ms_ >= kRainEveryMs)) {
        due.lightning = true;
        lightning_asked_ = true;
        lightning_ms_ = now_ms;
    }
    return due;
}

std::optional<WeatherNotice> WeatherNoticeGate::observe(const WeatherHazards& hazards, std::int64_t now_minute) {
    // The episodes end: a dry hour, no alert any more.
    if (hazards.nowcast_available && hazards.rain_in_minutes < 0) rain_announced_ = false;
    if (hazards.alerts_available && hazards.active_alert_count == 0) alert_announced_.clear();

    if (hazards.lightning_available && hazards.lightning_observed && hazards.nearest_lightning_km <= kStormKm &&
        (!storm_announced_ || now_minute - storm_minute_ >= kStormRepeatMinutes)) {
        storm_announced_ = true;
        storm_minute_ = now_minute;
        WeatherNotice notice;
        notice.kind = WeatherNotice::Kind::Storm;
        notice.km = hazards.nearest_lightning_km;
        return notice;
    }
    if (hazards.alerts_available && severity(hazards.highest_alert) >= severity(WeatherAlertLevel::Moderate) &&
        !hazards.alert_name.empty() && hazards.alert_name != alert_announced_) {
        alert_announced_ = hazards.alert_name;
        WeatherNotice notice;
        notice.kind = WeatherNotice::Kind::Alert;
        notice.level = hazards.highest_alert;
        notice.name = hazards.alert_name;
        return notice;
    }
    if (hazards.nowcast_available && hazards.rain_in_minutes >= 0 && hazards.rain_in_minutes <= kRainSoonMinutes &&
        !rain_announced_) {
        rain_announced_ = true;
        WeatherNotice notice;
        notice.kind = WeatherNotice::Kind::Rain;
        notice.minutes = hazards.rain_in_minutes;
        return notice;
    }
    return std::nullopt;
}

}  // namespace aura

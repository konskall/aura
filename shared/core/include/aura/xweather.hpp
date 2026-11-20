#pragma once

#include "aura/weather.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace aura {

// Xweather (Vaisala), with the owner's keys (2026-11-19): what Open-Meteo's
// forecast lacks. Rain in the next hour minute by minute (/conditions,
// minutelyprecip), official alerts for the place (/alerts) and lightning near
// (/lightning/closest). Its data shows with "Powered by Vaisala Xweather".
struct XweatherNowcast {
    bool rain_ok{false};
    int rain_in_minutes{-1};  // 0: raining now; -1: none in the hour
    int rain_minutes{0};      // how long, from its start
    float rain_peak_mm_h{0.0F};
    bool alerts_ok{false};
    int alert_count{0};
    WeatherAlertLevel highest{WeatherAlertLevel::None};
    std::string alert_name;  // the worst one's
    bool lightning_ok{false};
    bool lightning_near{false};
    float lightning_km{0.0F};
    int lightning_age_minutes{0};
    int lightning_bearing_deg{0};
};

// The requests, without keys: the place to three decimals (about 100 m).
std::string xweather_minutely_path(double latitude, double longitude);
std::string xweather_alerts_path(double latitude, double longitude);
std::string xweather_lightning_path(double latitude, double longitude);
// https://data.api.xweather.com/<path> with the keys as query parameters (the
// API has no header form). Never logged.
std::string xweather_url(std::string_view path, std::string_view client_id, std::string_view client_secret);

// Each fills its part of `out`; false (and that part unset) for an error or a
// malformed answer. "warn_no_data" is a valid empty answer.
bool parse_xweather_minutely(std::string_view json, XweatherNowcast& out);
bool parse_xweather_alerts(std::string_view json, XweatherNowcast& out);
bool parse_xweather_lightning(std::string_view json, std::int64_t now_unix, XweatherNowcast& out);

// Asking costs accesses (15,000 a month on the owner's plan): rain is asked
// for only when the forecast makes it plausible in the next three hours, and
// lightning (ten accesses a time) only when a storm is.
bool xweather_rain_plausible(const WeatherData& data) noexcept;
bool xweather_storm_plausible(const WeatherData& data) noexcept;

// Into the weather: the hazards, and what falls now. Rain falling now shows as
// rain, a strike within 15 km in the last 15 minutes as a storm.
void apply_xweather(WeatherData& data, const XweatherNowcast& now);

// Which requests are due now; asking marks them. Alerts every hour; rain every
// 15 minutes while plausible; lightning every 15 minutes while a storm is.
class XweatherScheduler {
public:
    static constexpr std::uint64_t kAlertsEveryMs = 60ULL * 60ULL * 1000ULL;
    static constexpr std::uint64_t kRainEveryMs = 15ULL * 60ULL * 1000ULL;
    struct Due {
        bool alerts{false};
        bool rain{false};
        bool lightning{false};
    };
    Due due(const WeatherData& data, std::uint64_t now_ms);
    // A new place: everything is asked at the next look.
    void reset() noexcept { asked_ = false; }

private:
    bool asked_{false};
    std::uint64_t alerts_ms_{0};
    std::uint64_t rain_ms_{0};
    std::uint64_t lightning_ms_{0};
    bool rain_asked_{false};
    bool lightning_asked_{false};
};

struct WeatherNotice {
    enum class Kind { Rain, Storm, Alert };
    Kind kind{Kind::Rain};
    int minutes{0};  // Rain: in how many minutes (0: now)
    float km{0.0F};  // Storm: how far
    WeatherAlertLevel level{WeatherAlertLevel::None};
    std::string name;  // Alert: its name
};

// When the weather deserves a notice: rain within kRainSoonMinutes (once,
// until the hour is dry again), a strike within kStormKm (at most once in
// kStormRepeatMinutes), an alert of Moderate or worse (once per alert).
// One notice a look: a storm first, then an alert, then rain.
class WeatherNoticeGate {
public:
    static constexpr int kRainSoonMinutes = 30;
    static constexpr float kStormKm = 25.0F;
    static constexpr std::int64_t kStormRepeatMinutes = 60;

    std::optional<WeatherNotice> observe(const WeatherHazards& hazards, std::int64_t now_minute);

private:
    bool rain_announced_{false};
    bool storm_announced_{false};
    std::int64_t storm_minute_{0};
    std::string alert_announced_;
};

}  // namespace aura

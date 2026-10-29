#include "aura/open_meteo_weather_service.hpp"
#include "aura/weather.hpp"
#include "aura/weather_store.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

aura::WeatherLocation athens() {
    return {"Αθήνα", "Αττική", "Ελλάδα", "GR", "Europe/Athens", 37.9838, 23.7275};
}

aura::WeatherData sample_data() {
    aura::WeatherData data;
    data.location = athens();
    data.temperature_c = 22.4F;
    data.apparent_temperature_c = 23.1F;
    data.wind_kmh = 12.0F;
    data.humidity_percent = 45;
    data.daily_minimum_c = 17.0F;
    data.daily_maximum_c = 25.0F;
    data.condition = aura::WeatherCondition::Clear;
    data.weather_code = 0;
    data.rain_probability_percent = 18;
    data.observed_at = "2026-10-19T18:00";
    data.hourly[0] = {"2026-10-19T18:00", 22.4F, aura::WeatherCondition::Clear, 0, 18};
    data.hourly[1] = {"2026-10-19T21:00", 19.2F, aura::WeatherCondition::PartlyCloudy, 2, 24, false};
    data.hourly_count = 2;
    data.daily[0] = {"2026-10-19", 17.0F, 25.0F, aura::WeatherCondition::Clear, 0, 18};
    data.daily[1] = {"2026-10-20", 16.0F, 23.0F, aura::WeatherCondition::Rain, 63, 78};
    data.daily_count = 2;
    data.provider = aura::WeatherProvider::TlesDetector;
    data.metrics.pressure_hpa = 1014.2F;
    data.metrics.set(aura::WeatherMetric::Pressure);
    data.metrics.visibility_km = 18.5F;
    data.metrics.set(aura::WeatherMetric::Visibility);
    data.metrics.european_aqi = 42;
    data.metrics.set(aura::WeatherMetric::AirQuality);
    data.hazards.alerts_available = true;
    data.hazards.active_alert_count = 2;
    data.hazards.nearby_alert_count = 1;
    data.hazards.highest_alert = aura::WeatherAlertLevel::Moderate;
    data.hazards.lightning_available = true;
    data.hazards.lightning_observed = true;
    data.hazards.nearest_lightning_km = 84.0F;
    data.hazards.lightning_bearing_deg = 215;
    data.hazards.lightning_age_minutes = 4;
    data.hazards.max_flashes_per_5_min = 9;
    data.hazards.xweather_available = true;
    data.hazards.positive_cg_count = 3;
    data.hazards.max_positive_ka = 91;
    return data;
}

}  // namespace

int main() {
    check(athens().valid(), "valid Greek location accepted");
    auto invalid = athens();
    invalid.latitude = 120.0;
    check(!invalid.valid(), "invalid latitude rejected");
    check(aura::weather_condition_from_wmo(0) == aura::WeatherCondition::Clear, "WMO clear mapping");
    check(aura::weather_condition_from_wmo(63) == aura::WeatherCondition::Rain, "WMO rain mapping");
    check(aura::weather_condition_from_wmo(75) == aura::WeatherCondition::Snow, "WMO snow mapping");
    check(aura::weather_condition_from_wmo(96) == aura::WeatherCondition::Thunderstorm,
          "WMO thunder mapping");

    const auto saved_intent = aura::analyze_weather_intent("Πες μου τον καιρό");
    check(saved_intent.kind == aura::WeatherIntentKind::ShowSaved, "saved-city weather intent");
    const auto city_intent = aura::analyze_weather_intent("Τι καιρό κάνει στη Σπάρτη;");
    check(city_intent.kind == aura::WeatherIntentKind::QueryCity && city_intent.city_query == "Σπάρτη",
          "Greek city weather intent");
    const auto change_intent = aura::analyze_weather_intent("Άλλαξε πόλη σε Αθήνα");
    check(change_intent.kind == aura::WeatherIntentKind::ChangeCity && change_intent.city_query == "Αθήνα",
          "change-city intent requires UI selection");
    check(aura::analyze_weather_intent("Πες μου για τον Ταΰγετο").kind == aura::WeatherIntentKind::None,
          "non-weather prompt not intercepted");
    check(aura::analyze_weather_intent("Σε ευχαριστώ για την ευκαιρία").kind == aura::WeatherIntentKind::None,
          "'ευκαιρία' is not weather");
    check(aura::analyze_weather_intent("Ποια είναι η θερμοκρασία βρασμού του νερού;").kind ==
              aura::WeatherIntentKind::None,
          "a physics temperature is a question for the assistant");
    check(aura::analyze_weather_intent("What is the exchange rate?").kind == aura::WeatherIntentKind::None,
          "'exchange' is not 'change'");
    check(aura::analyze_weather_intent("Τι θερμοκρασία έχει έξω;").kind == aura::WeatherIntentKind::ShowSaved,
          "the temperature outside is weather");
    check(aura::analyze_weather_intent("Θα βρέξει αύριο;").kind == aura::WeatherIntentKind::ShowSaved,
          "rain tomorrow is weather");

    aura::WeatherModel model;
    check(model.snapshot().state == aura::WeatherState::Unconfigured, "weather starts unconfigured");
    check(model.snapshot().refresh_interval == aura::WeatherRefreshInterval::Minutes30,
          "weather refresh defaults to 30 minutes");
    check(aura::weather_refresh_interval_minutes(aura::WeatherRefreshInterval::Minutes15) == 15 &&
          aura::weather_refresh_interval_minutes(aura::WeatherRefreshInterval::Minutes30) == 30 &&
          aura::weather_refresh_interval_minutes(aura::WeatherRefreshInterval::Hour1) == 60 &&
          aura::weather_refresh_interval_minutes(aura::WeatherRefreshInterval::Hours3) == 180,
          "weather refresh choices expose bounded minute values");
    check(!aura::weather_refresh_interval_valid(static_cast<aura::WeatherRefreshInterval>(45)) &&
          !model.set_refresh_interval(static_cast<aura::WeatherRefreshInterval>(45)),
          "unsupported weather refresh interval rejected");
    const auto interval_sequence = model.snapshot().sequence;
    check(model.set_refresh_interval(aura::WeatherRefreshInterval::Hour1) &&
          model.snapshot().refresh_interval == aura::WeatherRefreshInterval::Hour1 &&
          model.snapshot().sequence > interval_sequence,
          "valid weather refresh interval updates shared state");
    {
        aura::WeatherModel cleared;
        cleared.set_refresh_interval(aura::WeatherRefreshInterval::Hours3);
        cleared.restore_location(athens());
        cleared.restore_cache(sample_data());
        const auto before_clear = cleared.snapshot().sequence;
        cleared.clear_location();
        check(cleared.snapshot().sequence > before_clear,
              "clearing keeps the sequence increasing so the UI applies it");
        check(cleared.snapshot().refresh_interval == aura::WeatherRefreshInterval::Hours3,
              "clearing the location preserves the refresh preference");
    }
    check(model.restore_location(athens()), "saved location restored");
    check(model.begin_fetch(athens(), true), "weather fetch begins");
    check(model.fetch_succeeded(sample_data()), "weather result accepted");
    check(model.snapshot().state == aura::WeatherState::Ready && model.snapshot().has_data,
          "weather becomes ready");
    model.fetch_failed("NETWORK_UNAVAILABLE", true);
    check(model.snapshot().state == aura::WeatherState::Ready && model.snapshot().data.stale &&
          model.snapshot().notice_code == "OFFLINE_CACHE", "offline keeps last successful data");
    check(model.begin_search("Σπάρτη"), "city search begins");
    model.search_succeeded({athens()});
    check(model.snapshot().state == aura::WeatherState::SearchResults && model.snapshot().suggestion_count == 1,
          "bounded search results accepted");

    const std::string geocoding =
        R"({"results":[{"name":"Σπάρτη","latitude":37.0733,"longitude":22.4297,"country_code":"GR","country":"Ελλάδα","admin1":"Πελοπόννησος","timezone":"Europe/Athens"}]})";
    const auto geo = aura::simulator::parse_open_meteo_geocoding(geocoding);
    check(geo.ok && geo.locations.size() == 1 && geo.locations[0].name == "Σπάρτη",
          "Open-Meteo geocoding JSON parsed as UTF-8");
    check(!aura::simulator::parse_open_meteo_geocoding("{broken").ok,
          "malformed geocoding JSON rejected");

    const std::string forecast = R"({
      "current":{"time":"2026-10-19T18:00","temperature_2m":22.4,"relative_humidity_2m":45,"apparent_temperature":23.1,"is_day":1,"weather_code":1,"wind_speed_10m":12.0},
      "hourly":{"time":["2026-10-19T18:00","2026-10-19T19:00","2026-10-19T20:00","2026-10-19T21:00","2026-10-19T22:00","2026-10-19T23:00","2026-10-20T00:00","2026-10-20T01:00","2026-10-20T02:00","2026-10-20T03:00"],"temperature_2m":[22.4,21.8,21.0,20.2,19.8,19.1,18.5,18.0,17.8,17.4],"weather_code":[1,1,2,2,2,3,3,3,3,2],"precipitation_probability":[18,20,22,24,28,30,32,34,30,26],"is_day":[1,0,0,0,0,0,0,0,0,0]},
      "daily":{"time":["2026-10-19","2026-10-20","2026-10-21","2026-10-22"],"weather_code":[1,63,3,0],"temperature_2m_max":[25,23,21,24],"temperature_2m_min":[17,16,15,16],"precipitation_probability_max":[24,78,35,12]}
    })";
    const auto parsed = aura::simulator::parse_open_meteo_forecast(forecast, athens());
    check(parsed.ok && parsed.data.valid() && parsed.data.hourly_count == 4 && parsed.data.daily_count == 4,
          "Open-Meteo current/hourly/daily JSON parsed");
    check(parsed.data.condition == aura::WeatherCondition::PartlyCloudy,
          "forecast condition mapping applied");
    check(parsed.data.weather_code == 1 && parsed.data.rain_probability_percent == 18 &&
          parsed.data.hourly[1].rain_probability_percent == 24 &&
          parsed.data.daily[1].rain_probability_max_percent == 78,
          "WMO code and rain probability retained");
    check(parsed.data.hourly[0].is_day && !parsed.data.hourly[1].is_day,
          "hourly day/night semantics are retained for weather icons");
    check(!aura::simulator::parse_open_meteo_forecast("{}", athens()).ok,
          "incomplete forecast rejected");

    const std::string tles_weather = R"({
      "status":"available","retrievedAt":"2026-10-20T18:00:00Z",
      "observer":{"latitude":37.9838,"longitude":23.7275},
      "current":{"time":"2026-10-20T21:00","temperatureC":19.4,"apparentTemperatureC":18.8,
        "relativeHumidity":63,"dewPointC":12.1,"pressureHpa":1012.8,"windSpeedKmh":17.2,
        "windGustsKmh":31.0,"windDirectionDeg":220,"cloudCover":72,"precipitationMm":0.4,
        "visibilityM":16800,"uvIndex":0,"weatherCode":61,"isDay":false},
      "hours":[
        {"time":"2026-10-20T21:00","temperatureC":19.4,"weatherCode":61,"precipitationProbability":68,"isDay":false},
        {"time":"2026-10-20T22:00","temperatureC":19.0,"weatherCode":61,"precipitationProbability":72},
        {"time":"2026-10-20T23:00","temperatureC":18.7,"weatherCode":63,"precipitationProbability":76},
        {"time":"2026-10-21T00:00","temperatureC":18.2,"weatherCode":95,"precipitationProbability":82}],
      "days":[
        {"date":"2026-10-20T00:00:00Z","weatherCode":61,"temperatureMaxC":22,"temperatureMinC":16,"precipitationProbability":78},
        {"date":"2026-10-21T00:00:00Z","weatherCode":95,"temperatureMaxC":20,"temperatureMinC":14,"precipitationProbability":88}],
      "airQuality":{"status":"available","current":{"europeanAqi":37}}
    })";
    auto enhanced = aura::simulator::parse_tlesdetector_weather(tles_weather, athens());
    check(enhanced.ok && enhanced.data.valid() &&
          enhanced.data.provider == aura::WeatherProvider::TlesDetector,
          "existing tlesdetector weather payload parsed into bounded AURA data");
    check(enhanced.ok && !enhanced.data.is_day && !enhanced.data.hourly[0].is_day,
          "tlesdetector current and hourly night flags are retained");

    const std::string opportunities = R"({
      "observer":{"latitude":37.9838,"longitude":23.7275},
      "fetchedAt":"2026-10-20T18:00:00Z",
      "weatherAlerts":{"status":"available","totalCount":3,"nearbyCount":1,
        "alerts":[{"severity":"Severe"}]},
      "lightning":{"status":"available","retrievedAt":"2026-10-20T18:00:00Z","clusters":[]},
      "candidates":[{"source":"observed","distanceKm":84,"bearingDeg":215,
        "observedLightning":{"ageMinutes":4,"maxFlashesPer5Min":9,
          "cg":{"positiveCount":3,"maxPositiveKa":91}}}]
    })";
    check(aura::simulator::apply_tlesdetector_opportunities(opportunities, enhanced.data),
          "existing tlesdetector opportunities payload merged");
    check(enhanced.data.metrics.has(aura::WeatherMetric::Pressure) &&
          enhanced.data.metrics.has(aura::WeatherMetric::AirQuality) &&
          enhanced.data.metrics.european_aqi == 37 &&
          enhanced.data.hazards.nearby_alert_count == 1 &&
          enhanced.data.hazards.highest_alert == aura::WeatherAlertLevel::Severe &&
          enhanced.data.hazards.lightning_observed && enhanced.data.hazards.max_positive_ka == 91,
          "tlesdetector metrics, alerts and Xweather lightning retained");
    check(!aura::simulator::parse_tlesdetector_weather(
          R"({"status":"unavailable"})", athens()).ok,
          "unavailable tlesdetector weather rejected");

    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto store_base = std::filesystem::temp_directory_path() / ("aura-weather-test-" + unique) / "weather";
    aura::simulator::WeatherStore store(store_base);
    check(store.load_refresh_interval() == aura::WeatherRefreshInterval::Minutes30,
          "missing weather settings use the safe default interval");
    check(!store.save_refresh_interval(static_cast<aura::WeatherRefreshInterval>(45)),
          "weather store rejects unsupported intervals");
    check(store.save_refresh_interval(aura::WeatherRefreshInterval::Hour1) &&
          store.load_refresh_interval() == aura::WeatherRefreshInterval::Hour1,
          "weather refresh preference persists across store reads");
    check(store.save_location(athens()), "location persisted");
    const auto restored_location = store.load_location();
    check(restored_location && restored_location->name == "Αθήνα", "location persistence round trip");
    check(store.save_cache(sample_data()), "weather cache persisted");
    const auto restored_cache = store.load_cache();
    check(restored_cache && restored_cache->valid() && restored_cache->origin == aura::WeatherDataOrigin::Cache &&
          restored_cache->daily_count == 2 && restored_cache->weather_code == 0 &&
          restored_cache->rain_probability_percent == 18 &&
          !restored_cache->hourly[1].is_day &&
          restored_cache->daily[1].rain_probability_max_percent == 78 &&
          restored_cache->provider == aura::WeatherProvider::TlesDetector &&
          restored_cache->metrics.has(aura::WeatherMetric::AirQuality) &&
          restored_cache->metrics.european_aqi == 42 &&
          restored_cache->hazards.lightning_observed &&
          restored_cache->hazards.max_positive_ka == 91,
          "enriched weather cache round trip");
    check(store.clear_location(), "weather persistence cleared");
    check(store.clear_settings() &&
          store.load_refresh_interval() == aura::WeatherRefreshInterval::Minutes30,
          "factory reset clears the refresh preference");
    std::error_code cleanup_error;
    std::filesystem::remove(store_base.parent_path(), cleanup_error);

    if (failures == 0) std::cout << "Weather tests passed\n";
    // Shown temperatures are whole degrees and never "-0".
    check(aura::whole_degrees(-0.4F) == 0, "-0.4 C reads 0, not -0");
    check(aura::whole_degrees(-0.6F) == -1, "-0.6 C reads -1");
    check(aura::whole_degrees(21.5F) == 22, "halves round away from zero");
    check(aura::whole_degrees(-3.2F) == -3, "below zero keeps its sign");

    return failures == 0 ? 0 : 1;
}

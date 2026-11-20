#pragma once

#include "aura/xweather.hpp"
#include "aura/language.hpp"
#include "aura/weather.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <atomic>
#include <optional>
#include <string>

namespace aura::firmware {

// HTTPS device adapter for Open-Meteo plus the optional tlesdetector
// weather/opportunities APIs. Upstream provider credentials never enter this component.
// The optional enhancement URL and saved city are provisioned in NVS namespace
// "aura_weather" at setup/manufacturing time.
class Esp32WeatherBackend {
public:
    bool initialize();
    bool configured() const noexcept { return initialized_; }
    std::optional<WeatherLocation> saved_location() const { return saved_location_; }
    WeatherRefreshInterval refresh_interval() const noexcept { return refresh_interval_; }
    bool save_location(const WeatherLocation& location);
    bool save_refresh_interval(WeatherRefreshInterval interval);
    bool request(const WeatherLocation& location);
    bool request_search(std::string query, InterfaceLanguage language);
    bool poll(WeatherFetchResult& result);
    bool poll_search(WeatherGeocodingResult& result);
    // Xweather's due requests (xweather.hpp) on the same worker, with the keys
    // the PC sent; false while it is busy. Its answer arrives by poll_xweather().
    bool request_xweather(const std::string& client_id, const std::string& client_secret,
                          const WeatherLocation& location, XweatherScheduler::Due due);
    bool poll_xweather(XweatherNowcast& result);

private:
    enum class Operation { Fetch, Search, Xweather };
    static void worker_entry(void* context);
    WeatherFetchResult fetch(const WeatherLocation& location) const;
    WeatherGeocodingResult search(const std::string& query, InterfaceLanguage language) const;

    std::string server_url_;
    std::optional<WeatherLocation> saved_location_;
    WeatherRefreshInterval refresh_interval_{kDefaultWeatherRefreshInterval};
    Operation pending_operation_{Operation::Fetch};
    WeatherLocation pending_location_;
    std::string pending_query_;
    InterfaceLanguage pending_language_{InterfaceLanguage::Greek};
    std::optional<WeatherFetchResult> completed_;
    std::optional<WeatherGeocodingResult> completed_search_;
    std::string xweather_id_;
    std::string xweather_secret_;
    XweatherScheduler::Due xweather_due_{};
    std::optional<XweatherNowcast> completed_xweather_;
    SemaphoreHandle_t result_mutex_{nullptr};
    std::atomic<bool> busy_{false};
    bool tles_configured_{false};
    bool initialized_{false};
};

}  // namespace aura::firmware

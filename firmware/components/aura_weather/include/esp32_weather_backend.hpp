#pragma once

#include "aura/weather.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <atomic>
#include <optional>
#include <string>

namespace aura::firmware {

// HTTPS device adapter for the existing tlesdetector weather/opportunities APIs. Upstream
// provider credentials never enter this component. The server URL and saved city are
// provisioned in NVS namespace "aura_weather" at setup/manufacturing time.
class Esp32WeatherBackend {
public:
    bool initialize();
    bool configured() const noexcept { return configured_; }
    std::optional<WeatherLocation> saved_location() const { return saved_location_; }
    bool save_location(const WeatherLocation& location);
    bool request(const WeatherLocation& location);
    bool poll(WeatherFetchResult& result);

private:
    static void worker_entry(void* context);
    WeatherFetchResult fetch(const WeatherLocation& location) const;

    std::string server_url_;
    std::optional<WeatherLocation> saved_location_;
    WeatherLocation pending_location_;
    std::optional<WeatherFetchResult> completed_;
    SemaphoreHandle_t result_mutex_{nullptr};
    std::atomic<bool> busy_{false};
    bool configured_{false};
};

}  // namespace aura::firmware

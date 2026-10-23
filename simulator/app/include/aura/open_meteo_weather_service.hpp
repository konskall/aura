#pragma once

#include "aura/weather.hpp"

#include <atomic>
#include <cstdint>
#include <future>
#include <string>

namespace aura::simulator {

class OpenMeteoWeatherService {
public:
    std::future<WeatherGeocodingResult> search(std::string query);
    std::future<WeatherFetchResult> fetch(WeatherLocation location);
    void cancel() noexcept;
    const char* name() const noexcept { return "Open-Meteo"; }

private:
    std::atomic<std::uint64_t> generation_{0};
};

WeatherGeocodingResult parse_open_meteo_geocoding(const std::string& json);
WeatherFetchResult parse_open_meteo_forecast(const std::string& json,
                                             const WeatherLocation& location);
WeatherFetchResult parse_tlesdetector_weather(const std::string& json,
                                              const WeatherLocation& location);
bool apply_tlesdetector_opportunities(const std::string& json, WeatherData& data);

}  // namespace aura::simulator

#pragma once

#include "aura/weather.hpp"

#include <filesystem>
#include <optional>

namespace aura::simulator {

class WeatherStore {
public:
    explicit WeatherStore(std::filesystem::path base_path);

    static std::filesystem::path default_base_path();
    std::optional<WeatherLocation> load_location() const;
    std::optional<WeatherData> load_cache() const;
    WeatherRefreshInterval load_refresh_interval() const;
    bool save_location(const WeatherLocation& location) const;
    bool save_cache(const WeatherData& data) const;
    bool save_refresh_interval(WeatherRefreshInterval interval) const;
    bool clear_location() const;
    bool clear_settings() const;

    const std::filesystem::path& base_path() const noexcept { return base_path_; }

private:
    std::filesystem::path location_path() const;
    std::filesystem::path cache_path() const;
    std::filesystem::path settings_path() const;
    std::filesystem::path base_path_;
};

}  // namespace aura::simulator

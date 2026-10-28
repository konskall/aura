#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace aura {

enum class FuelPriceState { Unavailable, Loading, Ready, Error };

struct FuelPriceData {
    std::string period_date;
    std::string source;
    std::string source_url;
    int unleaded95_millieur_per_litre{0};
    int diesel_millieur_per_litre{0};
    std::optional<int> lpg_millieur_per_litre;
    bool stale{false};
    bool demo{false};
};

struct FuelPriceSnapshot {
    FuelPriceState state{FuelPriceState::Unavailable};
    FuelPriceData data{};
    std::string error_code;
    std::uint64_t sequence{0};
};

class FuelPriceModel {
public:
    bool begin_fetch();
    void fetch_succeeded(FuelPriceData data);
    void fetch_failed(std::string error_code);
    const FuelPriceSnapshot& snapshot() const noexcept { return snapshot_; }

private:
    FuelPriceSnapshot snapshot_{};
};

std::optional<FuelPriceData> parse_fuel_price_json(std::string_view json);
bool fuel_prices_stale(const FuelPriceData& data, int current_year, int current_month, int current_day,
                       int maximum_age_days = 14) noexcept;
const char* fuel_price_state_name(FuelPriceState state) noexcept;
// How long until the prices should be fetched again: every 6 hours when they
// are current, every 15 minutes while they are missing, failed or stale.
std::uint32_t fuel_refresh_interval_ms(const FuelPriceSnapshot& snapshot) noexcept;

}  // namespace aura

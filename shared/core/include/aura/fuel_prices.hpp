#pragma once

#include <cstddef>
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

// The European Commission's Weekly Oil Bulletin, read by the device itself: this
// page names the week's workbook.
inline constexpr char kFuelBulletinPage[] = "https://energy.ec.europa.eu/data-and-analysis/weekly-oil-bulletin_en";
// The address of the week's workbook in that page; empty when it names none on
// the Commission's own site.
std::string fuel_bulletin_link(std::string_view page);
// Greece's prices from the workbook (an .xlsx: XML files in a ZIP). Nothing
// when a part is missing, damaged, too large or out of range.
std::optional<FuelPriceData> parse_fuel_bulletin(const std::uint8_t* workbook, std::size_t size);

bool fuel_prices_stale(const FuelPriceData& data, int current_year, int current_month, int current_day,
                       int maximum_age_days = 14) noexcept;
const char* fuel_price_state_name(FuelPriceState state) noexcept;
// "1,912" or "1.912" (the interface language's separator); "—" when unknown.
std::string fuel_price_text(int millieur_per_litre, const char* decimal_separator);
// How long until the prices should be fetched again: every 6 hours when they
// are current, every 15 minutes while they are missing, failed or stale.
std::uint32_t fuel_refresh_interval_ms(const FuelPriceSnapshot& snapshot) noexcept;

}  // namespace aura

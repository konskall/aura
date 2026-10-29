#include "aura/fuel_prices.hpp"

#include "aura/text_util.hpp"

#include <cstdio>
#include <string>
#include <utility>

namespace aura {
namespace {
constexpr const char* kSource = "European Commission Weekly Oil Bulletin";
constexpr const char* kSourceUrl = "https://energy.ec.europa.eu/data-and-analysis/weekly-oil-bulletin_en";

bool valid_date(const std::string& value) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') return false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 4 || index == 7) continue;
        if (value[index] < '0' || value[index] > '9') return false;
    }
    const int year = std::stoi(value.substr(0, 4));
    const int month = std::stoi(value.substr(5, 2));
    const int day = std::stoi(value.substr(8, 2));
    if (year < 2000 || year > 2199 || month < 1 || month > 12 || day < 1 || day > 31) return false;
    const auto round_trip = from_minute_index(days_from_civil(year, month, day) * 1440);
    return round_trip.year == year && round_trip.month == month && round_trip.day == day;
}

bool valid_price(std::int64_t value) noexcept { return value >= 500 && value <= 5000; }
}  // namespace

bool FuelPriceModel::begin_fetch() {
    if (snapshot_.state == FuelPriceState::Loading) return false;
    snapshot_.state = FuelPriceState::Loading;
    snapshot_.error_code.clear();
    ++snapshot_.sequence;
    return true;
}

void FuelPriceModel::fetch_succeeded(FuelPriceData data) {
    snapshot_.state = FuelPriceState::Ready;
    snapshot_.data = std::move(data);
    snapshot_.error_code.clear();
    ++snapshot_.sequence;
}

void FuelPriceModel::fetch_failed(std::string error_code) {
    snapshot_.state = FuelPriceState::Error;
    snapshot_.error_code = sanitize_text(error_code, 64);
    if (snapshot_.error_code.empty()) snapshot_.error_code = "FUEL_DATA_UNAVAILABLE";
    ++snapshot_.sequence;
}

std::optional<FuelPriceData> parse_fuel_price_json(std::string_view json) {
    const auto object = parse_flat_json(json, 8192);
    if (!object || (object->size() != 8 && object->size() != 9)) return std::nullopt;
    if (const auto* ok = object->find("ok"); ok && (ok->kind != FlatJsonValue::Kind::Bool || !ok->boolean)) {
        return std::nullopt;
    }
    if (object->integer("schema") != 1 || object->string("country") != std::optional<std::string>("GR")) {
        return std::nullopt;
    }
    const auto date = object->string("period_date");
    const auto source = object->string("source");
    const auto source_url = object->string("source_url");
    const auto unleaded = object->integer("unleaded95_millieur_per_litre");
    const auto diesel = object->integer("diesel_millieur_per_litre");
    const auto* lpg = object->find("lpg_millieur_per_litre");
    if (!date || !valid_date(*date) || source != std::optional<std::string>(kSource) ||
        source_url != std::optional<std::string>(kSourceUrl) || !unleaded || !valid_price(*unleaded) ||
        !diesel || !valid_price(*diesel) || !lpg) return std::nullopt;

    FuelPriceData result;
    result.period_date = *date;
    result.source = *source;
    result.source_url = *source_url;
    result.unleaded95_millieur_per_litre = static_cast<int>(*unleaded);
    result.diesel_millieur_per_litre = static_cast<int>(*diesel);
    if (lpg->kind == FlatJsonValue::Kind::Integer && valid_price(lpg->integer)) {
        result.lpg_millieur_per_litre = static_cast<int>(lpg->integer);
    } else if (lpg->kind != FlatJsonValue::Kind::Null) {
        return std::nullopt;
    }
    return result;
}

bool fuel_prices_stale(const FuelPriceData& data, int current_year, int current_month, int current_day,
                       int maximum_age_days) noexcept {
    if (!valid_date(data.period_date) || maximum_age_days < 0) return true;
    const int year = std::stoi(data.period_date.substr(0, 4));
    const int month = std::stoi(data.period_date.substr(5, 2));
    const int day = std::stoi(data.period_date.substr(8, 2));
    const auto age = days_from_civil(current_year, current_month, current_day) - days_from_civil(year, month, day);
    return age < 0 || age > maximum_age_days;
}

std::string fuel_price_text(int millieur_per_litre, const char* decimal_separator) {
    if (millieur_per_litre < 0) return "\u2014";
    char value[24];
    std::snprintf(value, sizeof(value), "%d%s%03d", millieur_per_litre / 1000,
                  decimal_separator ? decimal_separator : ",", millieur_per_litre % 1000);
    return value;
}

const char* fuel_price_state_name(FuelPriceState state) noexcept {
    switch (state) {
        case FuelPriceState::Unavailable: return "unavailable";
        case FuelPriceState::Loading: return "loading";
        case FuelPriceState::Ready: return "ready";
        case FuelPriceState::Error: return "error";
    }
    return "unknown";
}

std::uint32_t fuel_refresh_interval_ms(const FuelPriceSnapshot& snapshot) noexcept {
    const bool current = snapshot.state == FuelPriceState::Ready && !snapshot.data.stale;
    return current ? 6U * 3600U * 1000U : 15U * 60U * 1000U;
}

}  // namespace aura

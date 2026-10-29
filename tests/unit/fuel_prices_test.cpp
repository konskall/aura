#include "aura/fuel_prices.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}  // namespace

int main() {
    using namespace aura;
    const char* valid = R"({"ok":true,"schema":1,"country":"GR","period_date":"2026-10-19","source":"European Commission Weekly Oil Bulletin","source_url":"https://energy.ec.europa.eu/data-and-analysis/weekly-oil-bulletin_en","unleaded95_millieur_per_litre":2191,"diesel_millieur_per_litre":2126,"lpg_millieur_per_litre":null})";
    const auto parsed = parse_fuel_price_json(valid);
    expect(parsed && parsed->unleaded95_millieur_per_litre == 2191 &&
           parsed->diesel_millieur_per_litre == 2126 && !parsed->lpg_millieur_per_litre,
           "official bridge snapshot parses and preserves unavailable LPG");
    expect(!parse_fuel_price_json(R"({"schema":1,"country":"GR"})"), "partial payload is rejected");

    FuelPriceModel model;
    expect(model.begin_fetch() && model.snapshot().state == FuelPriceState::Loading, "fetch starts");
    model.fetch_succeeded(*parsed);
    expect(model.snapshot().state == FuelPriceState::Ready && model.snapshot().data.source.find("Commission") !=
           std::string::npos, "official result becomes visible");
    model.fetch_failed("OFFLINE");
    expect(model.snapshot().state == FuelPriceState::Error && model.snapshot().error_code == "OFFLINE",
           "failure is explicit");

    // Prices refresh on their own: soon while missing, failed or stale, rarely when fresh.
    FuelPriceSnapshot fresh;
    fresh.state = FuelPriceState::Ready;
    FuelPriceSnapshot stale = fresh;
    stale.data.stale = true;
    FuelPriceSnapshot failed;
    failed.state = FuelPriceState::Error;
    expect(fuel_refresh_interval_ms(fresh) >= 3U * 3600U * 1000U, "fresh prices are checked a few times a day");
    expect(fuel_refresh_interval_ms(stale) <= 30U * 60U * 1000U &&
               fuel_refresh_interval_ms(failed) <= 30U * 60U * 1000U &&
               fuel_refresh_interval_ms(FuelPriceSnapshot{}) <= 30U * 60U * 1000U,
           "missing, failed or stale prices are retried within half an hour");
    std::cout << "Fuel price tests passed\n";
    expect(fuel_price_text(1912, ",") == "1,912", "Greek prices use a decimal comma");
    expect(fuel_price_text(1912, ".") == "1.912", "English prices use a decimal point");
    expect(fuel_price_text(1005, ".") == "1.005", "the thousandths keep their zeros");
    expect(fuel_price_text(-1, ".") == "\u2014", "an unknown price is a dash");

    return 0;
}

// The register formats of the board's clock (PCF85063) and power manager
// (AXP2101): pure functions the firmware's I2C drivers use, checked here.
#include "aura/board_chips.hpp"
#include "aura/text_util.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::int64_t utc(int year, int month, int day, int hour, int minute, int second) {
    return aura::days_from_civil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
}

void clock_tests() {
    using aura::pcf85063_decode;
    using aura::pcf85063_encode;
    const auto registers = pcf85063_encode(utc(2026, 11, 5, 12, 34, 56));
    expect(registers && *registers == aura::Pcf85063Time{0x56, 0x34, 0x12, 0x05, 0x04, 0x11, 0x26},
           "seconds to years in BCD, with Thursday as weekday 4");
    expect((*registers)[0] < 0x80, "setting the clock clears the oscillator-stop flag");
    for (const auto when : {utc(2000, 1, 1, 0, 0, 0), utc(2024, 2, 29, 23, 59, 59), utc(2099, 12, 31, 23, 59, 59)}) {
        const auto encoded = pcf85063_encode(when);
        expect(encoded && pcf85063_decode(*encoded) == when, "a time reads back as it was written");
    }
    expect(!pcf85063_encode(utc(1999, 12, 31, 23, 59, 59)) && !pcf85063_encode(utc(2100, 1, 1, 0, 0, 0)),
           "the clock holds 2000 to 2099 only");
    auto stopped = *registers;
    stopped[0] |= 0x80;
    expect(!pcf85063_decode(stopped), "a clock whose oscillator stopped (power was lost) has no time");
    auto bad_minutes = *registers;
    bad_minutes[1] = 0x5A;
    expect(!pcf85063_decode(bad_minutes), "a digit that is not BCD is no time");
    auto bad_month = *registers;
    bad_month[5] = 0x13;
    expect(!pcf85063_decode(bad_month), "month 13 is no time");
    auto bad_day = *registers;
    bad_day[3] = 0x31;
    bad_day[5] = 0x02;
    expect(!pcf85063_decode(bad_day), "31 February is no time");
}

void power_tests() {
    using aura::axp2101_decode;
    aura::Axp2101Registers charging;
    charging.status1 = 0x28;  // VBUS good, battery present
    charging.status2 = 0x20;  // current into the battery
    charging.battery_percent = 87;
    charging.battery_mv_high = 0x10;
    charging.battery_mv_low = 0x68;
    const auto reading = axp2101_decode(charging);
    expect(reading.battery_present && reading.external_power && reading.charging, "USB in and charging");
    expect(reading.percent == 87 && reading.battery_mv == 4200, "the gauge's percent and the battery voltage");
    aura::Axp2101Registers discharging = charging;
    discharging.status1 = 0x08;
    discharging.status2 = 0x40;  // current out of the battery
    const auto on_battery = axp2101_decode(discharging);
    expect(on_battery.battery_present && !on_battery.external_power && !on_battery.charging, "on battery");
    aura::Axp2101Registers full = charging;
    full.status2 = 0x04;  // standby: charge done
    expect(!axp2101_decode(full).charging, "a full battery on USB is not charging");
    aura::Axp2101Registers absent = charging;
    absent.status1 = 0x20;
    expect(!axp2101_decode(absent).battery_present && !axp2101_decode(absent).percent,
           "without a battery there is no percentage");
    aura::Axp2101Registers nonsense = charging;
    nonsense.battery_percent = 140;
    expect(!axp2101_decode(nonsense).percent, "a gauge reading above 100 % is not believed");
    // As the board maker's example: battery, VBUS and system voltage and the
    // temperature measured, the TS pin's detection off, other bits kept.
    expect(aura::axp2101_adc_control(0x02) == 0x1D && aura::axp2101_adc_control(0xE2) == 0xFD,
           "the ADC channels AURA reads, with TS detection off");
    expect(aura::axp2101_battery_detection(0x00) == 0x01 && aura::axp2101_battery_detection(0xF0) == 0xF1,
           "battery detection on, other bits kept");
}

}  // namespace

int main() {
    clock_tests();
    power_tests();
    std::cout << "board_chips_test: PASS\n";
    return 0;
}

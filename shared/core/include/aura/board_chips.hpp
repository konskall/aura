#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "aura/device_status.hpp"

namespace aura {

// The register formats of two chips on the board's shared I2C bus, as pure
// functions: the firmware's drivers (firmware/components/aura_board) only
// move these bytes, and the host tests check them (tests/unit/board_chips_test).

// The real-time clock, an NXP PCF85063 (address 0x51): seven BCD registers
// from 0x04 (seconds) to 0x0A (years). AURA keeps UTC in it, so a change to or
// from daylight saving time, or a new time zone, never needs a write.
using Pcf85063Time = std::array<std::uint8_t, 7>;
// The registers for a UTC time (seconds since 1970) from 2000 to 2099. Writing
// them also clears the oscillator-stop flag (bit 7 of the seconds).
std::optional<Pcf85063Time> pcf85063_encode(std::int64_t utc_seconds) noexcept;
// The UTC time the registers hold, or nothing when the oscillator stopped since
// the clock was last set (the flag: the battery or power was lost) or a field
// is out of range.
std::optional<std::int64_t> pcf85063_decode(const Pcf85063Time& registers) noexcept;

// The power manager, an X-Powers AXP2101 (address 0x34), as its status, fuel
// gauge and battery-voltage registers report it (bit layout as in XPowersLib,
// the board maker's library).
struct Axp2101Registers {
    std::uint8_t status1{};          // 0x00: bit 5 VBUS good, bit 3 battery present
    std::uint8_t status2{};          // 0x01: bits 6-5 battery current (1 in, 2 out, 0 none), 2-0 charger stage
    std::uint8_t battery_percent{};  // 0xA4: the fuel gauge
    std::uint8_t battery_mv_high{};  // 0x34: battery voltage, the high 5 bits
    std::uint8_t battery_mv_low{};   // 0x35: battery voltage, the low 8 bits
    std::uint8_t vbus_mv_high{};     // 0x38: USB voltage, the high 6 bits
    std::uint8_t vbus_mv_low{};      // 0x39
    std::uint8_t system_mv_high{};   // 0x3A: system voltage, the high 6 bits
    std::uint8_t system_mv_low{};    // 0x3B
    std::uint8_t die_temp_high{};    // 0x3C: the chip's temperature, the high 6 bits
    std::uint8_t die_temp_low{};     // 0x3D
};

struct PowerReading {
    bool battery_present{false};
    bool external_power{false};  // USB (VBUS) is in
    bool charging{false};
    std::optional<int> percent;  // 0-100, with a battery present
    int battery_mv{0};
    int vbus_mv{0};
    int system_mv{0};
    int chip_celsius{0};  // the chip's die, to the nearest degree
    ChargeStage stage{ChargeStage::Unknown};
};

PowerReading axp2101_decode(const Axp2101Registers& registers) noexcept;
// The ADC channel control (0x30) with the measurements AURA reads switched on
// (battery, VBUS and system voltage, temperature) and the TS pin's detection
// off, as the board maker's ESP-IDF example sets it ("necessary on the board").
// The other bits stay as they were.
std::uint8_t axp2101_adc_control(std::uint8_t current) noexcept;
// Battery detection control (0x68) with detection on, the other bits kept.
std::uint8_t axp2101_battery_detection(std::uint8_t current) noexcept;

}  // namespace aura

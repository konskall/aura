#pragma once

#include "aura/board_chips.hpp"

#include <cstdint>
#include <optional>

namespace aura::firmware {

// The PCF85063 real-time clock on the board's shared I2C bus (0x51). It keeps
// UTC and runs from the board's backup supply while the ESP32-S3 is off, so a
// restart without Wi-Fi still knows the time. NOT VERIFIED ON HARDWARE.
class Pcf85063Clock {
public:
    // Joins the bus and makes sure the clock runs, in 24-hour mode.
    bool start();
    // The time the clock holds, or nothing if it lost it (see pcf85063_decode).
    std::optional<std::int64_t> read_utc();
    bool write_utc(std::int64_t utc_seconds);

private:
    void* device_{};
};

// The AXP2101 power manager (0x34). It switches on the measurements AURA reads,
// as the board maker's example does, and otherwise only reads: it never
// changes a power rail, the charger's settings or the power key.
// NOT VERIFIED ON HARDWARE.
class Axp2101Power {
public:
    bool start();
    std::optional<PowerReading> read();

private:
    void* device_{};
};

}  // namespace aura::firmware

#pragma once

#include "aura/board_chips.hpp"

#include <cstdint>
#include <optional>

namespace aura::firmware {

// The PCF85063 real-time clock on the board's shared I2C bus (0x51). It keeps
// UTC and runs from the board's backup supply while the ESP32-S3 is off, so a
// restart without Wi-Fi still knows the time (read at boot on the board,
// 2026-11-19: "Time from the board clock").
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
// as the board maker's example does, sets the PWR key's timing and its press
// interrupts, and switches the board off when asked; it never changes a power
// rail or the charger's settings. Read on the board since the first flash
// (2026-11-14): the battery's details in Κατάσταση συσκευής.
class Axp2101Power {
public:
    bool start();
    std::optional<PowerReading> read();
    // The PWR key's presses since the last call (cleared as they are read).
    PowerKeyPresses read_power_key();
    // Switches the board off; held for 1 s, the PWR key switches it on.
    bool power_off();

private:
    void* device_{};
};

}  // namespace aura::firmware

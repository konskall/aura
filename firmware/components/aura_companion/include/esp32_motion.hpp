#pragma once

#include "aura/display_power.hpp"

#include <cstdint>
#include <mutex>
#include <vector>

namespace aura::firmware {

// QMI8658 accelerometer on the board's shared I2C bus, sampled at 50 Hz by
// its own task. Samples are handed to the UI loop, which runs the shared
// ImuGestureDetector; no LVGL call happens here. NOT VERIFIED ON HARDWARE.
class Esp32Motion {
public:
    bool start();
    bool available() const noexcept { return available_; }
    // Moves the queued samples into `output` (appends).
    void drain(std::vector<ImuSample>& output);

private:
    static void task(void* argument);
    bool read_sample(ImuSample& sample);

    void* device_{};
    bool available_{false};
    std::mutex mutex_;
    std::vector<ImuSample> pending_;
};

}  // namespace aura::firmware

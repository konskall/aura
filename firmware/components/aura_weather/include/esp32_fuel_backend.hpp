#pragma once

#include "aura/fuel_prices.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <atomic>
#include <optional>

namespace aura::firmware {

// The week's fuel prices, read by the device from the European Commission's
// Weekly Oil Bulletin: its page names the week's workbook, which is fetched and
// read on a task of its own. The simulator gets the same prices from the bridge.
class Esp32FuelBackend {
public:
    bool initialize();
    // Starts a fetch; false while one is running.
    bool request();
    // True once for each fetch that ended: `prices` is its result, empty if it failed.
    bool poll(std::optional<FuelPriceData>& prices);

private:
    static void worker_entry(void* context);

    SemaphoreHandle_t result_mutex_{nullptr};
    std::atomic<bool> busy_{false};
    bool finished_{false};
    std::optional<FuelPriceData> result_;
};

}  // namespace aura::firmware

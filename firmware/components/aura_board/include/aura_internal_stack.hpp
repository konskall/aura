#pragma once

#include <functional>

namespace aura_board {

// Runs `job` on a short-lived task whose stack is in internal RAM, and waits
// for it; called from a task whose stack is internal already, it just calls
// `job`. One job runs at a time. False if `job` failed or the task could not
// start.
//
// NVS reads and writes go through the SPI flash driver, which turns the cache
// off while it works, and with the cache off a stack in PSRAM is out of reach:
// ESP-IDF stops the board rather than let such a task touch the flash. Threads
// that keep their stacks in PSRAM to spare internal RAM (which ran out on the
// first flash, 2026-11-14) reach NVS through this.
bool run_on_internal_stack(const std::function<bool()>& job);

}  // namespace aura_board

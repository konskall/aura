#include "aura_internal_stack.hpp"

#include <cstdint>

#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace aura_board {
namespace {

// Encrypted NVS (XTS-AES) needs a few KiB of stack; this leaves room.
constexpr std::uint32_t kStackBytes = 6144;

struct Work {
    const std::function<bool()>* job;
    bool result;
};

// Created once and never deleted: a task that has just given `done` may still
// be inside xSemaphoreGive() when the waiting caller resumes.
SemaphoreHandle_t one_at_a_time() {
    static SemaphoreHandle_t gate = xSemaphoreCreateMutex();
    return gate;
}

SemaphoreHandle_t done() {
    static SemaphoreHandle_t finished = xSemaphoreCreateBinary();
    return finished;
}

void run_work(void* argument) {
    auto* work = static_cast<Work*>(argument);
    work->result = (*work->job)();
    // The last touch of `work`, which lives on the caller's stack.
    xSemaphoreGive(done());
    vTaskDelete(nullptr);
}

}  // namespace

bool run_on_internal_stack(const std::function<bool()>& job) {
    int on_this_stack = 0;
    if (esp_ptr_internal(&on_this_stack)) return job();
    SemaphoreHandle_t gate = one_at_a_time();
    SemaphoreHandle_t finished = done();
    if (gate == nullptr || finished == nullptr) return false;
    xSemaphoreTake(gate, portMAX_DELAY);
    Work work{&job, false};
    const bool started = xTaskCreatePinnedToCore(run_work, "aura_nvs", kStackBytes, &work, uxTaskPriorityGet(nullptr),
                                                 nullptr, tskNO_AFFINITY) == pdPASS;
    if (started) xSemaphoreTake(finished, portMAX_DELAY);
    xSemaphoreGive(gate);
    return started && work.result;
}

}  // namespace aura_board

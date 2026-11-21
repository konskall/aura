#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace aura::firmware {

// «Αύρα» on the device (2026-11-20, ADR 0011): microWakeWord's streaming model
// (tools/wakeword, built in) over the microphone's 16 kHz voice. Features every
// 10 ms with the training's settings (esp-micro-speech-features), the model on
// every three of them (30 ms) on its own task, and aura::WakeWordDetector
// counting words as microWakeWord's own test did, so that test's cutoffs hold.
// Everything it hears stays in RAM and is gone once processed.
class Esp32WakeWord {
public:
    struct Stats {
        float peak{0.0F};               // the highest window mean, 0-1
        std::uint32_t inferences{0};
        std::uint32_t dropped{0};       // samples that found the buffer full
        std::uint32_t slowest_us{0};    // the slowest inference
        std::size_t arena_bytes{0};
    };

    // Loads the model and starts its task. False without a built-in model or
    // when it does not load: the device then offers no wake word.
    bool initialize();
    bool ready() const noexcept { return ready_; }
    // The window mean that wakes (wake_word_cutoff).
    void set_cutoff(float probability) noexcept { cutoff_.store(probability); }
    // A new listen: the features, the model's state and the detector start
    // again, and what was waiting is dropped.
    void restart() noexcept { restart_.store(true); }
    // The microphone's voice as it arrives (the companion's loop). Never
    // blocks: what finds the buffer full is counted and dropped.
    void feed(const std::int16_t* samples, std::size_t count) noexcept;
    // True once for each «Αύρα».
    bool take_detection() noexcept { return detected_.exchange(false); }
    // Since the last call (peak and inferences start again).
    Stats take_stats() noexcept;
    // The highest of the tries that came near without waking since the last
    // call (WakeWordDetector::take_near_miss), 0 if none: how near the owner's
    // tries from a distance came.
    float take_near_miss() noexcept;

private:
    static void task_entry(void* argument);
    void task();
    void invoke();

    struct Engine;
    Engine* engine_{nullptr};
    void* stream_{nullptr};
    bool ready_{false};
    std::atomic<float> cutoff_{0.98F};
    std::atomic<bool> restart_{false};
    std::atomic<bool> detected_{false};
    std::atomic<std::uint32_t> dropped_{0};
    std::mutex stats_mutex_;
    Stats stats_{};
    float near_miss_{0.0F};  // under stats_mutex_
};

}  // namespace aura::firmware

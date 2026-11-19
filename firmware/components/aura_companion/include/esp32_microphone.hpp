#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace aura::firmware {

// The ES7210 through the BSP codec device, open only while a recording or the
// barge-in monitor runs, delivering 16 kHz PCM to the shared VoiceRecorder.
// Four TDM slots a frame (measured on the board, 2026-11-16): 0 the speaker's
// signal (MIC3, wired back from the ES8311's output: "AEC" in the schematic),
// 1 and 3 the two microphones, 2 unused. The voice is slot 1, the reference
// slot 0. The ES7210 starts with ~250 ms of noise in every slot: the first
// 300 ms after a start are dropped.
class Esp32Microphone {
public:
    // It was 30 dB; 24 leaves room for a voice close to the device (the
    // questions' peaks of 32767 were the ES7210's start, not speech).
    static constexpr float kVoiceGainDb = 24.0F;
    // While the device speaks: its own voice clipped the microphone at 24 dB
    // (peak 32768 on the board, 2026-11-16), which no echo filter undoes.
    static constexpr float kMonitorGainDb = 18.0F;
    static constexpr std::uint32_t kSettleMs = 300;

    bool initialize();
    bool available() const noexcept { return codec_ != nullptr; }
    bool running() const noexcept { return running_.load(); }
    bool start();
    // Requests stop and waits until the capture task has closed the codec.
    // False means the driver did not quiesce within the bounded timeout; the
    // caller must not drain/restart until a later stop succeeds.
    bool stop();
    void drain(std::vector<std::int16_t>& output);
    // The voice and, sample for sample, the speaker's signal.
    void drain(std::vector<std::int16_t>& output, std::vector<std::int16_t>& reference);
    // Drops what was heard so far (a warm microphone handed to a recording).
    void discard();
    // The voice's gain while open.
    void set_gain(float db);

private:
    static void task(void* argument);
    // The levels of a session, for the log on stop (numbers only).
    // Sums of samples squared, exact in 64-bit integers: the ESP32-S3 has no
    // unit for doubles, and these ran for every frame the microphone heard.
    struct Levels {
        std::int64_t voice{0};
        std::int64_t second{0};
        std::int64_t reference{0};
        int voice_peak{0};
        int reference_peak{0};
        std::uint32_t frames{0};
    };

    void* codec_{};
    std::int16_t* frames_{nullptr};  // one read: 320 frames of four slots, in PSRAM
    std::atomic<bool> running_{false};
    SemaphoreHandle_t lifecycle_{nullptr};
    std::mutex mutex_;
    std::vector<std::int16_t> pending_;
    std::vector<std::int16_t> pending_reference_;
    Levels levels_{};
};

// Voice notes, alarms and notes live on the microSD card (FAT32) when present.
class Esp32CompanionStore {
public:
    bool mount();
    bool mounted() const noexcept { return mounted_; }
    std::string read(const char* name) const;
    bool write(const char* name, const std::string& data) const;
    bool write_note_audio(const std::string& file, const std::vector<std::uint8_t>& wav) const;
    void remove_note_audio(const std::string& file) const;
    std::string note_audio_path(const std::string& file) const;
    void factory_reset() const;
    // The card's size and free space; false without a card.
    bool space(std::uint64_t& total_bytes, std::uint64_t& free_bytes) const;
    // Formats the whole card (FAT). The caller restarts the device after it.
    bool format() const;

private:
    bool mounted_{false};
};

}  // namespace aura::firmware

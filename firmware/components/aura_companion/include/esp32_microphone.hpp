#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace aura::firmware {

// The ES7210 microphones through the BSP codec device, open only while a
// recording runs, delivering 16 kHz mono PCM to the shared VoiceRecorder.
// NOT COMPILED, NOT VERIFIED ON HARDWARE.
class Esp32Microphone {
public:
    bool initialize();
    bool available() const noexcept { return codec_ != nullptr; }
    bool start();
    void stop();
    void drain(std::vector<std::int16_t>& output);

private:
    static void task(void* argument);

    void* codec_{};
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::vector<std::int16_t> pending_;
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

private:
    bool mounted_{false};
};

}  // namespace aura::firmware

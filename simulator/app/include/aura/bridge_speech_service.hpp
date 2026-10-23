#pragma once

#include "aura/voice.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace aura::simulator {

// Speech through the loopback bridge: local whisper.cpp for recognition and
// the installed Windows voices for synthesis. Nothing leaves the PC.
class BridgeSpeechService final : public ISpeechService {
public:
    BridgeSpeechService(std::string token, std::uint16_t port);
    // Reads /health once (blocking, short timeout); call off the UI thread.
    void refresh_health();
    bool stt_available() const override { return stt_->load(); }
    bool tts_available(const std::string& language) const override;
    std::future<TranscriptResult> transcribe(std::vector<std::uint8_t> wav, std::string language) override;
    std::future<SpeechResult> synthesize(std::string text, std::string language) override;

private:
    std::string token_;
    std::uint16_t port_;
    std::shared_ptr<std::atomic<bool>> stt_{std::make_shared<std::atomic<bool>>(false)};
    std::shared_ptr<std::atomic<bool>> tts_el_{std::make_shared<std::atomic<bool>>(false)};
    std::shared_ptr<std::atomic<bool>> tts_en_{std::make_shared<std::atomic<bool>>(false)};
};

// The PC microphone through SDL2, opened only while recording, at 16 kHz mono.
class SdlVoiceCapture {
public:
    SdlVoiceCapture() = default;
    ~SdlVoiceCapture();
    SdlVoiceCapture(const SdlVoiceCapture&) = delete;
    SdlVoiceCapture& operator=(const SdlVoiceCapture&) = delete;

    bool start();
    void stop();
    bool active() const noexcept { return device_ != 0; }
    // Moves captured samples into `output` (appends).
    void drain(std::vector<std::int16_t>& output);
    const std::string& error() const noexcept { return error_; }

private:
    static void callback(void* user, std::uint8_t* stream, int length);

    std::uint32_t device_{0};
    std::mutex mutex_;
    std::vector<std::int16_t> pending_;
    std::string error_;
};

}  // namespace aura::simulator

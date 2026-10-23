#pragma once

#include "aura/audio_service.hpp"
#include "bsp_amplifier_control.hpp"

#include <cstdint>
#include <string>

#include "esp_codec_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace aura::firmware {

class Esp32AudioBackend final : public IAudioService {
public:
    Esp32AudioBackend();
    ~Esp32AudioBackend() override;
    Esp32AudioBackend(const Esp32AudioBackend&) = delete;
    Esp32AudioBackend& operator=(const Esp32AudioBackend&) = delete;

    bool initialize() override;
    void shutdown() override;
    bool play_pcm(const PcmClip& clip) override;
    bool play_wav(const std::string& path) override;
    bool play_test_tone(std::uint32_t duration_ms = 900) override;
    bool pause() override;
    bool resume() override;
    void stop() override;
    void set_volume(float volume) override;
    void set_muted(bool muted) override;
    void tick(std::uint32_t elapsed_ms) override;
    AudioDiagnostics diagnostics() const override;

private:
    static void task_entry(void* context);
    void playback_task();
    bool load_pcm_wav(const std::string& path, PcmClip& clip);
    void set_error_locked(const char* code);

    mutable SemaphoreHandle_t mutex_{nullptr};
    TaskHandle_t task_{nullptr};
    esp_codec_dev_handle_t codec_{nullptr};
    BspAmplifierControl amplifier_{};
    AudioStateMachine machine_{};
    AudioDiagnostics diagnostics_{};
    PcmClip clip_{};
    std::size_t cursor_{0};
    std::uint32_t generation_{0};
    bool shutting_down_{false};
};

}  // namespace aura::firmware

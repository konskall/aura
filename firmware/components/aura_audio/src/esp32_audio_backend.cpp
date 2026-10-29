#include "esp32_audio_backend.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "bsp/esp-bsp.h"
#include "esp_log.h"

namespace aura::firmware {
namespace {
constexpr char kTag[] = "aura_audio";
constexpr std::size_t kMaxPcmSamples = 256U * 1024U;
constexpr std::size_t kChunkSamples = 512;
constexpr double kPi = 3.14159265358979323846;

std::uint16_t le16(const std::uint8_t* value) {
    return static_cast<std::uint16_t>(value[0] | (value[1] << 8));
}

std::uint32_t le32(const std::uint8_t* value) {
    return static_cast<std::uint32_t>(value[0] | (value[1] << 8) | (value[2] << 16) | (value[3] << 24));
}
}

Esp32AudioBackend::Esp32AudioBackend() { mutex_ = xSemaphoreCreateMutex(); }
Esp32AudioBackend::~Esp32AudioBackend() { shutdown(); }

bool Esp32AudioBackend::initialize() {
    if (!mutex_) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (machine_.state() == AudioState::Ready) {
        xSemaphoreGive(mutex_);
        return true;
    }
    xSemaphoreGive(mutex_);

    codec_ = bsp_audio_codec_speaker_init();
    if (!codec_ || !amplifier_.initialize(codec_)) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        machine_.dispatch(AudioCommand::Initialize);
        set_error_locked("ES8311_INIT_FAILED");
        xSemaphoreGive(mutex_);
        return false;
    }
    esp_codec_dev_set_out_vol(codec_, 55);

    xSemaphoreTake(mutex_, portMAX_DELAY);
    machine_.dispatch(AudioCommand::Initialize);
    diagnostics_.backend = "Waveshare BSP • ES8311";
    diagnostics_.device_available = true;
    diagnostics_.format = {24000, 1, 16};
    diagnostics_.error_code.clear();
    diagnostics_.state = machine_.state();
    shutting_down_ = false;
    xSemaphoreGive(mutex_);

    if (xTaskCreatePinnedToCore(task_entry, "aura_audio", 6144, this, 5, &task_, 0) != pdPASS) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        set_error_locked("AUDIO_TASK_CREATE_FAILED");
        xSemaphoreGive(mutex_);
        return false;
    }
    return true;
}

void Esp32AudioBackend::shutdown() {
    if (!mutex_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    shutting_down_ = true;
    ++generation_;
    const TaskHandle_t task = task_;
    xSemaphoreGive(mutex_);
    if (task) xTaskNotifyGive(task);
    for (int i = 0; task_ && i < 50; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    if (task_) {
        vTaskDelete(task_);
        task_ = nullptr;
    }
    amplifier_.shutdown();
    if (codec_) {
        esp_codec_dev_close(codec_);
        codec_ = nullptr;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    clip_.samples.clear();
    machine_.dispatch(AudioCommand::Shutdown);
    diagnostics_.state = machine_.state();
    diagnostics_.device_available = false;
    diagnostics_.envelope = 0.0F;
    xSemaphoreGive(mutex_);
}

bool Esp32AudioBackend::play_pcm(const PcmClip& clip) { return play_clip(clip, false); }

bool Esp32AudioBackend::play_alert(const PcmClip& clip) { return play_clip(clip, true); }

bool Esp32AudioBackend::play_clip(const PcmClip& clip, bool alert) {
    if (!mutex_ || !codec_ || clip.samples.empty() || clip.samples.size() > kMaxPcmSamples ||
        clip.format.bits_per_sample != 16 || clip.format.channels < 1 || clip.format.channels > 2 ||
        (clip.format.sample_rate != 16000 && clip.format.sample_rate != 24000)) return false;
    stop();
    xSemaphoreTake(mutex_, portMAX_DELAY);
    clip_ = clip;
    alert_clip_ = alert;
    pending_wav_path_.clear();
    cursor_ = 0;
    ++generation_;
    machine_.dispatch(AudioCommand::BeginBuffering);
    diagnostics_.state = machine_.state();
    diagnostics_.format = clip.format;
    diagnostics_.buffered_frames = clip.samples.size() / clip.format.channels;
    diagnostics_.progress = 0.0F;
    diagnostics_.error_code.clear();
    const TaskHandle_t task = task_;
    xSemaphoreGive(mutex_);
    if (task) xTaskNotifyGive(task);
    return task != nullptr;
}

bool Esp32AudioBackend::load_pcm_wav(const std::string& path, PcmClip& clip) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    std::array<std::uint8_t, 12> riff{};
    if (std::fread(riff.data(), 1, riff.size(), file) != riff.size() ||
        std::memcmp(riff.data(), "RIFF", 4) != 0 || std::memcmp(riff.data() + 8, "WAVE", 4) != 0) {
        std::fclose(file);
        return false;
    }
    bool have_format = false;
    bool loaded = false;
    while (!loaded) {
        std::array<std::uint8_t, 8> header{};
        if (std::fread(header.data(), 1, header.size(), file) != header.size()) break;
        const std::uint32_t size = le32(header.data() + 4);
        if (std::memcmp(header.data(), "fmt ", 4) == 0) {
            std::array<std::uint8_t, 16> format{};
            if (size < format.size() || std::fread(format.data(), 1, format.size(), file) != format.size()) break;
            clip.format.channels = static_cast<std::uint8_t>(le16(format.data() + 2));
            clip.format.sample_rate = le32(format.data() + 4);
            clip.format.bits_per_sample = static_cast<std::uint8_t>(le16(format.data() + 14));
            have_format = le16(format.data()) == 1 && clip.format.bits_per_sample == 16 &&
                clip.format.channels >= 1 && clip.format.channels <= 2 &&
                (clip.format.sample_rate == 16000 || clip.format.sample_rate == 24000);
            if (size > format.size()) std::fseek(file, static_cast<long>(size - format.size()), SEEK_CUR);
        } else if (std::memcmp(header.data(), "data", 4) == 0 && have_format) {
            if (size == 0 || size / sizeof(std::int16_t) > kMaxPcmSamples) break;
            clip.samples.resize(size / sizeof(std::int16_t));
            loaded = std::fread(clip.samples.data(), 1, size, file) == size;
        } else {
            std::fseek(file, static_cast<long>(size), SEEK_CUR);
        }
        if (size & 1U) std::fseek(file, 1, SEEK_CUR);
    }
    std::fclose(file);
    return loaded;
}

bool Esp32AudioBackend::play_wav(const std::string& path) {
    if (!mutex_ || !codec_ || path.empty() || path.size() > 255 || path.find("..") != std::string::npos ||
        path.rfind("/sdcard/aura/", 0) != 0) return false;
    stop();
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_wav_path_ = path;
    alert_clip_ = false;
    clip_.samples.clear();
    cursor_ = 0;
    ++generation_;
    machine_.dispatch(AudioCommand::BeginBuffering);
    diagnostics_.state = machine_.state();
    diagnostics_.buffered_frames = 0;
    diagnostics_.progress = 0.0F;
    diagnostics_.error_code.clear();
    const TaskHandle_t task = task_;
    xSemaphoreGive(mutex_);
    if (task) xTaskNotifyGive(task);
    return task != nullptr;
}

bool Esp32AudioBackend::play_test_tone(std::uint32_t duration_ms) {
    if (duration_ms < 50 || duration_ms > 3000) return false;
    PcmClip clip;
    clip.format = {24000, 1, 16};
    const std::size_t frames = static_cast<std::size_t>(clip.format.sample_rate) * duration_ms / 1000;
    clip.samples.resize(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        clip.samples[i] = static_cast<std::int16_t>(std::sin(2.0 * kPi * 523.25 * i / clip.format.sample_rate) * 3600.0);
    }
    return play_pcm(clip);
}

bool Esp32AudioBackend::pause() {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool accepted = machine_.dispatch(AudioCommand::Pause);
    diagnostics_.state = machine_.state();
    diagnostics_.envelope = 0.0F;
    xSemaphoreGive(mutex_);
    if (accepted) amplifier_.disable();
    return accepted;
}

bool Esp32AudioBackend::resume() {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool accepted = machine_.dispatch(AudioCommand::Resume);
    diagnostics_.state = machine_.state();
    const bool muted = diagnostics_.muted;
    xSemaphoreGive(mutex_);
    if (accepted && !muted) amplifier_.enable();
    return accepted;
}

void Esp32AudioBackend::stop() {
    if (!mutex_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (machine_.dispatch(AudioCommand::Stop)) {
        ++generation_;
        machine_.dispatch(AudioCommand::Complete);
    }
    diagnostics_.state = machine_.state();
    diagnostics_.envelope = 0.0F;
    diagnostics_.progress = 0.0F;
    diagnostics_.buffered_frames = 0;
    clip_.samples.clear();
    pending_wav_path_.clear();
    alert_clip_ = false;
    xSemaphoreGive(mutex_);
    amplifier_.disable();
}

void Esp32AudioBackend::set_volume(float volume) {
    volume = std::clamp(volume, 0.0F, 1.0F);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    diagnostics_.volume = volume;
    xSemaphoreGive(mutex_);
    if (codec_) esp_codec_dev_set_out_vol(codec_, static_cast<int>(volume * 100.0F));
}

void Esp32AudioBackend::set_muted(bool muted) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    diagnostics_.muted = muted;
    if (muted) diagnostics_.envelope = 0.0F;
    const bool playing = machine_.state() == AudioState::Playing;
    xSemaphoreGive(mutex_);
    if (muted) amplifier_.mute();
    else if (playing) amplifier_.unmute();
}

void Esp32AudioBackend::tick(std::uint32_t) {}

AudioDiagnostics Esp32AudioBackend::diagnostics() const {
    if (!mutex_) return {};
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const AudioDiagnostics copy = diagnostics_;
    xSemaphoreGive(mutex_);
    return copy;
}

void Esp32AudioBackend::task_entry(void* context) {
    static_cast<Esp32AudioBackend*>(context)->playback_task();
}

void Esp32AudioBackend::playback_task() {
    std::array<std::int16_t, kChunkSamples> chunk{};
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (shutting_down_) {
            task_ = nullptr;
            xSemaphoreGive(mutex_);
            vTaskDelete(nullptr);
            return;
        }
        const std::uint32_t generation = generation_;
        const std::string wav_path = pending_wav_path_;
        xSemaphoreGive(mutex_);

        if (!wav_path.empty()) {
            PcmClip loaded;
            const bool ok = load_pcm_wav(wav_path, loaded);
            xSemaphoreTake(mutex_, portMAX_DELAY);
            if (shutting_down_ || generation != generation_) {
                xSemaphoreGive(mutex_);
                continue;
            }
            pending_wav_path_.clear();
            if (!ok) {
                set_error_locked("INVALID_OR_OVERSIZED_WAV");
                xSemaphoreGive(mutex_);
                continue;
            }
            clip_ = std::move(loaded);
            cursor_ = 0;
            diagnostics_.format = clip_.format;
            diagnostics_.buffered_frames = clip_.samples.size() /
                std::max<std::uint8_t>(1, clip_.format.channels);
            xSemaphoreGive(mutex_);
        }

        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (shutting_down_ || generation != generation_ || clip_.samples.empty()) {
            xSemaphoreGive(mutex_);
            continue;
        }
        const AudioFormat format = clip_.format;
        xSemaphoreGive(mutex_);

        // C++ requires designated initializers in declaration order
        // (bits_per_sample, channel, channel_mask, sample_rate, ...); assign instead.
        esp_codec_dev_sample_info_t sample_info{};
        sample_info.bits_per_sample = format.bits_per_sample;
        sample_info.channel = format.channels;
        sample_info.sample_rate = format.sample_rate;
        if (esp_codec_dev_open(codec_, &sample_info) != ESP_CODEC_DEV_OK) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            set_error_locked("ES8311_OPEN_FAILED");
            xSemaphoreGive(mutex_);
            continue;
        }
        xSemaphoreTake(mutex_, portMAX_DELAY);
        machine_.dispatch(AudioCommand::BeginPlayback);
        diagnostics_.state = machine_.state();
        const bool muted = diagnostics_.muted;
        const bool alert = alert_clip_;
        const float owner_volume = diagnostics_.volume;
        xSemaphoreGive(mutex_);
        // A muted device stays muted for new clips (set_muted(false) unmutes),
        // except a clock alert: it rings through mute, never below the alert minimum.
        if (alert) {
            esp_codec_dev_set_out_vol(codec_, static_cast<int>(playback_gain(owner_volume, muted, true) * 100.0F));
        }
        if (!muted || alert) amplifier_.enable();

        bool failed = false;
        for (;;) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            if (shutting_down_ || generation != generation_) {
                xSemaphoreGive(mutex_);
                break;
            }
            if (machine_.state() == AudioState::Paused) {
                xSemaphoreGive(mutex_);
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            if (cursor_ >= clip_.samples.size()) {
                xSemaphoreGive(mutex_);
                break;
            }
            const std::size_t count = std::min(kChunkSamples, clip_.samples.size() - cursor_);
            std::copy_n(clip_.samples.data() + cursor_, count, chunk.data());
            cursor_ += count;
            float peak = 0.0F;
            for (std::size_t i = 0; i < count; ++i) peak = std::max(peak, std::abs(static_cast<float>(chunk[i])) / 32768.0F);
            diagnostics_.envelope = diagnostics_.muted && !alert ? 0.0F : diagnostics_.envelope * 0.7F + peak * 0.3F;
            diagnostics_.progress = static_cast<float>(cursor_) / clip_.samples.size();
            diagnostics_.buffered_frames = (clip_.samples.size() - cursor_) / std::max<std::uint8_t>(1, format.channels);
            xSemaphoreGive(mutex_);
            if (esp_codec_dev_write(codec_, chunk.data(), count * sizeof(std::int16_t)) != ESP_CODEC_DEV_OK) {
                failed = true;
                break;
            }
        }
        if (alert) {
            // Back to the owner's level for everything else.
            xSemaphoreTake(mutex_, portMAX_DELAY);
            const float volume = diagnostics_.volume;
            xSemaphoreGive(mutex_);
            esp_codec_dev_set_out_vol(codec_, static_cast<int>(volume * 100.0F));
        }
        amplifier_.disable();
        esp_codec_dev_close(codec_);
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (generation == generation_) {
            if (failed) set_error_locked("I2S_WRITE_FAILED");
            else if (machine_.state() == AudioState::Playing) machine_.dispatch(AudioCommand::Complete);
            diagnostics_.state = machine_.state();
            diagnostics_.envelope = 0.0F;
            diagnostics_.buffered_frames = 0;
            clip_.samples.clear();
            cursor_ = 0;
        }
        xSemaphoreGive(mutex_);
    }
}

void Esp32AudioBackend::set_error_locked(const char* code) {
    machine_.dispatch(AudioCommand::Fail);
    diagnostics_.state = machine_.state();
    diagnostics_.error_code = code ? code : "AUDIO_ERROR";
    diagnostics_.envelope = 0.0F;
}

}  // namespace aura::firmware

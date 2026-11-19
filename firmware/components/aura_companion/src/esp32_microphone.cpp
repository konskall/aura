#include "esp32_microphone.hpp"

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace aura::firmware {
namespace {

constexpr const char* kTag = "aura-mic";
constexpr std::size_t kMaxPending = 16000 * 2;  // two seconds
constexpr const char* kRoot = "/sdcard/aura";
// One read: 20 ms of four 16-bit TDM slots.
constexpr std::size_t kReadFrames = 320;
constexpr std::size_t kSlots = 4;
constexpr std::size_t kReferenceSlot = 0;
constexpr std::size_t kVoiceSlot = 1;
constexpr std::size_t kSecondSlot = 3;
// The ES7210's registers for MIC3 (the speaker's signal) beside the BSP's
// MIC1 and MIC2, as its driver sets them for three microphones: clocks,
// power, the PGA at 12 dB (the speaker's signal stays far from clipping),
// TDM on SDOUT1, and the reset that applies them.
constexpr int kClockOffReg = 0x01;
constexpr int kMic3Clocks = 0x15;
constexpr int kMic34PowerReg = 0x4C;
constexpr int kMic3GainReg = 0x45;
constexpr int kPgaOn = 0x10;
constexpr int kReferenceGain = 4;  // GAIN_12DB
constexpr int kInterfaceReg = 0x12;
constexpr int kTdm = 0x02;
constexpr int kResetReg = 0x00;

// MIC3 into the TDM frame after the BSP's codec opened MIC1 and MIC2.
void enable_reference(esp_codec_dev_handle_t codec) {
    int value = 0;
    esp_codec_dev_read_reg(codec, kClockOffReg, &value);
    esp_codec_dev_write_reg(codec, kClockOffReg, value & ~kMic3Clocks);
    esp_codec_dev_write_reg(codec, kMic34PowerReg, 0x00);
    esp_codec_dev_read_reg(codec, kMic3GainReg, &value);
    esp_codec_dev_write_reg(codec, kMic3GainReg, (value & ~0x1F) | kPgaOn | kReferenceGain);
    esp_codec_dev_write_reg(codec, kInterfaceReg, kTdm);
    esp_codec_dev_write_reg(codec, kResetReg, 0x71);
    esp_codec_dev_write_reg(codec, kResetReg, 0x41);
}

// The internal RAM the panel's transfers need while the microphone is open:
// with it open the panel's transfers found none (2026-11-16, «Draw bitmap
// failed: ESP_ERR_NO_MEM» while listening).
void log_internal_ram(const char* when) {
    ESP_LOGI(kTag, "microphone %s: internal %u KiB, DMA block %u KiB", when,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) / 1024U));
}

}  // namespace

bool Esp32Microphone::initialize() {
    if (!lifecycle_) {
        lifecycle_ = xSemaphoreCreateBinary();
        if (!lifecycle_) return false;
        xSemaphoreGive(lifecycle_);
    }
    if (!frames_) {
        frames_ = static_cast<std::int16_t*>(
            heap_caps_malloc(kReadFrames * kSlots * sizeof(std::int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!frames_) return false;
    }
    codec_ = bsp_audio_codec_microphone_init();
    ESP_LOGI(kTag, "ES7210 microphone %s", codec_ ? "ready" : "unavailable");
    return codec_ != nullptr;
}

bool Esp32Microphone::start() {
    if (!codec_ || !frames_ || !lifecycle_ || xSemaphoreTake(lifecycle_, 0) != pdTRUE) return false;
    esp_codec_dev_sample_info_t info{};
    info.sample_rate = 16000;
    info.channel = kSlots;  // TDM: 64-bit frames, as the speaker's clips (Esp32AudioBackend)
    info.bits_per_sample = 16;
    auto codec = static_cast<esp_codec_dev_handle_t>(codec_);
    if (esp_codec_dev_open(codec, &info) != ESP_CODEC_DEV_OK) {
        xSemaphoreGive(lifecycle_);
        return false;
    }
    esp_codec_dev_set_in_gain(codec, kVoiceGainDb);
    enable_reference(codec);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.clear();
        pending_reference_.clear();
        levels_ = {};
    }
    running_ = true;
    // The capture task's stack lives in PSRAM: it never touches the flash, and
    // internal RAM is what the panel's transfers and the I2S buffers need while
    // the microphone is open (2026-11-16).
    if (xTaskCreateWithCaps(&Esp32Microphone::task, "aura_mic", 4096, this, 5, nullptr,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        running_ = false;
        esp_codec_dev_close(codec);
        xSemaphoreGive(lifecycle_);
        return false;
    }
    log_internal_ram("on");
    return true;
}

bool Esp32Microphone::stop() {
    const bool was_running = running_.exchange(false);
    if (!lifecycle_) return true;
    if (xSemaphoreTake(lifecycle_, pdMS_TO_TICKS(1500)) != pdTRUE) {
        ESP_LOGE(kTag, "capture task did not stop within 1500 ms");
        return false;
    }
    xSemaphoreGive(lifecycle_);
    if (was_running) {
        log_internal_ram("off");
        Levels levels;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            levels = levels_;
        }
        const auto rms = [&levels](std::int64_t energy) {
            return static_cast<unsigned>(levels.frames == 0 ? 0.0
                                                            : std::sqrt(static_cast<double>(energy) / levels.frames));
        };
        ESP_LOGI(kTag, "microphone heard %u ms: voice rms %u peak %d, second microphone rms %u, speaker rms %u peak %d",
                 static_cast<unsigned>(levels.frames / 16U), rms(levels.voice), levels.voice_peak, rms(levels.second),
                 rms(levels.reference), levels.reference_peak);
    }
    return true;
}

void Esp32Microphone::task(void* argument) {
    auto* self = static_cast<Esp32Microphone*>(argument);
    auto codec = static_cast<esp_codec_dev_handle_t>(self->codec_);
    std::int16_t* frames = self->frames_;
    std::uint32_t settling = 16U * kSettleMs;  // frames still dropped
    while (self->running_.load()) {
        if (esp_codec_dev_read(codec, frames, kReadFrames * kSlots * sizeof(std::int16_t)) != ESP_CODEC_DEV_OK) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        if (settling > 0) {
            settling = settling > kReadFrames ? settling - static_cast<std::uint32_t>(kReadFrames) : 0U;
            continue;
        }
        std::lock_guard<std::mutex> lock(self->mutex_);
        auto& levels = self->levels_;
        for (std::size_t frame = 0; frame < kReadFrames; ++frame) {
            const std::int16_t* slots = frames + frame * kSlots;
            const int voice = slots[kVoiceSlot];
            const int reference = slots[kReferenceSlot];
            const int second = slots[kSecondSlot];
            levels.voice += static_cast<std::int64_t>(voice) * voice;
            levels.second += static_cast<std::int64_t>(second) * second;
            levels.reference += static_cast<std::int64_t>(reference) * reference;
            levels.voice_peak = std::max(levels.voice_peak, voice < 0 ? -voice : voice);
            levels.reference_peak = std::max(levels.reference_peak, reference < 0 ? -reference : reference);
            if (self->pending_.size() < kMaxPending) {
                self->pending_.push_back(slots[kVoiceSlot]);
                self->pending_reference_.push_back(slots[kReferenceSlot]);
            }
        }
        levels.frames += static_cast<std::uint32_t>(kReadFrames);
    }
    esp_codec_dev_close(codec);
    xSemaphoreGive(self->lifecycle_);
    vTaskDeleteWithCaps(nullptr);
}

void Esp32Microphone::drain(std::vector<std::int16_t>& output) {
    std::lock_guard<std::mutex> lock(mutex_);
    output.insert(output.end(), pending_.begin(), pending_.end());
    pending_.clear();
    pending_reference_.clear();
}

void Esp32Microphone::drain(std::vector<std::int16_t>& output, std::vector<std::int16_t>& reference) {
    std::lock_guard<std::mutex> lock(mutex_);
    output.insert(output.end(), pending_.begin(), pending_.end());
    reference.insert(reference.end(), pending_reference_.begin(), pending_reference_.end());
    pending_.clear();
    pending_reference_.clear();
}

void Esp32Microphone::set_gain(float db) {
    if (codec_ && running_.load()) esp_codec_dev_set_in_gain(static_cast<esp_codec_dev_handle_t>(codec_), db);
}

void Esp32Microphone::discard() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.clear();
    pending_reference_.clear();
}

bool Esp32CompanionStore::mount() {
    mounted_ = bsp_sdcard_mount() == ESP_OK;
    if (mounted_) {
        mkdir(kRoot, 0775);
        mkdir("/sdcard/aura/notes", 0775);
    }
    ESP_LOGI(kTag, "microSD %s", mounted_ ? "mounted" : "absent: notes and alarms stay in RAM");
    return mounted_;
}

std::string Esp32CompanionStore::read(const char* name) const {
    if (!mounted_) return {};
    const std::string path = std::string(kRoot) + "/" + name;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) file = std::fopen((path + ".bak").c_str(), "rb");
    if (!file) return {};
    std::string data;
    char buffer[512];
    std::size_t read = 0;
    constexpr std::size_t maximum = 256U * 1024U;
    while (data.size() < maximum &&
           (read = std::fread(buffer, 1, std::min(sizeof(buffer), maximum - data.size()), file)) > 0) {
        data.append(buffer, read);
    }
    const bool oversized = data.size() == maximum && std::fgetc(file) != EOF;
    std::fclose(file);
    return oversized ? std::string{} : data;
}

bool Esp32CompanionStore::write(const char* name, const std::string& data) const {
    if (!mounted_) return false;
    const std::string path = std::string(kRoot) + "/" + name;
    const std::string temporary = path + ".tmp";
    const std::string backup = path + ".bak";
    std::FILE* file = std::fopen(temporary.c_str(), "wb");
    if (!file) return false;
    const bool written = std::fwrite(data.data(), 1, data.size(), file) == data.size();
    const bool flushed = written && std::fflush(file) == 0 && fsync(fileno(file)) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!flushed || !closed) {
        std::remove(temporary.c_str());
        return false;
    }
    struct stat existing{};
    const bool had_previous = stat(path.c_str(), &existing) == 0;
    std::remove(backup.c_str());
    if (had_previous && std::rename(path.c_str(), backup.c_str()) != 0) {
        std::remove(temporary.c_str());
        return false;
    }
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        if (had_previous) std::rename(backup.c_str(), path.c_str());
        std::remove(temporary.c_str());
        return false;
    }
    std::remove(backup.c_str());
    return true;
}

std::string Esp32CompanionStore::note_audio_path(const std::string& file) const {
    return std::string(kRoot) + "/notes/" + file;
}

bool Esp32CompanionStore::write_note_audio(const std::string& file, const std::vector<std::uint8_t>& wav) const {
    if (!mounted_) return false;
    const auto path = note_audio_path(file);
    const auto temporary = path + ".tmp";
    std::FILE* output = std::fopen(temporary.c_str(), "wb");
    if (!output) return false;
    const bool written = std::fwrite(wav.data(), 1, wav.size(), output) == wav.size();
    const bool flushed = written && std::fflush(output) == 0 && fsync(fileno(output)) == 0;
    const bool closed = std::fclose(output) == 0;
    if (!flushed || !closed || std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::remove(temporary.c_str());
        return false;
    }
    return true;
}

void Esp32CompanionStore::remove_note_audio(const std::string& file) const {
    if (mounted_) {
        const auto path = note_audio_path(file);
        std::remove(path.c_str());
        std::remove((path + ".tmp").c_str());
    }
}

bool Esp32CompanionStore::space(std::uint64_t& total_bytes, std::uint64_t& free_bytes) const {
    total_bytes = 0;
    free_bytes = 0;
    if (!mounted_) return false;
    std::uint64_t total = 0;
    std::uint64_t free = 0;
    if (esp_vfs_fat_info(BSP_SD_MOUNT_POINT, &total, &free) != ESP_OK) return false;
    total_bytes = total;
    free_bytes = free;
    return true;
}

bool Esp32CompanionStore::format() const {
    if (!mounted_ || bsp_sdcard == nullptr) return false;
    const auto formatted = esp_vfs_fat_sdcard_format(BSP_SD_MOUNT_POINT, bsp_sdcard);
    if (formatted != ESP_OK) ESP_LOGE(kTag, "microSD format failed: %s", esp_err_to_name(formatted));
    return formatted == ESP_OK;
}

void Esp32CompanionStore::factory_reset() const {
    if (!mounted_) return;
    for (const char* name : {"clock.txt", "notes.txt", "organizer.txt", "conversations.txt", "sound.txt", "display.txt",
                               "language.txt", "app-layout.txt", "photos-settings.json", "aoraton-sync.txt"}) {
        const auto path = std::string(kRoot) + "/" + name;
        std::remove(path.c_str());
        std::remove((path + ".tmp").c_str());
        std::remove((path + ".bak").c_str());
    }
    const std::string notes = std::string(kRoot) + "/notes";
    if (DIR* directory = opendir(notes.c_str())) {
        while (const auto* entry = readdir(directory)) {
            if (entry->d_name[0] == '.') continue;
            std::remove((notes + "/" + entry->d_name).c_str());
        }
        closedir(directory);
    }
}

}  // namespace aura::firmware

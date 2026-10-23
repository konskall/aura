#include "esp32_microphone.hpp"

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdio>
#include <string>
#include <sys/stat.h>

namespace aura::firmware {
namespace {

constexpr const char* kTag = "aura-mic";
constexpr std::size_t kMaxPending = 16000 * 2;  // two seconds
constexpr const char* kRoot = "/sdcard/aura";

}  // namespace

bool Esp32Microphone::initialize() {
    codec_ = bsp_audio_codec_microphone_init();
    ESP_LOGI(kTag, "ES7210 microphone %s", codec_ ? "ready" : "unavailable");
    return codec_ != nullptr;
}

bool Esp32Microphone::start() {
    if (!codec_ || running_.load()) return false;
    esp_codec_dev_sample_info_t info{};
    info.sample_rate = 16000;
    info.channel = 2;  // the ES7210 delivers a stereo pair; the left channel is kept
    info.bits_per_sample = 16;
    auto codec = static_cast<esp_codec_dev_handle_t>(codec_);
    if (esp_codec_dev_open(codec, &info) != ESP_CODEC_DEV_OK) return false;
    esp_codec_dev_set_in_gain(codec, 30.0F);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.clear();
    }
    running_ = true;
    if (xTaskCreate(&Esp32Microphone::task, "aura_mic", 4096, this, 5, nullptr) != pdPASS) {
        running_ = false;
        esp_codec_dev_close(codec);
        return false;
    }
    return true;
}

void Esp32Microphone::stop() { running_ = false; }

void Esp32Microphone::task(void* argument) {
    auto* self = static_cast<Esp32Microphone*>(argument);
    auto codec = static_cast<esp_codec_dev_handle_t>(self->codec_);
    static std::int16_t frames[320 * 2];
    while (self->running_.load()) {
        if (esp_codec_dev_read(codec, frames, sizeof(frames)) != ESP_CODEC_DEV_OK) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        std::lock_guard<std::mutex> lock(self->mutex_);
        for (std::size_t index = 0; index + 1 < sizeof(frames) / sizeof(frames[0]); index += 2) {
            if (self->pending_.size() < kMaxPending) self->pending_.push_back(frames[index]);
        }
    }
    esp_codec_dev_close(codec);
    vTaskDelete(nullptr);
}

void Esp32Microphone::drain(std::vector<std::int16_t>& output) {
    std::lock_guard<std::mutex> lock(mutex_);
    output.insert(output.end(), pending_.begin(), pending_.end());
    pending_.clear();
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
    if (!file) return {};
    std::string data;
    char buffer[512];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0 && data.size() < 256 * 1024) data.append(buffer, read);
    std::fclose(file);
    return data;
}

bool Esp32CompanionStore::write(const char* name, const std::string& data) const {
    if (!mounted_) return false;
    const std::string path = std::string(kRoot) + "/" + name;
    const std::string temporary = path + ".tmp";
    std::FILE* file = std::fopen(temporary.c_str(), "wb");
    if (!file) return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), file) == data.size();
    std::fclose(file);
    if (!ok) return false;
    std::remove(path.c_str());
    return std::rename(temporary.c_str(), path.c_str()) == 0;
}

std::string Esp32CompanionStore::note_audio_path(const std::string& file) const {
    return std::string(kRoot) + "/notes/" + file;
}

bool Esp32CompanionStore::write_note_audio(const std::string& file, const std::vector<std::uint8_t>& wav) const {
    if (!mounted_) return false;
    std::FILE* output = std::fopen(note_audio_path(file).c_str(), "wb");
    if (!output) return false;
    const bool ok = std::fwrite(wav.data(), 1, wav.size(), output) == wav.size();
    std::fclose(output);
    return ok;
}

void Esp32CompanionStore::remove_note_audio(const std::string& file) const {
    if (mounted_) std::remove(note_audio_path(file).c_str());
}

}  // namespace aura::firmware

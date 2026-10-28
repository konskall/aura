#include "esp32_microphone.hpp"

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
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

}  // namespace

bool Esp32Microphone::initialize() {
    if (!lifecycle_) {
        lifecycle_ = xSemaphoreCreateBinary();
        if (!lifecycle_) return false;
        xSemaphoreGive(lifecycle_);
    }
    codec_ = bsp_audio_codec_microphone_init();
    ESP_LOGI(kTag, "ES7210 microphone %s", codec_ ? "ready" : "unavailable");
    return codec_ != nullptr;
}

bool Esp32Microphone::start() {
    if (!codec_ || !lifecycle_ || xSemaphoreTake(lifecycle_, 0) != pdTRUE) return false;
    esp_codec_dev_sample_info_t info{};
    info.sample_rate = 16000;
    info.channel = 2;  // the ES7210 delivers a stereo pair; the left channel is kept
    info.bits_per_sample = 16;
    auto codec = static_cast<esp_codec_dev_handle_t>(codec_);
    if (esp_codec_dev_open(codec, &info) != ESP_CODEC_DEV_OK) {
        xSemaphoreGive(lifecycle_);
        return false;
    }
    esp_codec_dev_set_in_gain(codec, 30.0F);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.clear();
    }
    running_ = true;
    if (xTaskCreate(&Esp32Microphone::task, "aura_mic", 4096, this, 5, nullptr) != pdPASS) {
        running_ = false;
        esp_codec_dev_close(codec);
        xSemaphoreGive(lifecycle_);
        return false;
    }
    return true;
}

bool Esp32Microphone::stop() {
    running_ = false;
    if (!lifecycle_) return true;
    if (xSemaphoreTake(lifecycle_, pdMS_TO_TICKS(1500)) != pdTRUE) {
        ESP_LOGE(kTag, "capture task did not stop within 1500 ms");
        return false;
    }
    xSemaphoreGive(lifecycle_);
    return true;
}

void Esp32Microphone::task(void* argument) {
    auto* self = static_cast<Esp32Microphone*>(argument);
    auto codec = static_cast<esp_codec_dev_handle_t>(self->codec_);
    std::int16_t frames[320 * 2]{};
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
    xSemaphoreGive(self->lifecycle_);
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

void Esp32CompanionStore::factory_reset() const {
    if (!mounted_) return;
    for (const char* name : {"clock.txt", "notes.txt", "organizer.txt", "sound.txt", "display.txt", "language.txt",
                             "app-layout.txt"}) {
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

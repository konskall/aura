#include "esp32_motion.hpp"

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace aura::firmware {
namespace {

constexpr const char* kTag = "aura-motion";
// QMI8658 registers (datasheet rev. 0.9): identity, control and accelerometer data.
constexpr std::uint8_t kWhoAmI = 0x00;
constexpr std::uint8_t kWhoAmIValue = 0x05;
constexpr std::uint8_t kCtrl1 = 0x02;   // bit 6: address auto-increment
constexpr std::uint8_t kCtrl2 = 0x03;   // accelerometer full scale and output data rate
constexpr std::uint8_t kCtrl7 = 0x08;   // bit 0: accelerometer enable
constexpr std::uint8_t kAccelX = 0x35;  // AX_L .. AZ_H
constexpr float kLsbPerG = 8192.0F;     // +/-4 g
constexpr std::size_t kMaxPending = 64;

}  // namespace

bool Esp32Motion::start() {
    if (bsp_i2c_init() != ESP_OK) {
        ESP_LOGW(kTag, "I2C bus unavailable");
        return false;
    }
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    // SA0 selects 0x6B (pulled up on this board) or 0x6A.
    for (const std::uint16_t address : {static_cast<std::uint16_t>(0x6B), static_cast<std::uint16_t>(0x6A)}) {
        i2c_device_config_t config{};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = address;
        config.scl_speed_hz = 400000;
        i2c_master_dev_handle_t device = nullptr;
        if (i2c_master_bus_add_device(bus, &config, &device) != ESP_OK) continue;
        std::uint8_t identity = 0;
        if (i2c_master_transmit_receive(device, &kWhoAmI, 1, &identity, 1, 50) == ESP_OK && identity == kWhoAmIValue) {
            device_ = device;
            break;
        }
        i2c_master_bus_rm_device(device);
    }
    if (!device_) {
        ESP_LOGW(kTag, "QMI8658 not found");
        return false;
    }
    auto* device = static_cast<i2c_master_dev_handle_t>(device_);
    const std::uint8_t setup[][2] = {
        {kCtrl1, 0x40},  // auto-increment, little endian
        {kCtrl2, 0x17},  // +/-4 g, 62.5 Hz
        {kCtrl7, 0x01},  // accelerometer on, gyroscope off
    };
    for (const auto& command : setup) {
        if (i2c_master_transmit(device, command, sizeof(command), 50) != ESP_OK) {
            ESP_LOGW(kTag, "QMI8658 configuration failed");
            return false;
        }
    }
    available_ = xTaskCreate(&Esp32Motion::task, "aura_motion", 3072, this, 4, nullptr) == pdPASS;
    ESP_LOGI(kTag, "QMI8658 %s", available_ ? "sampling at 50 Hz" : "task failed");
    return available_;
}

bool Esp32Motion::read_sample(ImuSample& sample) {
    std::uint8_t raw[6]{};
    auto* device = static_cast<i2c_master_dev_handle_t>(device_);
    if (i2c_master_transmit_receive(device, &kAccelX, 1, raw, sizeof(raw), 20) != ESP_OK) return false;
    const auto axis = [&raw](int index) {
        return static_cast<float>(static_cast<std::int16_t>(raw[index * 2] | (raw[index * 2 + 1] << 8))) / kLsbPerG;
    };
    // Sensor axes to display axes: verify on the board (NEEDS HARDWARE).
    sample.ax = axis(0);
    sample.ay = axis(1);
    sample.az = axis(2);
    sample.t_ms = static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
    return true;
}

void Esp32Motion::task(void* argument) {
    auto* self = static_cast<Esp32Motion*>(argument);
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        ImuSample sample;
        if (self->read_sample(sample)) {
            std::lock_guard<std::mutex> lock(self->mutex_);
            if (self->pending_.size() < kMaxPending) self->pending_.push_back(sample);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(20));
    }
}

void Esp32Motion::drain(std::vector<ImuSample>& output) {
    std::lock_guard<std::mutex> lock(mutex_);
    output.insert(output.end(), pending_.begin(), pending_.end());
    pending_.clear();
}

}  // namespace aura::firmware

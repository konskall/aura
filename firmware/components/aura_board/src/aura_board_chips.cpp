#include "aura_board_chips.hpp"

#include <cstring>

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

namespace aura::firmware {
namespace {

constexpr const char* kTag = "aura_chips";
constexpr int kTimeoutMs = 50;
constexpr std::uint16_t kClockAddress = 0x51;
constexpr std::uint16_t kPowerAddress = 0x34;
// PCF85063 registers: Control_1, then the seven time registers from seconds.
constexpr std::uint8_t kClockControl1 = 0x00;
constexpr std::uint8_t kClockSeconds = 0x04;
constexpr std::uint8_t kClock12Hour = 0x02;  // Control_1 bit 1
constexpr std::uint8_t kClockStop = 0x20;    // Control_1 bit 5
// AXP2101 registers (XPowersLib names in brackets).
constexpr std::uint8_t kPowerStatus = 0x00;          // [STATUS1], then [STATUS2]
constexpr std::uint8_t kPowerAdcControl = 0x30;      // [ADC_CHANNEL_CTRL]
constexpr std::uint8_t kPowerBatteryVoltage = 0x34;  // [ADC_DATA_RELUST0/1]
constexpr std::uint8_t kPowerDetection = 0x68;       // [BAT_DET_CTRL]
constexpr std::uint8_t kPowerPercent = 0xA4;         // [BAT_PERCENT_DATA]

void* join_bus(std::uint16_t address) {
    if (bsp_i2c_init() != ESP_OK) return nullptr;
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == nullptr) return nullptr;
    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = 400000;
    i2c_master_dev_handle_t device = nullptr;
    if (i2c_master_bus_add_device(bus, &config, &device) != ESP_OK) return nullptr;
    return device;
}

bool read_registers(void* device, std::uint8_t first, std::uint8_t* out, std::size_t length) {
    return i2c_master_transmit_receive(static_cast<i2c_master_dev_handle_t>(device), &first, 1, out, length,
                                       kTimeoutMs) == ESP_OK;
}

bool write_registers(void* device, std::uint8_t first, const std::uint8_t* data, std::size_t length) {
    std::uint8_t buffer[8];
    if (length + 1 > sizeof(buffer)) return false;
    buffer[0] = first;
    std::memcpy(buffer + 1, data, length);
    return i2c_master_transmit(static_cast<i2c_master_dev_handle_t>(device), buffer, length + 1, kTimeoutMs) ==
           ESP_OK;
}

// Writes `wanted` only if the register holds something else.
bool settle_register(void* device, std::uint8_t address, std::uint8_t current, std::uint8_t wanted) {
    return wanted == current || write_registers(device, address, &wanted, 1);
}

}  // namespace

bool Pcf85063Clock::start() {
    device_ = join_bus(kClockAddress);
    std::uint8_t control = 0;
    if (device_ == nullptr || !read_registers(device_, kClockControl1, &control, 1)) {
        ESP_LOGW(kTag, "PCF85063 clock not answering");
        device_ = nullptr;
        return false;
    }
    return settle_register(device_, kClockControl1, control,
                           static_cast<std::uint8_t>(control & ~(kClock12Hour | kClockStop)));
}

std::optional<std::int64_t> Pcf85063Clock::read_utc() {
    Pcf85063Time registers{};
    if (device_ == nullptr || !read_registers(device_, kClockSeconds, registers.data(), registers.size())) {
        return std::nullopt;
    }
    return pcf85063_decode(registers);
}

bool Pcf85063Clock::write_utc(std::int64_t utc_seconds) {
    const auto registers = pcf85063_encode(utc_seconds);
    return device_ != nullptr && registers &&
           write_registers(device_, kClockSeconds, registers->data(), registers->size());
}

bool Axp2101Power::start() {
    device_ = join_bus(kPowerAddress);
    std::uint8_t adc = 0;
    std::uint8_t detection = 0;
    if (device_ == nullptr || !read_registers(device_, kPowerAdcControl, &adc, 1) ||
        !read_registers(device_, kPowerDetection, &detection, 1)) {
        ESP_LOGW(kTag, "AXP2101 power manager not answering");
        device_ = nullptr;
        return false;
    }
    return settle_register(device_, kPowerAdcControl, adc, axp2101_adc_control(adc)) &&
           settle_register(device_, kPowerDetection, detection, axp2101_battery_detection(detection));
}

std::optional<PowerReading> Axp2101Power::read() {
    std::uint8_t status[2]{};
    std::uint8_t voltage[2]{};
    std::uint8_t percent = 0;
    if (device_ == nullptr || !read_registers(device_, kPowerStatus, status, sizeof(status)) ||
        !read_registers(device_, kPowerBatteryVoltage, voltage, sizeof(voltage)) ||
        !read_registers(device_, kPowerPercent, &percent, 1)) {
        return std::nullopt;
    }
    Axp2101Registers registers;
    registers.status1 = status[0];
    registers.status2 = status[1];
    registers.battery_mv_high = voltage[0];
    registers.battery_mv_low = voltage[1];
    registers.battery_percent = percent;
    return axp2101_decode(registers);
}

}  // namespace aura::firmware

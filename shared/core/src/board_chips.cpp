#include "aura/board_chips.hpp"

#include "aura/text_util.hpp"

namespace aura {
namespace {

constexpr std::int64_t kSecondsPerDay = 86400;

std::uint8_t to_bcd(int value) noexcept {
    return static_cast<std::uint8_t>(((value / 10) << 4) | (value % 10));
}

// The value of a BCD byte (`mask` drops the bits that are not digits), or -1.
int from_bcd(std::uint8_t value, std::uint8_t mask) noexcept {
    const std::uint8_t digits = value & mask;
    const int tens = digits >> 4;
    const int ones = digits & 0x0F;
    if (tens > 9 || ones > 9) return -1;
    return tens * 10 + ones;
}

int days_in_month(int year, int month) noexcept {
    static constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && leap ? 29 : kDays[month - 1];
}

}  // namespace

std::optional<Pcf85063Time> pcf85063_encode(std::int64_t utc_seconds) noexcept {
    if (utc_seconds < 0) return std::nullopt;
    const std::int64_t day = utc_seconds / kSecondsPerDay;
    const auto seconds_of_day = static_cast<int>(utc_seconds % kSecondsPerDay);
    const auto date = from_minute_index(day * 1440);
    if (date.year < 2000 || date.year > 2099) return std::nullopt;
    return Pcf85063Time{to_bcd(seconds_of_day % 60),
                        to_bcd(seconds_of_day / 60 % 60),
                        to_bcd(seconds_of_day / 3600),
                        to_bcd(date.day),
                        static_cast<std::uint8_t>(date.weekday),
                        to_bcd(date.month),
                        to_bcd(date.year - 2000)};
}

std::optional<std::int64_t> pcf85063_decode(const Pcf85063Time& registers) noexcept {
    if ((registers[0] & 0x80) != 0) return std::nullopt;  // the oscillator stopped
    const int second = from_bcd(registers[0], 0x7F);
    const int minute = from_bcd(registers[1], 0x7F);
    const int hour = from_bcd(registers[2], 0x3F);  // 24-hour mode
    const int day = from_bcd(registers[3], 0x3F);
    const int month = from_bcd(registers[5], 0x1F);
    const int year = from_bcd(registers[6], 0xFF);
    if (second < 0 || second > 59 || minute < 0 || minute > 59 || hour < 0 || hour > 23 || month < 1 ||
        month > 12 || year < 0) {
        return std::nullopt;
    }
    if (day < 1 || day > days_in_month(2000 + year, month)) return std::nullopt;
    return days_from_civil(2000 + year, month, day) * kSecondsPerDay + hour * 3600 + minute * 60 + second;
}

PowerReading axp2101_decode(const Axp2101Registers& registers) noexcept {
    PowerReading reading;
    reading.external_power = (registers.status1 & 0x20) != 0;
    reading.battery_present = (registers.status1 & 0x08) != 0;
    reading.charging = ((registers.status2 >> 5) & 0x03) == 0x01;
    if (reading.battery_present && registers.battery_percent <= 100) reading.percent = registers.battery_percent;
    reading.battery_mv = ((registers.battery_mv_high & 0x1F) << 8) | registers.battery_mv_low;
    reading.vbus_mv = ((registers.vbus_mv_high & 0x3F) << 8) | registers.vbus_mv_low;
    reading.system_mv = ((registers.system_mv_high & 0x3F) << 8) | registers.system_mv_low;
    // XPowersLib: 22 + (7274 - raw) / 20 °C, here in tenths and then rounded.
    const int raw = ((registers.die_temp_high & 0x3F) << 8) | registers.die_temp_low;
    const int tenths = 220 + (7274 - raw) / 2;
    reading.chip_celsius = (tenths + (tenths >= 0 ? 5 : -5)) / 10;
    const int stage = registers.status2 & 0x07;
    reading.stage = stage <= 5 ? static_cast<ChargeStage>(stage) : ChargeStage::Unknown;
    return reading;
}

std::uint8_t axp2101_adc_control(std::uint8_t current) noexcept {
    constexpr std::uint8_t kBattery = 0x01;
    constexpr std::uint8_t kTsPin = 0x02;
    constexpr std::uint8_t kVbus = 0x04;
    constexpr std::uint8_t kSystem = 0x08;
    constexpr std::uint8_t kTemperature = 0x10;
    return static_cast<std::uint8_t>((current | kBattery | kVbus | kSystem | kTemperature) & ~kTsPin);
}

std::uint8_t axp2101_battery_detection(std::uint8_t current) noexcept {
    return static_cast<std::uint8_t>(current | 0x01);
}

}  // namespace aura

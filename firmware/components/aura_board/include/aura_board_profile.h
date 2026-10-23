#pragma once

namespace aura_board {

inline constexpr const char* profile_name = "waveshare-esp32-s3-touch-amoled-2.16-r1";

struct Pins {
    static constexpr int lcd_sio0 = 4;
    static constexpr int lcd_sio1 = 5;
    static constexpr int lcd_sio2 = 6;
    static constexpr int lcd_sio3 = 7;
    static constexpr int lcd_clock = 38;
    static constexpr int lcd_chip_select = 12;
    static constexpr int lcd_reset = 39;
    static constexpr int i2c_scl = 14;
    static constexpr int i2c_sda = 15;
    static constexpr int touch_interrupt = 11;
    static constexpr int touch_reset = 40;
    static constexpr int sd_cmd = 1;
    static constexpr int sd_clock = 2;
    static constexpr int sd_data0 = 3;
    static constexpr int sd_chip_select = 41;
    static constexpr int i2s_master_clock = 42;
    static constexpr int i2s_bit_clock = 9;
    static constexpr int i2s_word_select = 45;
    static constexpr int audio_playback_data = 8;
    static constexpr int audio_capture_data = 10;
    static constexpr int amplifier_enable = 46;
    static constexpr int imu_interrupt1 = 17;
    static constexpr int imu_interrupt2 = 21;
    static constexpr int rtc_interrupt = 13;
    static constexpr int user_button = 18;
    static constexpr int boot_button = 0;
};

struct Display {
    static constexpr int width = 480;
    static constexpr int height = 480;
    static constexpr int partial_buffer_lines = 48;
};

}  // namespace aura_board


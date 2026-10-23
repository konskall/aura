# Hardware target and verified board profile

Target: Waveshare ESP32-S3-Touch-AMOLED-2.16, battery-included option (nominal 3.7 V / 1000 mAh), optional FAT32 microSD.

Verified from the current official Waveshare documentation on 2026-10-17:

- ESP32-S3R8, dual-core Xtensa LX7 up to 240 MHz, 512 KiB on-chip SRAM, 8 MiB PSRAM, 16 MiB flash.
- 480 × 480 2.16-inch AMOLED, CO5300 QSPI controller.
- CST9220 touch over shared I2C; the current BSP registry dependency names a compatible `cst9217` driver and must be validated on hardware.
- AXP2101 PMIC, QMI8658 IMU, PCF85063 RTC.
- ES7210 microphone ADC and ES8311 playback codec/amplifier path. A physical speaker is not assumed.
- TF slot, native USB-C, 2.4 GHz Wi-Fi, Bluetooth 5 LE. Bluetooth Classic A2DP/HFP is not promised.

## Central board pins

| Function | GPIO |
|---|---|
| AMOLED SIO0/SIO1/SIO2/SIO3 | 4 / 5 / 6 / 7 |
| AMOLED clock / CS / reset | 38 / 12 / 39 |
| Shared I2C SCL / SDA | 14 / 15 |
| Touch interrupt / reset | 11 / 40 |
| SD CMD / CLK / D0 / CS | 1 / 2 / 3 / 41 |
| I2S MCLK / BCLK / LRCK | 42 / 9 / 45 |
| Playback data / microphone data / amplifier | 8 / 10 / 46 |
| QMI8658 INT1 / INT2 | 17 / 21 |
| RTC interrupt | 13 |
| User / boot buttons | 18 / 0 |

The code consumes these through `firmware/components/aura_board/include/aura_board_profile.h`; pins must not be scattered through application code. Safe PMIC/display/audio initialization order still needs BSP source inspection and physical verification.

Board documentation: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.16 (pinout, chips, ESP-IDF examples). Checked there and in the BSP 2.0.1 header for v13: GPIO18 is the user key (10 k pull-up, low when pressed), the QMI8658 sits on the shared I2C bus with INT1/INT2 on GPIO17/21 (address 0x6B or 0x6A by SA0), two microphones feed the ES7210, and the BSP provides `bsp_i2c_get_handle`, `bsp_audio_codec_microphone_init`, `bsp_sdcard_mount` and `bsp_display_brightness_set` (CO5300 command 0x51). The docs do not describe a speaker connector.

Authoritative references: official Waveshare product documentation and schematic, official managed BSP `waveshare/esp32_s3_touch_amoled_2_16` 2.0.1, ESP-IDF 5.5.5 documentation.


# Hardware target and verified board profile

Target: Waveshare ESP32-S3-Touch-AMOLED-2.16, battery-included option (nominal 3.7 V / 1000 mAh), with a FAT32 microSD card. The card (`/sdcard/aura`) holds photos, notes and voice notes, file transfers and the cache of Aoraton's encrypted attachments. Without one the device still runs: notes and alarms stay in memory and nothing is cached.

Verified from the current official Waveshare documentation on 2026-10-17:

- ESP32-S3R8, dual-core Xtensa LX7 up to 240 MHz, 512 KiB on-chip SRAM, 8 MiB PSRAM, 16 MiB flash.
- 480 × 480 2.16-inch AMOLED, CO5300 QSPI controller.
- CST9220 touch over shared I2C; the current BSP registry dependency names a compatible `cst9217` driver and must be validated on hardware.
- AXP2101 PMIC, QMI8658 IMU, PCF85063 RTC.
  - Drivers since 2026-11-05 (`firmware/components/aura_board/src/aura_board_chips.cpp`, register formats in `shared/core/src/board_chips.cpp`, both NOT VERIFIED ON HARDWARE):
    - **PCF85063 at 0x51.** It keeps UTC and sets the system clock at boot. Each SNTP sync writes it again.
    - **AXP2101 at 0x34.** It is read every 5 s: battery percent, voltage, USB and charging.
  - The AXP2101 setup does what Waveshare's ESP-IDF example `01_AXP2101` does with its bundled XPowersLib:
    - ADC control 0x30: battery, VBUS and system voltage and the temperature on, TS-pin detection off ("necessary on the board");
    - battery detection 0x68 bit 0 on;
    - nothing else: no rail, charger or power-key writes.
- ES7210 microphone ADC and ES8311 playback codec/amplifier path. A physical speaker is not assumed.
- TF slot, native USB-C, 2.4 GHz Wi-Fi, Bluetooth 5 LE. Bluetooth Classic A2DP/HFP is not promised.

## Flash layout (16 MB, `firmware/partitions.csv`)

Since 2026-11-06, the owner's choice:

- **Two app slots of 7 MB** (`ota_0` at `0x20000`, `ota_1` at `0x720000`) for over-the-air updates. An update may fill one slot and no more (`OtaUpdateModel::kMaxImageBytes`).
- **`storage`**: 1.8 MB of littlefs at `0xE20000`. Nothing uses it today, because everything the owner keeps lives on the microSD card; it is a reserve.
- **NVS** (encrypted), its keys, the OTA data and a 64 KB core dump.

The production table (`partitions.production.csv`) moves only the small partitions for the signed bootloader. The app slots stay where they are. A new layout needs a full flash over USB, because an update cannot change it. `aura_firmware_config_tests` checks both tables.

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

Power off is deep sleep woken by the user key (GPIO18) only: the touch interrupt no longer wakes the board, so a touch in a pocket cannot switch it on, and the screen says to press the side button. The panel dims through `bsp_display_brightness_set` alone (the shared UI draws no dimming veil on the device).

The code consumes these through `firmware/components/aura_board/include/aura_board_profile.h`; pins must not be scattered through application code. Safe PMIC/display/audio initialization order still needs BSP source inspection and physical verification.

Board documentation: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.16 (pinout, chips, ESP-IDF examples). Checked there and in the BSP 2.0.1 header for v13: GPIO18 is the user key (10 k pull-up, low when pressed), the QMI8658 sits on the shared I2C bus with INT1/INT2 on GPIO17/21 (address 0x6B or 0x6A by SA0), two microphones feed the ES7210, and the BSP provides `bsp_i2c_get_handle`, `bsp_audio_codec_microphone_init`, `bsp_sdcard_mount` and `bsp_display_brightness_set` (CO5300 command 0x51). The docs do not describe a speaker connector.

Authoritative references: official Waveshare product documentation and schematic, official managed BSP `waveshare/esp32_s3_touch_amoled_2_16` 2.0.1, ESP-IDF 5.5.5 documentation.


# Hardware target and verified board profile

The first flash and the checks on the board: [FIRST_FLASH.md](FIRST_FLASH.md).

Target: Waveshare ESP32-S3-Touch-AMOLED-2.16, battery-included option (nominal 3.7 V / 1000 mAh), with a FAT32 microSD card. The card (`/sdcard/aura`) holds photos, notes and voice notes, file transfers and the cache of Aoraton's encrypted attachments. Without one the device still runs: notes and alarms stay in memory and nothing is cached. Its free space is read a minute apart (`esp_vfs_fat_info`). `Διαμόρφωση κάρτας` runs `esp_vfs_fat_sdcard_format` and restarts; NVS (Wi-Fi, the keys, the Aoraton account) is kept.

Verified from the current official Waveshare documentation on 2026-10-17:

- ESP32-S3R8, dual-core Xtensa LX7 up to 240 MHz, 512 KiB on-chip SRAM, 8 MiB PSRAM, 16 MiB flash.
- 480 × 480 2.16-inch AMOLED, CO5300 QSPI controller.
- CST9220 touch over shared I2C; the current BSP registry dependency names a compatible `cst9217` driver and must be validated on hardware.
- AXP2101 PMIC, QMI8658 IMU, PCF85063 RTC.
  - Drivers since 2026-11-05 (`firmware/components/aura_board/src/aura_board_chips.cpp`, register formats in `shared/core/src/board_chips.cpp`, both NOT VERIFIED ON HARDWARE):
    - **PCF85063 at 0x51.** It keeps UTC and sets the system clock at boot. Each SNTP sync writes it again.
    - **AXP2101 at 0x34.** It is read every 5 s: battery percent (0xA4), USB and charging (0x00, 0x01), and the ADC block 0x34–0x3D.
      - The block gives battery, USB and system voltage, and the die temperature: 22 + (7274 − raw) / 20 °C, as XPowersLib computes it. Bits 2–0 of 0x01 give the charger's stage.
      - The chip measures no health or cycle count. Κατάσταση συσκευής shows the rated 1000 mAh instead.
  - The AXP2101 setup does what Waveshare's ESP-IDF example `01_AXP2101` does with its bundled XPowersLib:
    - ADC control 0x30: battery, VBUS and system voltage and the temperature on, TS-pin detection off ("necessary on the board");
    - battery detection 0x68 bit 0 on;
    - since the first flash, the PWR key: its timing (0x27), its short and long presses (0x41), and switching off (0x10 bit 0); no rail or charger writes.
- ES7210 microphone ADC and ES8311 playback codec/amplifier path. The owner's board has a speaker (2026-11-06).
  The firmware plays clips at 16 or 24 kHz, and the radio as a continuous stream at the station's own rate (8–48 kHz). The codec is reopened in between, because the board has one output.
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
| Keys: right (user), left (BOOT), centre (PWR) | GPIO18 / GPIO0 / the AXP2101's PWRON |

The keys (the owner's request on the first flash, 2026-11-14): the left one turns the volume down and the right one up, 10 % a press and again every 200 ms while held. The centre one is the PMU's PWR key: a press wakes or sleeps the screen, held 1.5 s it switches AURA off through the PMU (0x10 bit 0), and held 1 s while off the PMU switches it on. Its presses are read from the AXP2101 every 100 ms (interrupt status 0x49; timing 0x27: long press 1.5 s, the chip's own forced off 6 s, on 1 s). Without the PMU, power off is deep sleep woken by the right key; the touch interrupt never wakes the board. The panel dims through `bsp_display_brightness_set` alone (the shared UI draws no dimming veil on the device).

## Internal RAM (the first flash, 2026-11-14)

The board ran out of internal RAM at boot until these held; `aura_firmware_config_tests` checks the settings.

- malloc() takes PSRAM first (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0`); 64 KiB stay for allocations that ask for internal or DMA memory (`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL`).
- Wi-Fi's and lwIP's buffers in PSRAM, Wi-Fi's code out of IRAM.
- The panel's SPI transfers are 6 rows (the driver copies each from PSRAM into internal RAM first). `psram_dma_direct` underflows at the panel's 40 MHz.
- Threads that never touch the flash keep their stacks in PSRAM; NVS from them goes through `aura_board::run_on_internal_stack()` (the flash driver turns the cache off).
- LVGL's lock: `aura_board_display_lock()`, never the BSP 2.0.1's `bsp_display_lock()`, whose result is inverted.
- The screen turns through the panel's MADCTL (0xA0 upright, 0xC0 the right edge down, 0x60 upside down, 0x00 the left edge down); touches are turned back in software. The QMI8658's axes match the display frame (checked on the board).

## Σύναψη on the board (2026-11-15)

- Its worker draws on the first core (priority 1, taking turns with the UI loop), its helper on the second (priority 1, below LVGL). The worker's two strip memories are internal DMA RAM, the helper's PSRAM (`PanelSynapseOutput` in `app_main.cpp`).
- The frame's middle rows go to the panel straight (`esp_lcd_panel_draw_bitmap` under LVGL's lock); LVGL draws the rest, and the whole frame when anything covers it.
- The device frame's loops run from IRAM (~9 KiB). In them: no `sqrtf` (it runs from flash) and no float division (ROM's `__divsf3`), and bits move with `__builtin_bit_cast` (a 4-byte `std::memcpy` became a ROM call).
- The worker wakes the UI loop when it has drawn a frame (`FrameWorker::set_finished`); while the screen is lit the loop sleeps no longer than Synapse's rest.

The code consumes these through `firmware/components/aura_board/include/aura_board_profile.h`; pins must not be scattered through application code. Safe PMIC/display/audio initialization order still needs BSP source inspection and physical verification.

Board documentation: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.16 (pinout, chips, ESP-IDF examples). Checked there and in the BSP 2.0.1 header for v13: GPIO18 is the user key (10 k pull-up, low when pressed), the QMI8658 sits on the shared I2C bus with INT1/INT2 on GPIO17/21 (address 0x6B or 0x6A by SA0), two microphones feed the ES7210, and the BSP provides `bsp_i2c_get_handle`, `bsp_audio_codec_microphone_init`, `bsp_sdcard_mount` and `bsp_display_brightness_set` (CO5300 command 0x51). The docs do not describe a speaker connector.

Authoritative references: official Waveshare product documentation and schematic, official managed BSP `waveshare/esp32_s3_touch_amoled_2_16` 2.0.1, ESP-IDF 5.5.5 documentation.


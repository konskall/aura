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
  The firmware plays clips at 16 or 24 kHz, sent as 32-bit samples (below), and the radio as a continuous stream at the station's own rate (8–48 kHz). The codec is reopened in between, because the board has one output.
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

## Μάρμαρο's, Σέλας's and Γραφίτης's presences on the board (2026-11-16)

- Each presence is one opaque RGB565 frame in PSRAM, two buffers, behind every label (`shared/ui/include/aura/aura_device_frame.hpp`). The light fields under it are drawn into it, and each frame redraws only what changed.
- A frame is drawn in strips of eight rows by the frame worker (first core) and its helper (second core, below LVGL), each taking the next strip; each lane has its own rows of light. Σέλας's strips follow its band's grid (from row 134).
- A changed strip that nothing LVGL draws lies over goes to the panel straight, from the two internal DMA strip memories Synapse uses (`PanelSynapseOutput`); LVGL draws only the strips under labels and buttons. A strip LVGL is still to draw waits for LVGL's refresh; one LVGL is told to draw after it was sent goes to LVGL again; a frame left behind by a screen change sends nothing more.
- The helper sends between its strips, waiting for LVGL's lock (at most 40 ms); the worker sends what is left once the frame is drawn. Nothing tries the lock without waiting: the LVGL adapter logs every miss as an error over the 115200-baud console, about 6 ms a line, and about 60 a second had stalled the worker.
- A frame that took the worker 33 ms or longer starts the next in the tick that shows it, so the next is drawn while LVGL shows this one. Before, the frame waited for the tick after, which waited for LVGL's drawing: the two took turns.
- Frames a second, measured on COM3 with a temporary profile (removed afterwards). «Speaks» used a synthetic voice level from 0.15 to 0.85 every 600 ms.

| Look | Home | Thinking | Speaks (glow follows the voice) |
|---|---|---|---|
| Μάρμαρο | 29.5 (12 at the first flash) | 29 (8.8) | 15.5 (3-5) |
| Σέλας | 21.5 (5.6) | 18 | 11.5 (3-5) |
| Γραφίτης | no presence on Home | 29 | 17.5 |

- The microphone's level never reaches the UI on the board: `set_input_amplitude` is called only in the simulator. While listening, the glow therefore stays still and moves at the thinking rate; while speaking, it follows the playback envelope.

## Microphones and speaker on the board (2026-11-16)

Measured on COM3 with temporary firmware (removed afterwards) and Waveshare's schematic:

- The ES7210 has two microphones (MIC1 and MIC2, the top corners) and, on MIC3, the ES8311's output wired back ("AEC" in the schematic, taken before the NS4150B amplifier). MIC4 is unused. The BSP opens only MIC1 and MIC2; `Esp32Microphone` adds MIC3 through the ES7210's registers after opening (clocks, power, PGA, TDM on SDOUT1, reset) and reads four 16-bit TDM slots a frame: **0 the speaker's signal, 1 and 3 the microphones, 2 empty**. Slot 1 is the voice.
- **The ES7210 starts with about 250 ms of noise in every slot** (rms about 1100, peaks at full scale), after each start and again when the speaker's side of the shared I2S is reconfigured while it listens. The first 300 ms after a start are dropped. Before, every question began with it (its "peak 32767"), and a listen after an answer took it for speech and ended 1.4 s later with nothing said.
- Gains: the voice at 24 dB (it was 30; the questions' peaks of 32767 were the start's noise, so 24 dB is room for a voice close to the device, not a measured clip); 18 dB while the device speaks (its own voice reached 32768 at 24 dB); the speaker's signal at 12 dB (peak about 4200 at the owner's volume). Each question's levels are in the log (`question: … input peak, rms` and `rms by tenths of a second`).
- The speaker and the microphones share one I2S. Playback and recording run at the same rate (16 kHz), and a clip goes out as 32-bit samples, so the frame is 64 bits both ways and neither side reconfigures the shared clock while the other runs.
- **The echo:** the microphones hear the speaker far louder than a voice (rms about 8300 at 24 dB). Offline, from 3 s of the board's samples: the echo's response is short (99% of it within 3.3 ms), and the best linear filter of any length takes out about 12 dB (coherence 0.906 at 1–2 kHz: the speaker's and the amplifier's distortion, which the signal taken before the amplifier does not hold). The NLMS filter of `EchoReferenceGate` (128 taps) reaches that on the board: 11.5–12.2 dB and no false interruption over 8 s of speech-like sound, and 19.1 dB on a spoken answer. Below 500 Hz the speaker gives under 1% of the echo.
- **The voice** (Azure's MP3, 32 kbit/s, 16 kHz, mono) has a worker thread and a connection of its own, and plays as it arrives: the speaker starts once 1.2 s of it has been decoded, about 6 KB, three reads of the HTTPS client. Each spoken piece logs how long it played and how long it waited for the download, and each answer how long after the question its first words, its voice and its whole text came (`turn:`).

The code consumes these through `firmware/components/aura_board/include/aura_board_profile.h`; pins must not be scattered through application code. Safe PMIC/display/audio initialization order still needs BSP source inspection and physical verification.

Board documentation: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.16 (pinout, chips, ESP-IDF examples). Checked there and in the BSP 2.0.1 header for v13: GPIO18 is the user key (10 k pull-up, low when pressed), the QMI8658 sits on the shared I2C bus with INT1/INT2 on GPIO17/21 (address 0x6B or 0x6A by SA0), two microphones feed the ES7210, and the BSP provides `bsp_i2c_get_handle`, `bsp_audio_codec_microphone_init`, `bsp_sdcard_mount` and `bsp_display_brightness_set` (CO5300 command 0x51). The docs do not describe a speaker connector.

Authoritative references: official Waveshare product documentation and schematic, official managed BSP `waveshare/esp32_s3_touch_amoled_2_16` 2.0.1, ESP-IDF 5.5.5 documentation.


# ES8311 integration

Verified against the maintained Waveshare BSP `esp32_s3_touch_amoled_2_16` 2.0.1 source:

- `bsp_audio_codec_speaker_init()` initializes shared I²C/I²S and returns an `esp_codec_dev` output handle.
- The BSP configures ES8311 in DAC mode with MCLK enabled.
- Official pins are MCLK 42, BCLK 9, LRCK 45, playback data 8 and capture data 10.
- `esp_codec_dev_open`, `write`, `set_out_vol`, `set_out_mute` and `close` are the supported playback operations.
- The BSP default is mono, 16-bit, 22050 Hz; AURA explicitly opens supported clip formats at 16/24 kHz and requires board validation.

`firmware/components/aura_audio` contains the hardware backend. It does not allocate in the write loop and does not call LVGL. Static assertions compare AURA's board profile with BSP pin macros at compile time.

Firmware compilation is pending because ESP-IDF 5.5.5 is not installed in the current environment. Physical codec clocks, channel mapping, noise, underruns and speaker wiring remain unverified.

# Hardware validation checklist

Status: NEEDS HARDWARE.

- Build with ESP-IDF 5.5.5 and managed BSP 2.0.1.
- Verify GPIO46 boot behavior and that BSP mute keeps the amplifier off during boot/idle.
- Measure MCLK/BCLK/LRCK and confirm 16/24 kHz playback configuration.
- Verify mono/stereo channel routing through ES8311 and the actual speaker path.
- Test volume/mute, start/stop pop, 100 rapid cycles and idle-current reduction.
- Run 30-minute PCM/WAV playback; record underruns, heap, PSRAM and task high-water marks.
- Exercise audio while touching/scrolling LVGL and while Wi-Fi reconnects.
- Confirm ES7210 capture remains unaffected; do not claim full duplex or AEC.
- Test missing/corrupt microSD and bounded WAV rejection.
- Validate display, touch, PMIC, RTC, IMU, SD and battery behavior from the original matrix.

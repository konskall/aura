# ADR 0001: Platform and version baseline

Status: accepted, 2026-10-17.

Use C++17 for shared core/UI, LVGL 9.6.0, SDL2 2.32.10 for the native simulator, ESP-IDF 5.5.5 for firmware, and managed Waveshare BSP `waveshare/esp32_s3_touch_amoled_2_16` 2.0.1.

LVGL 9.6.0 is the current tagged 9.x line and keeps us below the BSP's declared `<10` constraint. ESP-IDF 5.5.5 is an official current stable documentation target and appears in Waveshare's first-party tested matrix. SDL2 is selected because LVGL 9.6's built-in driver and Windows guidance explicitly support it. Versions change only through a reviewed ADR plus simulator and firmware compilation.


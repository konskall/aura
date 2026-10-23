# AURA v4 asset and memory budget

Measured on the Windows native build on 2026-10-18. Device values remain unverified until hardware and the ESP-IDF 5.5.5 toolchain are available.

| Asset | Runtime dimensions | RGB565 bytes | Preview PNG bytes |
|---|---:|---:|---:|
| Home orb | 340×340 | 231,200 | 173,465 |
| Listening | 404×280 | 226,240 | 114,571 |
| Thinking | 340×340 | 231,200 | 190,955 |
| Speaking | 404×292 | 235,936 | 133,368 |
| Coast thumbnail | 138×138 | 38,088 | 33,163 |
| Coast wide | 404×270 | 218,160 | 174,807 |
| Fictional map | 448×448 | 401,408 | 333,555 |
| **Total compiled pixel data** | — | **1,582,232** | — |

- Compiled visual-asset object: 1,594,800 bytes on the MinGW native build.
- Five compiled Greek font objects: 246,404 bytes total on the native build.
- `libaura_ui.a`: 2,632,018 bytes. Native executable: 22,589,343 bytes; these include desktop/debug/runtime libraries and are not firmware flash figures.
- Editable 1024×1024 PNG sources total 12,168,527 bytes and live only under `design/generated/`; they are not compiled.
- Assets are constant RGB565 arrays. There is no runtime full-screen RGBA decode, video playback or permanent secondary framebuffer.
- UI stress: 180 view transitions, LVGL allocator 16,928 → 16,848 bytes, 1% endpoint fragmentation.

The current static asset payload is appropriate for the 16 MB flash target in principle, but firmware link size, cache behavior, PSRAM residency, dirty-region cost and device frame rate are **NOT RUN / NEEDS HARDWARE**. If device measurements fail the budget, the first adaptive tier is RLE/LZ4 conversion or a smaller map asset; the four signature states remain the visual priority.

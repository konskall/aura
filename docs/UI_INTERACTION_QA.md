# AURA v10.2 interaction QA

Date: 2026-10-19. SDL mouse events were injected through the real LVGL input device path, not by invoking callbacks directly.

| Suite | Result | Evidence |
|---|---:|---|
| Pointer coverage at 1× | 80/80 PASSED | `artifacts/v10_2/interaction/pointer-1x.json` |
| Pointer coverage at 2× | 80/80 PASSED | `artifacts/v10_2/interaction/pointer-2x.json` |
| Navigation stress | 180 transitions PASSED | LVGL allocation 20,920 → 20,872 bytes, 1% fragmentation. |
| Core/UI CTest | 6/6 PASSED | Final direct `scripts/test.ps1` run. |
| Bridge | 8/8 PASSED | UTF-8, limits, timeout, cancellation, auth boundary, concurrency and recovery. |
| Controls | 2/2 PASSED | Known bounded events accepted; invalid/oversized events rejected. |

The pointer matrix taps icon center, visible edge, label center and icon/label gap for composite tiles and rows. It covers the full Home ask pill, Quick Actions tiles and Settings gear, provider/reduced-motion/settings rows, Back/Home, notification open/clear, Audio tone/WAV/play-pause/stop/mute/volume, Speaking controls, Aoraton room/compose/preview/cancel/confirm, Offline retry, Error/Home, outside rejection, rapid-repeat debounce and real scroll gestures in Long Answer and Aoraton Chat.

Every decorative icon/label child remains non-clickable. One semantic parent owns each enabled action, so taps across the visible surface produce one callback. Disabled/unavailable routes remain inert and do not execute side effects. Aoraton send still requires explicit confirmation.

Physical capacitive-touch validation is **NEEDS HARDWARE**. Reproduce later by repeating the same four-point taps near tile edges, the 20 px side gutter, the bottom action row, scroll drags and rapid double taps on the Waveshare panel.

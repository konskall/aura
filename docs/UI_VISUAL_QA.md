# AURA v10.2 visual QA

Date: 2026-10-19. All captures come from `build/native/aura_simulator.exe` through the real LVGL 9.6/SDL pipeline at exactly 480×480; no HTML or static UI substitute was used.

## Results

| Check | Result | Evidence |
|---|---|---|
| All 18 production roles | PASSED | `artifacts/v10_2/AURA_V10_2_PRODUCTION_SCREENS.png`; 18 BMPs and PNGs under `artifacts/v10_2/screenshots/`. |
| Before/after comparison | PASSED | `artifacts/v10_2/AURA_V10_2_BEFORE_AFTER.png`. |
| Typography / Greek | PASSED | `artifacts/v10_2/AURA_V10_2_TYPOGRAPHY_PROOF.png`; three native pages. |
| Status combinations | PASSED | `artifacts/v10_2/AURA_V10_2_STATUS_MATRIX.png`; all 16 Wi-Fi × BLE × charging × DEV combinations are distinct and collision-free. |
| Hero crop | PASSED | Eight frames per state; zero bright pixels touch any four-pixel edge of the 176×176 Mercury source box. |
| Hero/text overlap | PASSED | Orb/glow remain below the 48 px header; wordmark/state titles and bottom actions do not overlap the hero. |
| Legacy subtitle / plain BLE | PASSED | UTF-8 source validator found neither legacy subtitle nor `"BLE"` label. |
| Offline / Error | PASSED | Recognizable Wi-Fi-off and warning symbols; balanced text and working actions. |

Visual inspection confirmed consistent IBM Plex baselines on answer, long answer, Settings, Aoraton, Offline/Error, Battery and Diagnostics. Home uses the Space Grotesk wordmark and has no replacement tagline. The microphone is wholly inside the pill. The normal battery fill is green; critical non-charging state is coral; the charging mark appears only adjacent to the battery within the right header cluster.

The simulator’s normal runtime clock is sourced from Windows local time and refreshed from the host loop. Deterministic evidence captures intentionally use a simulated fixed clock and battery state; the `DEV`/`DEMO` provenance remains visible.

Unresolved: physical AMOLED color, touch-edge behavior, firmware frame pacing, PMIC accuracy and device clock/RTC behavior require hardware.

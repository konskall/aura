# AURA v10 quality-assurance report

Date: 2026-10-19 (Europe/Athens)

Labels: **SIMULATOR VERIFIED** · **FIRMWARE BUILD NOT RUN** · **HARDWARE UNTESTED**

## Visual evidence

The evidence pipeline launches the real native LVGL/SDL executable for every capture; it does not render an HTML or static replacement.

- 18 production states: `artifacts/v10/AURA_V10_PRODUCTION_SCREENS.png`
- Before/after comparison: `artifacts/v10/AURA_V10_BEFORE_AFTER.png`
- Typography proof: `artifacts/v10/AURA_V10_TYPOGRAPHY_PROOF.png`
- 32-frame Mercury strip: `artifacts/v10/AURA_V10_MERCURY_MOTION.png`
- Individual exact-size captures: `artifacts/v10/screenshots/png/`
- Machine-readable results: `artifacts/v10/evidence-summary.json`

All 18 source screenshots were validated as exactly 480×480. Visual inspection covered crop, safe areas, hierarchy, Greek accents, scroll affordances, status provenance, icon consistency, controls and state identity.

## Defects corrected during v10 review

- Replaced the basic Noto type system with Space Grotesk branding and semantic IBM Plex Sans roles.
- Added combining/accent coverage and a three-page runtime glyph proof; removed the unused legacy generated fonts from the source tree.
- Replaced mixed text glyphs with one tintable semantic icon registry.
- Rebuilt Offline and Error with recognizable Wi-Fi-off and warning assets, aligned copy and working recovery controls.
- Replaced four unrelated/static AI visuals with one reusable pearlescent Mercury component.
- Corrected RGB screenshot capture: XRGB padding was previously interpreted as changing alpha by viewers.
- Narrowed the Mercury contour deformation after time-separated review exposed diagonal surfaces that looked like bitmap cropping.
- Retained bounded reading surfaces, dark Greek keyboard keys and stable status text.

## Animation evidence

Every primary state has eight distinct frames spanning 2,450 ms. The Mercury crop changes by 19,111–34,698 pixels from first to last; every adjacent pair exceeds the automated 1,000-pixel floor. Runtime logs show 14.69–15.10 Mercury FPS, a fixed 92,928-byte buffer, 1.252–1.623 ms average LVGL handler time, 7.324–11.336 ms maximum, 18,256–22,904 LVGL bytes used and 1% fragmentation.

Listening uses the explicitly labelled simulator input level. Speaking reads the real PCM envelope when audio is active. These are host observations, not firmware or physical-microphone benchmarks.

## Pointer and navigation coverage

The SDL mouse events traverse the real LVGL input path at 1× and 2× scale. The generated reports passed 80/80 checks at each scale.

Coverage includes icon center, label, circular/card edge and empty tile interior where applicable; outside rejection; rapid-repeat debounce; long-answer and chat scrolling; Home/Back routes; Settings persistence; Offline Retry; Error Retry/Home; audio controls; Aoraton compose preview/cancel/explicit confirm. Child images/text remain non-clickable, so a press is delivered once to the semantic parent.

- `artifacts/v10/interaction/pointer-1x.json`
- `artifacts/v10/interaction/pointer-2x.json`

## Automated checks

`scripts/test.ps1` performs native configure/build, six CTest binaries, bridge tests, controls tests, smoke, 180-route UI stress and the complete v10 evidence capture. `aura_ui_assets_tests` additionally validates all 74 icon roles, production Greek glyph coverage, brand glyphs, Mercury buffer size and cadence.

The authoritative final outcomes are recorded in `docs/VERIFICATION.md`; this document must not be used to infer a firmware or hardware pass.

## Outstanding hardware gates

- ESP-IDF 5.5.5 configure/link and final Flash/IRAM/DRAM/PSRAM map.
- Real display partial invalidation, touch edges and frame pacing.
- Microphone-envelope response, ES8311 playback, BSP amplifier control and simultaneous Wi-Fi load.
- Battery/thermal measurements and low-power animation policy.
- Physical Offline/Error recovery and loss-of-device behavior.

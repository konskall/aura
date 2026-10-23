# AURA v10 Mercury implementation

## Reference and implementation boundary

The visual reference is the VoiceOrbs Mercury Orb at <https://voiceorbs.vercel.app/orbs/mercury-orb>. Source commit `8b2799917734f52c9293a489e08982ca5b0187e9` was inspected under its MIT license; the notice is `shared/assets/licenses/VOICEORBS_MIT.txt`.

The reference is a browser GPU shader. AURA does not claim that React, Three.js, WebGL or the original shader runs on the ESP32-S3. `shared/ui/src/aura_mercury_view.cpp` is a purpose-built CPU/LVGL adaptation with the same pearlescent liquid-metal language.

## Rendering model

- One reusable `AuraMercuryView` and one stable 176×176 position across Idle, Listening, Thinking and Speaking.
- RGB565 color plus A8 transparency in one fixed 92,928-byte buffer.
- Integer sine lookup and bounded per-pixel flow fields; no allocation during a frame.
- Chrome luminance, localized specular highlight, cyan/violet iridescence, rim light and state-dependent fluid contour.
- Contour deformation is clamped to a narrow radial band so organic motion never reads as a cropped diagonal image.
- Only the Mercury image object is invalidated each frame; static text/status is not repainted intentionally.
- Target interval: 66 ms, approximately 15 FPS. Continuous phase prevents a visual reset between regenerated frames.

The implementation uses one CPU-generated frame rather than a pre-rendered atlas, compressed video or SD dependency. Its persistent animation RAM is exactly 92,928 bytes; LVGL object/descriptors are additional small allocations recorded by the simulator metrics.

## State mapping

| State | Motion and data source | UI contract |
|---|---|---|
| Idle/Home | Slow phase advance and gentle pearlescent flow | AURA brand, quiet helper copy, Ask/menu controls |
| Listening | Faster fluid activity coupled to the current input amplitude | `Ακούω…`, finish/cancel; simulator input is explicitly marked DEMO |
| Thinking | Fastest autonomous flow with violet halo emphasis | `Σκέφτομαι…`, no fake percent, cancel remains responsive |
| Speaking | Activity coupled to `AudioDiagnostics::envelope` from PCM playback | `Σου απαντώ…`, pause/mute/stop and real playback state |

Reduced Motion freezes optional motion while preserving the active state and controls. Route teardown detaches the image pointer before LVGL deletes the old screen, so no stale object is touched.

## Native evidence

The final simulator capture contains eight frames over 2,450 ms for every state:

| State | Mercury frames | Estimated rate | First→last changed pixels in Mercury crop | Handler average / max | LVGL used |
|---|---:|---:|---:|---:|---:|
| Home | 37 | 14.69 Hz | 34,698 | 1.615 / 11.336 ms | 19,792 B |
| Listening | 37 | 14.69 Hz | 34,540 | 1.452 / 9.073 ms | 19,296 B |
| Thinking | 37 | 14.69 Hz | 34,546 | 1.252 / 11.125 ms | 18,256 B |
| Speaking | 38 | 15.10 Hz | 19,111 | 1.623 / 7.324 ms | 22,904 B |

Fragmentation was 1% for all four samples. These are Windows simulator/capture measurements, not ESP32-S3 FPS, PSRAM, battery or thermal results.

Evidence:

- `artifacts/v10/AURA_V10_MERCURY_MOTION.png`
- `artifacts/v10/motion/{home,listening,thinking,speaking}/`
- `artifacts/v10/motion/*.gif`
- `artifacts/v10/performance/*.log`
- `artifacts/v10/evidence-summary.json`

## Hardware validation still required

The board must confirm the linked Flash contribution, 92,928-byte buffer placement, actual frame time under Wi-Fi/audio load, PSRAM/internal-heap headroom, partial-refresh behavior, touch latency, thermal behavior and battery cost. Low-battery policy may reduce the cadence, but it must retain the core state indication.

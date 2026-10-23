# AURA v6 visual QA

> Historical v6 evidence. The current verified report is [QA_V10.md](QA_V10.md).

The actual SDL renderer was captured at 480×480 for 18 representative states. The contact sheet is `artifacts/v6/AURA_V6_RECOVERY_SCREENS.png`; individual PNGs are under `artifacts/v6/screenshots/png/`.

## Review results

- Square 480×480 canvas is used edge to edge. The v4 circular bezel, circular map clipping and artificial corner mask are gone.
- Status bar is separated from content and shows host time plus explicitly `SIM` power/connectivity values in desktop mode.
- Home, Listening, Thinking and Speaking remain visually related but use distinct state-dependent motion.
- Long Greek answer text is scrollable and artwork does not cover it. The prompt preview is a one-line ellipsis rather than a clipped wrap.
- Quick Action cards have consistent 212×132 surfaces and 48+ px touch targets.
- Settings contains only working runtime controls/routes. Subtitle spacing was increased after screenshot review.
- The Aoraton keyboard was corrected from white-on-white to dark keys with legible Greek glyphs.
- Thinking originally rotated the full square source bitmap, exposing a tilted black rectangle. Rotation was removed from the bitmap; orbit particles and the bounded arc provide motion without that artifact.
- Unsupported Search, Weather, Navigation, Calendar, Camera and Voice routes render one consistent non-interactive unavailable state when invoked from developer tooling and are not enabled in product navigation.

## Animation review

`artifacts/v6/AURA_V6_MOTION_FRAMES.png` contains eight ordered frames for each required state. First-to-last changed pixels are recorded in `evidence-summary.json` and exceed the automated 1,000-pixel floor:

| State | Frames | Changed pixels in latest verified run |
|---|---:|---:|
| Home | 8 | 73,553 |
| Listening | 8 | 79,907 |
| Thinking | 8 | 64,715 |
| Speaking | 8 | 52,722 |

The numbers demonstrate that frames are not duplicates; visual inspection verifies that changes stay inside the intended visual regions with stable status text and responsive controls.

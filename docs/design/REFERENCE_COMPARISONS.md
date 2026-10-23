# AURA v4 target-versus-LVGL comparison

Evidence:

- Actual 18-screen sheet: `artifacts/screenshots/AURA_V4_ALL_SCREENS.png`
- Combined target-versus-actual sheet: `artifacts/screenshots/reference_vs_actual.png`
- Full-resolution pairs: `artifacts/screenshots/comparisons/`
- Raw 480×480 captures: `artifacts/screenshots/after/`

Scores use the v4 0–5 rubric: A silhouette/composition, B light/material/color, C typography/alignment, D affordance/interactivity, E observed simulator smoothness. They are a prioritization aid, not objective pixel similarity.

| # | Screen | A | B | C | D | E | Remaining discrepancy |
|---:|---|---:|---:|---:|---:|---:|---|
| 01 | Home | 4.5 | 4.5 | 4.0 | 4.5 | 4.5 | Generated orb is more internally detailed than reference |
| 02 | Listening | 4.5 | 4.5 | 4.0 | 4.5 | 4.5 | Reference center membrane is narrower |
| 03 | Thinking | 4.5 | 4.5 | 4.0 | 4.0 | 4.0 | Runtime vortex uses opacity motion rather than a frame atlas |
| 04 | Speaking | 4.5 | 4.5 | 4.0 | 4.5 | 4.5 | Ribbon is larger; reference has a smaller equalizer |
| 05 | Quick Actions | 4.5 | 4.0 | 4.0 | 4.5 | 4.5 | Added compact Home affordance for explicit escape |
| 06 | Notifications | 4.5 | 4.0 | 4.0 | 4.5 | 4.5 | Fixture rows use project source semantics |
| 07 | Aoraton | 4.0 | 4.0 | 4.0 | 4.5 | 4.5 | Real mock adapter currently exposes two rooms, plus honest status row |
| 08 | Navigation | 4.5 | 4.5 | 4.0 | 4.0 | 4.0 | Fictional map replaces unsupported live navigation |
| 09 | Search | 4.5 | 4.0 | 4.0 | 4.0 | 4.5 | Third setup row added for provider honesty |
| 10 | Weather | 4.0 | 4.0 | 4.0 | 4.0 | 4.5 | Lower atmospheric glow is a glass gradient rather than a wave texture |
| 11 | Settings | 4.5 | 4.0 | 4.0 | 4.5 | 4.5 | Header/back consumes more upper chord than concept |
| 12 | Audio | 4.5 | 4.5 | 4.0 | 4.5 | 4.5 | Artwork is project-generated DEMO content |
| 13 | Tools | 4.5 | 4.0 | 4.0 | 4.0 | 4.5 | Utilities route to implemented honest states |
| 14 | Calendar | 4.0 | 4.0 | 4.0 | 4.0 | 4.5 | Week grid is intentionally compact at 480 px |
| 15 | Camera | 4.5 | 4.5 | 4.0 | 4.5 | 4.5 | Disabled shutter and setup copy replace fake live capture |
| 16 | Offline | 4.5 | 4.0 | 4.0 | 4.5 | 4.5 | Uses Wi-Fi symbol rather than conceptual bell/plane mark |
| 17 | Battery | 4.5 | 4.5 | 4.0 | 4.0 | 4.5 | No unmeasured runtime estimate is shown |
| 18 | Voice | 4.5 | 4.0 | 4.0 | 4.0 | 4.5 | Provider capability replaces fictional flags/languages |

The largest fidelity gain over the previous baseline is the silhouette and optical material of the four AI states. The remaining gaps are mainly small concept-specific iconography, device-only performance validation and unsupported-service setup states, not a fallback to the rejected square dashboard language.

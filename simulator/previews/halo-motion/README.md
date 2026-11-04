# Halo motion studies

Local, interactive design proposals for the owner's selection. Open `index.html`
in Edge/Chrome, or run `scripts/preview-halo-motion.ps1` from the repository.
All assets are local; no build, server, account, microphone or internet is required.

The four directions are Aether, Silk, Crown and Bloom. The gallery shares one
clock, state and synthetic voice envelope, so motion can be compared fairly.
Click a preview to inspect it in a 480x480 composition. State controls, an automatic
conversation sequence and pause are available in the gallery and focused view.
The gallery's intensity slider controls the synthetic listening/speaking envelope.
Preference and EL/EN selection remain in this preview's browser local storage;
they do not change the simulator. Tell Codex the chosen name to commission the
production implementation.

This is a design study, not a new production appearance. Native `PresenceView`
currently exposes Idle/Listening/Thinking/Speaking and renders Halo to a 176x176
RGB565+A8 buffer at a nominal 15 FPS. This study also explores Transcribing,
Complete and Difficulty, corresponding to adjacent assistant lifecycle stages.
Aether/Bloom use browser WebGL surface shading; Silk/Crown use 2D curves. Porting
the chosen direction requires a bounded shared LVGL renderer, state mapping,
real audio envelopes, reduced-motion behavior and ESP32 memory/timing validation.
Browser smoothness is not a claim about ESP32 performance.

Motion pauses when the page is hidden, offscreen cards stop rendering, and a
focused view suspends gallery rendering. Reduced-motion preferences start the
preview paused; the owner can explicitly play it. State transitions interpolate
continuously, and Error uses a slow warm palette rather than flashing.
Visible copy lives in `shared/localization/previews/halo-motion.js`.

Verification evidence is recorded in `docs/VERIFICATION.md`. Browser QA uses an
isolated profile and synthetic state only. The production app and firmware are
not rebuilt or changed for this proposal-only deliverable.

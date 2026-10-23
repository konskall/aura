# AURA — FINAL MASTER CORRECTION PROMPT v8.0
## Production-Grade UI/UX, Real Motion, Touch Reliability, Functional Integrity & ESP32-S3 Readiness
### Paste into the existing Codex or Claude Code session — do not start a new project

## 0. Mission and authority

You are the lead product designer, LVGL graphics engineer, embedded architect, desktop-simulator engineer, accessibility engineer and QA lead for the **existing AURA AI Assistant** repository. This is a **corrective implementation assignment**, not a new project, visual-concept exercise, or invitation to invent features.

**Target hardware:** Waveshare **ESP32-S3-Touch-AMOLED-2.16**, **480 × 480 square AMOLED touchscreen**, ESP32-S3, 8 MB PSRAM, 16 MB flash, Wi-Fi, Bluetooth LE, battery-equipped variant, existing board audio components (ES8311 playback, ES7210 capture, amplifier control subject to schematic/BSP verification), optional microSD. The physical board has **not yet arrived**. The **Windows 10 native LVGL/SDL simulator** is the primary executable acceptance target.

**AI during development:** existing Windows Codex CLI bridge, where already implemented. Do not attempt to run the Codex CLI, Claude Code, ChatGPT or a large LLM on the microcontroller. Keep cloud provider contracts optional and securely isolated from device firmware.

**Existing messaging product:** Aoraton, public address `https://aoraton.com`; local source at `C:\Users\polis\Documents\incognitochat`. Treat the Aoraton source as **read-only** unless the user separately authorizes changes. Preserve current AURA messaging work and the possibility of future real, end-to-end encrypted interoperability.

**This prompt supersedes conflicting prior UI styling and animation-free instructions**, but it does NOT supersede working functional requirements of the original seed, audio amendment, or Aoraton integration requirements. Retain established features. Do not interpret prior illustrative mockups as screen-shaped bitmaps to paste into the UI.

**The user-reported failures to solve** are: hero images badly clipped at bottom/right; Home, Listening, Thinking and Speaking dominated by static posters rather than true animation; poor or static-looking status bar; inaccurate `SIM`/cellular labels; circular smartwatch framing on a square display; inconsistent iconography; elements that respond only when the label is pressed but not the icon; dead actions, misleading demo features, impaired navigation, and loss of previously working features.

**Product outcome:** an elegant, striking **premium AI companion**, with living neon cyan/blue/violet/magenta motion graphics, excellent Greek typography, correct square-screen layouts, functional touch-first navigation, truthful system state, working AI/audio workflows, and an appropriately integrated Aoraton experience.

No fake completion. A successful build alone is not success. A single attractive screenshot is not success.

---

## 1. Safety first: audit, baseline and preserve working code

1. Read the current repository's instructions, original seed, later amendments, changelog, progress documents, build scripts, `sdkconfig`, selected LVGL version and simulator architecture.
2. Record Git status, current branch, recent commits and working-tree changes. Preserve uncommitted work. Create a non-destructive safety branch or patch/backup using appropriate Git operations. **Do not use `git reset --hard`, `git clean -fd`, force-push, or destructive restoration.**
3. Build and launch the **current** simulator, if possible. Capture a baseline of its actual runtime, including logs and screenshot evidence. Do not assume images provided by the user represent the currently running revision if the code has changed.
4. Inventory every existing screen, control, action, event callback, navigation route, service, application state and test. Classify capabilities as: `REAL/WORKING`, `SIMULATED/WORKING`, `PARTIAL`, `DISABLED`, `BROKEN`, `NOT IMPLEMENTED`.
5. Trace current AI flow, audio playback, settings persistence, notifications, offline behavior, Wi-Fi/BLE, battery representation, Aoraton mock/real mode, diagnostics and any genuinely implemented additional tools.
6. Write a short change-impact plan and functional-parity matrix. Use it to prevent deletion or regression of working features.
7. Do **not** replace the application with a fresh template, different framework, HTML prototype or pure image viewer. Work inside the existing real LVGL components and native simulator.

Execute the repairs after this bounded audit. Do not stop with a plan unless blocked by missing files or permission.

---

## 2. Design source of truth and square-screen composition

The previously supplied futuristic 18-panel neon concept is the **visual-language reference**: deeply black AMOLED background, luminous blue/cyan/violet/magenta energy, crisp Greek/English typography, polished dark surfaces, clear hierarchy and subtle glow. It is **not** a requirement to copy the reference's physical circular smartwatch outline; the real Waveshare screen is **square 480 × 480**.

Use any concept reference assets that already exist in the repository after verifying their provenance. If the user's latest 18-screen screenshot or older visual reference is not present as a file, report the missing reference and derive a baseline from the app itself; do not invent image paths or silently substitute unrelated graphics.

Mandatory design constraints:

- **Remove the giant circular ring/frame/mask** present around entire screens. Background extends naturally to all four edges of the square AMOLED.
- Establish an explicit square layout grid, responsive content regions, safe edges, reusable padding, fixed status bar geometry and content bounds. Account for 480 × 480 native size and simulator scaling.
- Keep interactive controls within the usable display; no right-edge or bottom-edge cutoffs.
- Center hero animations by **their actual visual bounds** (including bloom/glow), not merely their image canvas. Verify in screenshots.
- Do not put a rectangular black poster/background-image panel beneath the animation. The hero effect itself must be dynamically rendered or dynamically frame-animated; background is a deliberately constructed LVGL scene.
- Preserve text contrast. Greek glyphs, ellipses and wrapping must be accurate; load a font with full Greek coverage and enforce a consistent typographic scale.
- Build a coherent set of tokens: colors, glow/accent intensities, radii, spacing, icon stroke weight, typography, states, touch sizes, transitions.
- Do not oversaturate every component. Strong visual energy belongs to the main AI visual; utility screens prioritize usability and information density.
- Avoid smartphone conventions that do not apply to this embedded device.

Sample layout guidance, to refine through screenshots rather than blindly hardcode: roughly 40–48 px for the shared status region, deliberate 12–20 px horizontal padding for content, minimum 44 × 44 px touch targets, adequate bottom safe area, and a flexible central hero region. Never force oversized hero art into content reserved for buttons or text.

---

## 3. Rebuild the first FOUR AI states as genuinely animated interfaces

This is the **highest-priority visual correction**. The existing versions look like clipped static artwork with small decorative UI overlaid. That is rejected.

A valid solution can be **procedural LVGL primitives/canvas rendering**, **animated pre-rendered frame sequences**, or a carefully optimized hybrid; do NOT forbid animated assets if they achieve much higher visual quality. The core requirement is that **the hero itself changes over time across meaningful areas of the composition**, not a static promotional image with a little spinner or a few moving bars added above it. No full-screen static poster may masquerade as the animation.

### 3.1 Home / Idle — Living AURA presence

- A centered luminous blue/violet/magenta orb; premium translucent energy/ribbon aesthetic inspired by the reference.
- Real visual changes over time: flowing ribbons, evolving internal highlight/refraction, subtle breathing, living glow and/or dynamically composed frames.
- Motion restrained when idle, more expressive when transitioning to Listening or Speaking.
- Correct aspect ratio and safe bounds throughout all frames; glow should not abruptly cut off on the bottom or right.
- Integrate `AURA`, the short Greek subtitle and one clear **working** input affordance. No overlap between hero and the input control.
- If voice capture is unavailable, the affordance must lead to a genuinely working typed-input or explicit simulation flow, not pretend that the microphone works.

### 3.2 Listening — Living sound field

- A true responsive waveform, ribbons, or energy visualization. Not a static screenshot of a waveform.
- If real microphone capture is active, drive envelope/reactivity from measured incoming PCM/audio level, smoothed and bounded. If it is not active in the simulator, provide a clearly labelled `SIMULATED INPUT` or `DEMO` mode with deterministic, convincing dynamic samples and a route to actual typed-input testing.
- Display `Ακούω…` and an accurate input-state explanation; show Start/Stop/Cancel only if handlers exist.
- Do not render a permanent static screenshot with three blinking dots. The main waveform must evolve.

### 3.3 Thinking — Actual dynamic computation presence

- A centered evolving vortex/spiral/particle field or an equally polished alternative derived from the reference's visual language.
- Meaningful temporal progression: controlled rotation, particle movement, energy trails, layers and visual evolution. It must not simply be the original static vortex poster with a small spinner over it.
- Use a cancellable processing state connected to the real async AI request when available.
- Show `Σκέφτομαι…` and concise secondary text without covering essential visual elements.
- Animation stops or transitions correctly after answer, cancellation, timeout or error.

### 3.4 Speaking — Reactive luminous energy

- A distinct moving energy/orb scene plus actual audio-reactive waveform or bars.
- While PCM playback is available, use a time-smoothed RMS/peak envelope from the playback pipeline. If audio/TTS is not available, present a truthful fallback: text response or labelled demo playback.
- Synchronize visual state with `PLAYING`, `PAUSED`, `MUTED`, `STOPPED`, `COMPLETE`, and `ERROR` events. Never show `Speaking` indefinitely after sound stops.
- Keep play/pause/stop/mute controls reachable and correctly bound to audio actions.

### Shared implementation requirements for the four states

- Create **one reusable motion/render architecture** with four intentionally different behaviors, rather than four unrelated hacky bitmap screens.
- Use the existing LVGL version's native animation/timer APIs correctly, or bounded animated asset-frame decoding where appropriate.
- Do not block the UI or the audio thread. Minimize allocations per frame. Manage start/stop when screens become active/inactive.
- Clamp all draw coordinates, object sizes and glow/particle extents. No hardcoded positive x/y offsets that make the visual look centered only by accident.
- Verify first, middle, and late frames are correctly composed; do not accept one screenshot as evidence of motion.
- If the target look requires pre-rendered frames, store suitably optimized sequences in flash/microSD as appropriate, stream/decode using bounded buffers, verify licensing, and document cost. Avoid pretending the ESP32 can render an unoptimized desktop shader at full resolution.
- Visually comparable simulator and hardware backends should share the animation state model; identify any deliberate rendering-quality degradation on device.

---

## 4. Performance requirements for genuine animation

Create a measurable graphics-performance budget for the ESP32-S3 and the simulator. Avoid vague claims such as "ultra smooth" without measurements.

- Record renderer choice, frame update rate, worst and average frame time, heap/PSRAM use, draw buffer dimensions, transferred pixels, CPU burden and potential DMA constraints.
- Target perceived smoothness; start with **24–30 FPS for actively animated hero regions on the simulator**, then measure hardware capabilities and adapt. Do not promise hardware frame rates without testing.
- Keep status text and inactive UI **pixel-stable** while motion continues. Do not redraw the whole screen every frame unless measured as acceptable and strictly necessary.
- Prefer dirty rectangles, small bounded sprites, cached static layers and compositing of dynamic regions. If frame-based animation is used, update only the hero's rectangular bounds when possible.
- Be aware a single 480 × 480 RGB565 framebuffer takes roughly **450 KiB**; preserve RAM and avoid unnecessary full-size frame copies.
- Ensure all active animations can be paused in background screens and power-saving modes.
- Lower or stop animation when screen is off; avoid unnecessary battery drain.
- Guard rendering against missing assets/SD card, decoder failures and low memory with a visually sound fallback.
- No leaking LVGL objects, timers, image buffers, animation instances or event registrations during repeated screen transitions.

---

## 5. Entirely new, accurate premium STATUS BAR

Delete the old visual implementation of the top header and rebuild it as a reusable LVGL status component used by all relevant screens.

**This board has Wi-Fi and Bluetooth Low Energy. It does NOT have a cellular modem or SIM.** Remove all UI code, labels, mocks, screenshot fixture values and asset references that say `SIM`, `carrier`, `cellular`, `mobile signal`, or imply a phone network.

Display genuine device-appropriate status:

- **Left:** Wi-Fi status icon with connected/disconnected and possibly RSSI states; Bluetooth/BLE icon only when meaningful and driven by state.
- **Center:** current local time, legible with deliberate font weight and optical alignment.
- **Right:** battery icon plus measured or clearly simulated percentage, plus charging icon when true.
- Optional mute or notification dot only when it communicates a real state and fits.

Data-source rules:

- **Simulator time** from the host clock, regularly refreshed. **ESP32 time** from RTC/SNTP when available. On unsynchronized clock, show a truthful unknown/syncing state rather than a hardcoded clock.
- **Simulator battery** may be a controlled simulation, clearly labelled in developer mode. **Hardware battery** should use confirmed PMIC/battery telemetry (investigate AXP2101/BSP). If percentage cannot be estimated reliably, show an unknown/approximate indicator instead of inventing `92%` or `78%`.
- **Wi-Fi and BLE** indicators must reflect real platform state or a clearly labelled simulator state; no hardcoded connectivity claims.
- No overlapping text, clipped icons, excessive tiny writing or redundant status labels.
- Use one consistent icon family with appropriate pixel-aligned linework, not random Unicode/emoji glyphs.
- Test disconnected Wi-Fi, BLE off, charging, low battery, unknown battery and unsynchronized time.

---

## 6. Touch-first interaction: icon, label and whole tile must work

The 480 × 480 AMOLED is a **capacitive touch display**. Every displayed active action must be genuinely usable with a finger, not just with precise mouse clicks on labels.

Observed bug: tapping the circular icon often fails while tapping the text beneath works. Treat this as a root-cause interaction defect.

- Audit every LVGL object tree, event target, parent/child relationships, overlay, `CLICKABLE` flags, event bubbling, z-order, hit testing, scroll behavior, opacity and transparent-image interception.
- Give each action tile **one actionable parent** containing the icon and label. The visual icon, label, margin/gap and entire visible card should map to the same handler, **exactly once**.
- Do not allow decorative images, full-screen overlay objects or invisible controls to intercept gestures.
- Establish practical minimum tap targets around **44 × 44 px** or larger, subject to layout; leave space between adjacent actions to prevent accidental touches.
- Use immediate pressed/selected visual feedback and clear disabled/working states.
- Ensure all scrollable lists, answer cards, chat conversations and settings rows accept swipe gestures naturally. Back navigation must be accessible, predictable and consistent.
- Do not bind navigation to `LV_EVENT_PRESSED` if that causes duplicate actions; choose the correct event semantics for the installed LVGL version.
- Simulator pointer integration must mirror touch behavior, including press, release, drag and scrolling.
- Test icon center, edge, label center, and gap between icon/label for **every actionable tile**, not only Quick Actions.
- Automated tests must inject actual pointer/touch events into LVGL or the simulator input path; calling route functions directly is not enough to test hit areas.

No dead controls. No invisible hit targets far away from what the screen visually advertises.

---

## 7. Route integrity and strictly truthful functionality

Before UI changes, build an **Action and Route Registry** for all currently available features. For each action record: screen, visible label/icon, event object, expected handler, resulting route/action, backend dependency, implemented state, test ID and result.

**Rules:**

1. `ENABLED` means the visible control performs a real, meaningful operation or deliberately useful, clearly marked simulator operation.
2. `SIMULATED` controls must actually execute their mock scenario and be marked `DEMO` in a tasteful consistent manner; not merely display decorative content.
3. `UNAVAILABLE` features must be visually disabled or omitted; never pretend they work.
4. `ERROR/UNAVAILABLE` must not be reported as success.
5. Do not add new icons to fill space. Do not reintroduce GPS, camera, navigation, cellular or other features unless the code actually supports a useful mode or they are clearly disabled/labelled mock features already part of the existing contract.
6. Do not remove existing implemented features while redesigning the UI.
7. Every reachable screen must have a logical way back or home and must preserve correct navigation stack state.
8. Avoid circular trap navigation, broken dialogs, duplicated event handling and misleading toast messages.

Create route tests covering the full user journey from Home through all enabled screens and back. Identify actual functionality rather than inferring it from a screenshot.

---

## 8. Screen-by-screen acceptance: current EIGHTEEN screens

Use the current actual code's screen IDs; map them to the functional set below rather than inventing duplicates.

| # | Screen | Mandatory behavior and corrections |
|---|---|---|
| 01 | Home | Centered **genuinely animated** AURA orb; Greek typographic hierarchy; working input CTA; accessible secondary action; no crop/huge ring. |
| 02 | Listening | Animated/reactive sound field; truthful live vs demo input indicator; working stop/finish/cancel; no wallpaper waveform. |
| 03 | Thinking | True evolving vortex/particles, cancellable async processing; centered effect; meaningful transition to Answer/error. |
| 04 | Speaking | Actual moving/audio-reactive hero, synchronizes with playback; working playback controls, sensible fallback if no TTS. |
| 05 | Answer | Proper question/answer hierarchy, readable Greek, a real new-question action and back navigation. |
| 06 | Long Answer | Text wraps cleanly; **scroll really works**; no clipped lines; return/read-aloud actions only if supported. |
| 07 | Quick Actions | Only implemented/working/simulated-labelled actions; unified icon+label tile touch region, functioning routes. |
| 08 | Notifications | Useful timestamp/status, clickable actionable rows, dismiss/clear behavior, no misleading demo data. |
| 09 | Settings | Real persisted settings and truthfully disabled options; functional sliders/switches/subscreens; consistent icons. |
| 10 | Audio | Existing **working** PCM/WAV/speaker tests, play/stop/volume/mute controls, state feedback and visualizer. |
| 11 | Aoraton Rooms | Clearly marked mock vs real backend, real room list interactions, unread badges, no production-data claims. |
| 12 | Aoraton Chat | Actual scrollable messages from selected backend, navigation, sender/room context and compose route. |
| 13 | Aoraton Compose | Working text entry with simulator input, send draft and validation; correct touch keyboard behavior. |
| 14 | Aoraton Confirm | Correct recipient+exact message, cancel/confirm actions and send result; never send without explicit confirmation. |
| 15 | Offline | Accurate disconnection reason; retry performs meaningful retry; alternate route works. |
| 16 | Error | Error message corresponds to real failure; retry/cancel routes function; consistent icon and spacing. |
| 17 | Battery | Genuine or labelled-simulated status, charging indicator, battery visuals and refreshed values; no fixed invented percentages. |
| 18 | Diagnostics | Readable real simulator diagnostics; distinguish host metrics from unmeasured device metrics; no fake hardware claims. |

Preserve any **additional genuinely implemented screens/features** discovered in the repository (e.g., search, weather, tools, Wi-Fi/BLE configuration). Do not assume a previous screenshot proves those capabilities currently exist. Maintain access to implemented functionality through coherent navigation.

Make the first four screens the standout visual experience; make the remaining fourteen the polished, efficient everyday interface around it.

---

## 9. Icons, typography and microinteractions

Perform a controlled icon audit. Use one coherent vector or raster icon system suitable for LVGL, with consistent stroke weight, optical size and alignment. Replace semantically wrong glyphs (for example, a generic music note used for an unrelated feature), low-resolution imagery, mismatched fill/outline styles, awkward shadows and clipped icon bounding boxes. Prefer a vetted embedded icon/font pipeline with licensed assets.

- All icons should represent a meaningful feature/state; color is semantic and restrained.
- Correct Greek accents, line breaking, font metrics, kerning where supported and casing.
- Align baselines and vertically center icons within their actual hit area.
- Use consistent back/home/close metaphors.
- Create pressed, selected, loading, muted, disabled, error and success states with distinct feedback.
- Motion for interactions (short fades, state transitions) is allowed and encouraged when it does not interfere with input. Do not let decorative animations alter hit coordinates or steal pointer focus.

---

## 10. Audio engine and AI lifecycle must remain operational

Preserve and, where broken, repair the existing Audio Service established by the earlier audio-upgrade specification:

- Desktop simulator: **real host audio playback** via supported SDL/Windows output, WAV/PCM testing, play/pause/stop, volume/mute and diagnostics.
- ESP32 backend: ES8311 over I²S, relevant codec/I²C/BSP wiring, DMA-compatible buffers, bounded streaming and controlled amplifier power. `PA_CTRL` has been documented on GPIO46; **verify schematic, BSP, polarity and ESP32-S3 GPIO46/boot-strapping limitations before touching real hardware**. Do not assume this pin can be freely driven as a generic output.
- Validate whether a **physical speaker** is present in the purchased board assembly rather than inferring it from `audio playback` marketing text.
- Speaking visuals reflect the actual audio state and preferably PCM amplitude.
- Keep voice recognition and TTS capability honest: the Codex CLI supplies text, not automatic voice synthesis; TTS is a separate provider, and a Greek Windows voice may not be available.
- Do not turn mocked audio into a fake “working speaker” success indicator.
- Preserve cancellation and asynchronous UI responsiveness through AI request states: Idle → Listening/Input → Thinking → Answer/Speaking → Idle, with failure/offline routes.
- Keep the local Codex bridge **host-only**, properly isolated, with no inbound command execution from untrusted Aoraton messages or network text.
- Do not hardcode cloud API keys in firmware.

---

## 11. Preserve secure Aoraton flows without inventing new ones

The current project contains Aoraton mock screens and may have partially implemented transport. Preserve those flows through UI changes.

- Inspect current AURA Aoraton components and, when needed, review `C:\Users\polis\Documents\incognitochat` **read-only** to understand actual room, Firebase, encryption, messaging and notification schemas.
- Do not modify Aoraton production code, production database, secrets or security rules without separate authorization.
- Display `SIMULATED` / `DEMO` cleanly when using fixtures; no fake real connectivity.
- Preserve rooms → chat → compose → confirm → send result → notifications navigation.
- Keep Aoraton E2EE intact; no plaintext secrets on the device/SD and no decrypted chat content forwarded to an AI provider without specific consent.
- Treat incoming messages as untrusted data, never as executable AI/system instructions.
- Real message send requires explicit confirmation, deterministic result and duplicate-prevention/retry discipline.
- Do not pretend native WebRTC calling is implemented because an icon exists. Calls remain an explicitly future/experimental capability until actually proven.

If real interoperability is not yet established, maintain a high-quality working simulator mock with explicit truthful labeling and leave a clean adapter interface for a real backend.

---

## 12. Simulator-first execution and controls

The Windows simulator is **the actual product-development environment**, not a throwaway static showcase. It must use the real LVGL UI code used by the firmware and support mouse-as-touch interaction, keyboard text input, controlled state changes and debug instrumentation.

Provide a dedicated developer-only simulator control panel or CLI test harness (separate from the production device UI) for:

- Triggering Idle / Listening / Thinking / Speaking / Answer transitions.
- Supplying real typed questions and showing a genuine Codex response when available.
- Simulating microphone envelope/wake word **clearly as simulation**.
- Injecting audio playback/TTS events and measuring speaking response.
- Toggling Wi-Fi connected/disconnected and Bluetooth state.
- Simulating battery charge/low battery/unknown battery while host time continues to update.
- Injecting mock Aoraton notifications/messages and simulating send confirmation/retry/errors.
- Triggering offline, error, scroll-heavy answer and long-label stress cases.
- Capturing deterministic screenshots and multi-frame sequences.

Simulator tests must not require live production credentials, an API subscription or physical hardware. Where an external AI/voice service is unavailable, offer a **functional offline test path** without showing false network success.

---

## 13. Explicit proof of visual animation (not static posters)

For EACH of Home, Listening, Thinking and Speaking:

1. Run the app and activate that state.
2. Capture at least **8 distinct frames over 2–4 seconds** at stable viewport and state.
3. Build a contact sheet or short video/GIF where feasible, using supported repository tooling.
4. Programmatically quantify frame differences in the **hero region**, not only changed timestamps, progress circles, blinking labels or status bar icons.
5. Visually inspect the changing hero geometry and glow/energy structure over time.
6. Confirm the animation is centered and never clips at the right, bottom or other unintended bounds, across all sampled frames.
7. Verify pause/deactivation, scene switching and repeated visits do not leave orphan timers or continue updating hidden screens.
8. Document implementation style (procedural, sprites, frame sequence, hybrid), frame rate, CPU/memory measurements and the hardware trade-offs.

**Rejection case:** screenshot A and screenshot B share a completely identical main orb/wave/vortex and only small indicator elements differ. That is not a valid solution to the animation requirement.

The correct response to a performance limitation is to optimize the animated asset sequence or dynamic hero region, **not quietly fall back to the old static poster**.

---

## 14. Functional and touch verification — actual events, not mock assertions

Create a complete `UI_INTERACTION_MATRIX` covering all active controls on all current screens. It must include screenshot/screen, widget identity, visible target rectangle, expected event, expected action/route, run mode and test result.

For every enabled tile/button test:

- Tap at **icon center**.
- Tap at **icon edge / visible button edge**.
- Tap at **label center**.
- Tap at **space between icon and label**.
- Ensure each results in the same correct action **exactly once**.
- Verify press feedback, release/cancel and rapid repeat behavior.
- Verify a tap outside the visual target does not trigger the action.
- Verify overlays never intercept touches.
- Verify scroll/swipe does not accidentally activate buttons.

Add automated pointer injection or simulator-driven interaction tests. Use route-function-only tests as supplementary coverage, not a substitute.

Representative end-to-end flows:

1. Home → start input → Listening → Thinking → Answer → new question → Home.
2. Home → Quick Actions → each enabled feature → Back/Home.
3. Settings → alter actual available setting → leave/reopen → setting persists.
4. Audio → test sound → volume/mute/stop → corresponding status/visual update.
5. Aoraton mock Rooms → Chat → Compose → Confirm → Send → unread/notification update; cancellation does not send.
6. Trigger Wi-Fi loss → Offline → Retry → truthful result.
7. Error screen → retry/back; no dead ends.
8. Long answer and conversation scroll on a 480 × 480 touch viewport.
9. Status bar shows time changes and reflects modified simulated battery/network state.

No enabled control without verified handling. No misleading text describing an unimplemented capability as working.

---

## 15. Quality gates, priority and workflow

Implement in **small, testable increments** with screenshots and interaction checks after each stage; do not rewrite all eighteen screens in a single unverified batch.

### Gate A — Audit and preservation
- Inventory features, screens, routes, audio/AI/Aoraton services.
- Git backup and clean execution plan.
- Baseline screenshots and failure reproduction.

### Gate B — Accurate device shell
- Remove giant circular screen ring.
- Rebuild square-layout tokens and status bar.
- Remove ALL SIM/cellular references.
- Fix real/simulated state and fonts/icons.
- Run all existing smoke tests.

### Gate C — Touch and navigation recovery
- Fix object-tree hit areas, icon/label tile events, scroll and back navigation.
- Validate route matrix; remove misleading dead links.
- Prove no regression on existing features.

### Gate D — The four living AI scenes
- Replace static hero posters with genuine animated hero implementations.
- Fix position, bounds, clipping and responsive composition.
- Add AI/audio state-driven behavior; validate multi-frame sequences.
- Measure host performance and hardware budget.

### Gate E — Utility screens and polish
- Refine answers, long answers, quick actions, notifications, settings, audio, Aoraton, offline, error, battery and diagnostics.
- Fix icon coherence, spacing, Greek typography and accessible touch targets.
- Avoid meaningless features or cosmetic-only demo widgets.

### Gate F — Proof and release-quality report
- Rebuild simulator; execute runtime user journeys, interaction tests and audio checks.
- Capture **all 18 screen screenshots**, plus animation frame sequences for the first four.
- Compile ESP32 target if the toolchain is available; otherwise report explicitly that it was not built.
- Run memory/performance/static checks and document hardware-only validation required later.
- Produce honest before/after comparison and list all remaining limitations.

If a gate fails, **fix it before proceeding**, rather than adding more screens or replacing functioning services.

---

## 16. Required artifacts in the repository

Adapt to existing documentation structure; do not duplicate documents needlessly. Deliver:

- `docs/UI_AUDIT_AND_FEATURE_PARITY.md` — found screens/features, preserved/broken/disabled states.
- `docs/UI_DESIGN_SYSTEM.md` — square AMOLED layout, typography, palette, icon system, touch sizes.
- `docs/ANIMATION_ARCHITECTURE.md` — render approach, state engine, assets, memory/performance.
- `docs/STATUS_BAR_DATA_SOURCES.md` — Wi-Fi/BLE/time/battery, simulator vs real hardware.
- `docs/UI_INTERACTION_MATRIX.md` — all enabled controls and actual pointer tests.
- `docs/UI_VISUAL_QA.md` — 18 screen image checks and 4 motion evidence sequences.
- `docs/RELEASE_VALIDATION.md` — what was truly run/built/tested and what remains unverified.
- A deterministic way to launch simulator, execute interaction tests and capture screenshots.

Store screenshots and frames in a clearly named local artifact folder; do not check in enormous binary sequences unless the repo conventions support it.

Provide exact commands required on Windows 10 to run simulator/tests and view evidence. Prefer existing build system and avoid unnecessary extra toolchains.

---

## 17. Strict completion and rejection criteria

**This change is NOT complete if ANY of the following remain:**

- Home/Listening/Thinking/Speaking still use a largely unchanging, cropped static hero wallpaper.
- Hero artwork is clipped at right/bottom unintentionally or visibly off-center.
- The circular smartwatch frame remains across square screens.
- `SIM`/carrier/cellular status appears anywhere in the UI or mock output.
- Clock/battery/network states are hardcoded and represented as real.
- Header looks like tiny leftover text instead of an intentionally designed status component.
- An enabled icon fails when tapped while the label beneath succeeds.
- Any enabled action leads nowhere, routes to the wrong feature, or double-fires.
- Working features present before redesign have disappeared or become inaccessible.
- Aoraton demo data is presented as real production data.
- Audio controls are decorative rather than bound to actual playable host audio (where existing implementation supports playback).
- The agent submits only screenshots and build results, without proof of interaction and frame-by-frame motion.
- Hardware performance, speaker presence or battery telemetry are claimed verified without the physical device.

**Completion requires:** stable functioning simulator, premium square UI, verifiable four-state animations, truthful live/simulated device state, reliable touch events and route integrity, retained AI/audio/Aoraton functionality, and honest test evidence.

---

## 18. Final execution command

Inspect the current repository, preserve work safely, and IMPLEMENT the corrections now.

Do not stop after a plan. Do not make a static HTML mockup. Do not replace missing functionality with decoration. Do not paste giant precomposed posters over broken layouts. Do not casually add features. Do not regress existing services.

Work methodically through Gates A–F. Run the real native simulator, interact with the screen using the actual input path, capture screenshots and animation sequences, inspect problems and iterate. Build firmware when possible without pretending that physical hardware exists.

At the end, provide a concise factual report with:

1. Files modified and why.
2. Features preserved/restored.
3. Exactly what each of the four living AI hero animations now does.
4. Status-bar data sources and how SIM artifacts were removed.
5. Number of actionable controls actually tested and any remaining failures.
6. Simulator build/run commands and tests executed with pass/fail results.
7. Locations of 18 screenshots and 4 multi-frame animation evidence sets.
8. Measured simulator performance and estimated/verified device constraints.
9. ESP32 firmware build status and physical tests still pending.
10. Any blocked, unsupported or deliberately disabled feature, without exaggeration.

**Do the real engineering work. Preserve functionality. Deliver a premium, truly animated, touch-first AURA, not an attractive but nonfunctional collage.**

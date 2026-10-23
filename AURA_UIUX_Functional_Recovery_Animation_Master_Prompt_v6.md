# AURA — UI/UX FUNCTIONAL RECOVERY & MOTION SYSTEM v6.0
## Authoritative implementation prompt for Codex CLI / Claude Code

**Project:** AURA — Advanced Universal Responsive Assistant  
**Target:** Waveshare ESP32-S3-Touch-AMOLED-2.16, square 480×480 AMOLED  
**Development environment:** Windows 10, native interactive LVGL simulator, ESP-IDF firmware  
**Existing AI in development:** Local Codex CLI through a PC-only bridge  
**Future integration:** Aoraton encrypted messaging, source at `C:\Users\polis\Documents\incognitochat`

---

## 0. YOUR ROLE AND THE CURRENT FAILURE

Act as principal embedded UI/UX architect, senior LVGL engineer, interaction engineer, motion designer, firmware performance engineer, and QA lead. Work **inside the existing AURA repository**, not in a replacement demo.

Recent UI redesign attempts introduced serious regressions. The user reports that icon buttons are not clickable while their labels are, some buttons have no meaningful destination, previously working features disappeared, static concept artwork was pasted in place of actual animations, images were off-center, a huge circular border was applied to a square display, and time/battery indicators remained frozen. **These reports are confirmed defects to investigate, not artistic preferences to reinterpret.**

The goal is a **beautiful, responsive, animated, reliable, real product UI** with demonstrably correct behavior. This prompt supersedes prior contradictory instructions that disabled animations or required circular smartwatch framing. Retain the valuable neon/cyan/violet visual direction of the user's reference image, but make an interface genuinely designed for a **square 480×480 touchscreen**.

**No new speculative features. No fake navigation. No static poster-style screens. No loss of existing working capabilities.**

## 1. NON-NEGOTIABLE RULES

1. First inspect the real repository, branch, uncommitted changes, build system, LVGL version, screens, routing, AI, simulator, audio, storage, Aoraton work, and tests. Do not claim anything exists without finding it.
2. Preserve the user's work. Record `git status`, protect uncommitted work, and create a reversible recovery point/branch or patch. Never use destructive Git commands.
3. Before editing, write `docs/ui/FEATURE_INVENTORY.md`: every existing feature, its actual implementation status, old route, current route, and whether it worked before the redesign. Determine last known working behavior from Git history/tests where possible.
4. Compare the latest UI against existing screenshots and the user-provided concept. The concept specifies **visual style**, not screen shape, real hardware capabilities, or proof a feature works.
5. **Only working actions may appear enabled.** For incomplete features, either hide them or show clearly disabled `Unavailable / Not yet implemented` states, never a misleading clickable control.
6. Every visible enabled control must respond over its **entire intended touch area**, exactly once, and perform a real action. No label-only click targets, invisible interception, or dead end routes.
7. Restore lost functional screens/routes before adding visual refinements. Do not replace business logic with mocks to make screenshots look complete.
8. All screens remain part of one consistent component system. No unrelated UI frameworks, no HTML facsimile, no screenshot placed as an entire screen.
9. Animations are **required**: live, state-dependent, smooth, and intentionally integrated. Static images may be assets but never substitute for the requested motion.
10. Run real simulator interaction tests and capture the simulator's actual 480×480 output. Compilation alone is never a pass.
11. Keep a truthful separation between `REAL`, `SIMULATED`, `NOT AVAILABLE`, and `UNVERIFIED ON HARDWARE`.
12. Never edit/deploy the separately maintained Aoraton production application without authorization. Its actual repository is read-only for this task unless specifically approved.

## 2. START WITH A FACTUAL AUDIT AND BASELINE

Inspect:
- App entry point, simulator window/event loop, SDL or other backend, and LVGL initialization.
- All screen objects, hierarchies, `lv_obj` creation, layout, styling, event binding, routes, back stack, cleanup, and timer lifetimes.
- Input-device driver, pointer coordinate mapping, simulator scaling, click/press/release semantics, touch calibration abstraction, and clipping.
- Any decorative transparent overlays, images, or full-screen containers which may capture pointer events.
- Existing AI service and real Codex bridge, network, audio playback/TTS, wake-word simulation, notifications, settings, status bar, Aoraton, weather/search utilities.
- Existing image assets and memory footprints, design token implementations, localization and fonts.

Create a feature parity table with columns:
`Feature | Was available | Exists now | Trigger | Actual result | Defect | Fix | Test`. 

Identify all additions that exist **only as icons or empty pages**. Do not classify them as implemented.

If you cannot inspect the visual reference file locally, ask for its repository path or state the limitation; don't hallucinate a visual comparison. Relevant user feedback remains authoritative: square screen, no full-screen circle, functioning touch targets, proper animations, honest feature availability.

## 3. REAL DEVICE DESIGN LANGUAGE — SQUARE, NOT ROUND

The reference direction is premium AMOLED black with luminous electric-blue, cyan, deep violet and restrained magenta. Maintain strong contrast and careful typography for Greek and English.

Absolute layout rules:
- **480×480 square design canvas**, no circular viewport/frame, no full-screen watch bezel, no artificial corner clipping.
- Use the complete square display intentionally; appropriate internal margins, usually 12–20 px, with flexible safe areas.
- Standard reusable status region (when applicable), flexible content region, contextual controls. The status bar must not overlap content or decorative graphics.
- Logical vertical rhythm, baseline alignment, deliberate whitespace, consistent corner radii, icon sizes, spacing, shadows, and typography.
- Use a shared design token system: background, surfaces, border, foreground, muted foreground, accents, semantic error/success/warning, typography scale, spacing, radii, animation durations/easing, and touch sizes.
- Avoid indiscriminate blur/glow effects that render poorly on constrained hardware.
- Support full Greek glyphs, accents, punctuation, line breaks, truncation and scrollable long text.
- Touch targets generally at least 48×48 px; use 56+ px where practical for primary actions.
- Minimize visual clutter. Every icon must communicate a real, discoverable function.
- Contrast and legibility win over decorative effects. Text and buttons must not be placed on bright parts of imagery without legible treatment.

A reusable component library should include `StatusBar`, `AppHeader`, `PrimaryAction`, `ActionTile`, `IconButton`, `BackButton`, `ListRow`, `MessageRow`, `NotificationCard`, `SettingsRow`, `AudioControls`, `AIOrb`, `StateLabel`, `ErrorState`, `LoadingState`, and `Toast` (or equivalent names consistent with the repo).

## 4. TOUCH SYSTEM — FIX THE REPORTED ROOT BUG, NOT ITS SYMPTOMS

The current confirmed symptom is: touching the circular icon does nothing; touching the text underneath activates the action.

Perform a true LVGL hit-test / object-tree investigation before patching:
- Locate the object actually receiving `LV_EVENT_PRESSED`, `LV_EVENT_RELEASED`, `LV_EVENT_CLICKED`, and cancellation events.
- Inspect parent and child flags, `LV_OBJ_FLAG_CLICKABLE`, `LV_OBJ_FLAG_EVENT_BUBBLE` if appropriate to the installed LVGL version, z-order, hitboxes, overlaps, transforms, clipping, scrollable parents, and hit-test behavior.
- Verify decorative children cannot steal pointer input and that an invisible full-screen overlay cannot intercept it.
- Ensure the simulator pointer coordinate transform matches the rendered 480×480 area even if the desktop window has chrome, borders, HiDPI scaling, or is resized.
- Do not attach the only handler to a label. Use a **single semantic action tile as the actual interactive hit target**; icons and labels are visual content inside it.
- Ensure icons, labels, the circle, and space between them all map to the same action; avoid double firing via event bubbling.
- Provide pressed/selected feedback without accidentally blocking taps during transitions.
- Use the real LVGL version APIs; do not guess or blindly copy LVGL 8/9 syntax.
- Ensure scroll gestures scroll, horizontal gestures navigate only where appropriate, and taps don't accidentally trigger both.
- Protect callbacks from use-after-free when screens are changed while queued input/timers exist.

For each action tile, inject **real pointer press/release through the simulator's input path** at at least four coordinates: icon center, icon edge, label center, and the gap between. Verify correct single destination/action, not just handler invocation. Test both normal and simulator-scaled window configurations if supported.

Do not 'fix' the issue by adding arbitrary transparent buttons on top of unrelated elements. Repair component ownership and event propagation consistently across screens.

## 5. NAVIGATION AND FUNCTIONALITY CONTRACT

Create `docs/ui/ROUTE_ACTION_MATRIX.md` as the source of truth.

Every visible action must be classified as:
- `NAVIGATE`: defined screen and back-stack behavior.
- `EXECUTE`: concrete service invocation with success/failure feedback.
- `TOGGLE/ADJUST`: genuinely persisted or appropriately simulated setting.
- `DISABLED`: visible unavailable state with a useful explanation, no click pretending to work.
- `NON_INTERACTIVE`: clearly decorative or informational, not styled like a button.

For all enabled controls document: displayed text/icon, action ID, destination/handler, service dependency, expected success behavior, expected failure behavior, tests. No orphan screens; every reachable screen has a reliable Back/Home route. Do not route multiple unrelated icons to the same placeholder page. No meaningless icons, empty settings pages, or fake app launchers.

Preserve all **actually implemented** features from the existing AURA repository, including (where present): AI interactions and local Codex bridge, text results, simulator developer tools, Wi-Fi/offline handling, audio engine, settings, notifications, search, weather, and existing Aoraton work. If the UI exposes functionality not yet implemented, label/disable it honestly instead of inventing behavior.

Do not turn planned GPS, Bluetooth headset audio, native WebRTC calls, calendar, camera, music streaming, or maps into enabled features unless real implementations are discovered and verified. The board has no built-in GPS, so do not report live GPS without added hardware or an explicitly simulated location source. The ESP32-S3 is BLE-only; do not promise Bluetooth Classic headphone audio.

## 6. REACTIVE UI STATE MACHINE AND SINGLE SOURCE OF TRUTH

Use a deterministic assistant state machine with meaningful transitions, for example:
`IDLE → WAKE_DETECTED → LISTENING → PROCESSING → ANSWERING/SPEAKING → IDLE`, with `OFFLINE`, `ERROR`, and `CANCELLED` paths.

Tie UI presentation and motion to service events rather than manually changing unrelated labels:
- AI request start, partial response, complete response, cancellation, failure.
- Microphone listening / actual input amplitude where available.
- Audio playback start, amplitude/PCM envelope, finish, stop and error.
- Navigation and notifications.

In simulator/demo mode, generate **deterministic clearly labeled synthetic events**. In live Codex mode, show genuine service status. Never claim the mic is active when audio capture is absent. No permanent animated 'thinking' state after the backend has finished or errored. No UI spinner masking an indefinite hang; provide timeout, retry, and cancel.

All core app states must be testable without hardware. Do not launch Codex processes from inside ESP32 firmware or expose unrestricted CLI execution over the network. Codex CLI remains PC-only through a carefully isolated host bridge.

## 7. ANIMATION SYSTEM — MUST BE REAL, NOT A PASTED IMAGE

**This section supersedes the earlier v5 instruction to remove animation.** Implement a motion system as a first-class, non-blocking subsystem.

Create reusable `MotionController` / `AIVisualController` components driven by AI and audio state. Animations must start, stop, pause, resume, and release resources when the screen changes. They must never freeze touch handling.

Required motion behaviors:

**IDLE / HOME**
- Premium AI orb with subtle continuous 'breathing' or layered parallax; slow, smooth luminance/scale/inner energy variation.
- A true moving effect, not the same static image displayed forever.
- Low-intensity idle cadence and optional low-power/static mode.

**LISTENING**
- Blue/cyan reactive waveform, bars, rings or luminous equalizer evolving over time.
- If microphone samples exist, map filtered energy to animation; otherwise simulator uses a labeled synthetic amplitude feed.
- Clearly indicate whether real capture or demo simulation is active.

**PROCESSING / THINKING**
- Distinct animated flow/particle travel/rotating layered light, with measured progress only where real progress exists.
- A believable lively effect but not a fake percentage or fake completion timer.
- Cancel/timeout handling and no infinite stuck state.

**ANSWERING / SPEAKING**
- Real audio-reactive motion from PCM amplitude when playback exists.
- For text-only responses, use a restrained text/reveal transition; do not pretend sound is playing.
- Show useful answer content, not a huge decoration that obscures it.

**SUCCESS / ERROR / OFFLINE**
- Short, tasteful transition on state change; clear, immediately actionable controls.
- Persistent errors should not run costly perpetual animations.

**NAVIGATION / CONTROLS**
- Short eased transitions, pressed feedback, contextual panels and toast appearance, but preserve immediate touch response.

Technical rules:
- Favor native LVGL animations, small layered sprites, dynamic primitives, texture/sprite sequences, and reusable cached assets. Do not assume desktop GPU shaders are available on ESP32.
- If using prerendered frames, they must be an actual frame sequence or layered moving composition, not one pasted picture. Document licensing/source and compress/size assets for the device.
- Avoid decoding entire 480×480 full-frame animations on every frame. The target has 8 MB PSRAM shared with UI/network/audio; validate budgets.
- Update only changed regions. Use bounded sprites, partial drawing and dirty rectangles; keep status text pixel-stable and eliminate flicker.
- Animate at a measured target around 30 FPS for main visuals where feasible, and degrade gracefully under hardware load; no imaginary promises of 60 FPS.
- Keep audio, network, and UI threads separated. Avoid blocking sleeps or synchronous network calls in LVGL callbacks.
- Support reduced-motion and low-battery modes without breaking functionality.
- No photosensitive rapid flashing or harsh strobes.
- Expose animation FPS, UI frame time and memory usage in simulator diagnostics.

## 8. CORRECT EACH REAL SCREEN, WITHOUT INVENTING FEATURES

**Home** — square, full-bleed premium identity; centered living orb, elegant `AURA` name, current state, prominent actionable entry into conversation, deliberate access to launcher. No fullscreen circle, cropped giant poster, or hardcoded telemetry.

**Listening** — centered live waveform/equalizer, legible listening state, obvious cancel/stop, truthful real-vs-demo status, no permanently static waveform.

**Thinking** — distinct purposeful motion, processing label, responsive cancel and error path, not a static stock vortex image covering the UI.

**Answer/Conversation** — scrollable long Greek responses, streaming updates where supported, actionable relevant controls (copy/retry/speak only if implemented), clear return route; no text cutoffs or artwork overlays.

**Quick Actions** — clean 2×2 or adaptive grid made only of verified features. Whole tiles clickable (icon+label+gap). Appropriate icons and meaningful labels. Remove or disable unimplemented launcher items. No faux action icons.

**Notifications** — working list, unread indication derived from real/mock service, open/dismiss routes, empty state. Simulated notifications are explicitly marked as such.

**Search** — genuine search flow if wired, otherwise clearly identified simulation using fixture results; selectable result must open an actual detail or grounded external URL path if platform supports it. No fake phone numbers presented as real.

**Aoraton** — preserve existing functionality; show real status vs demo status honestly. Functional room/message routes only where implemented. Do not bypass E2EE, automatically send messages, or invoke external AI with decrypted private messages. Sending requires explicit confirmation. If only mock works, call it mock.

**Audio / Voice** — working playback, volume, mute, test actions, and meaningful service diagnostics if those features exist. No fake play/skip buttons or fake TTS language choices. Respect ES8311 and board BSP; GPIO46 is a strapping pin and PA control must follow verified board logic, not assumed HIGH/LOW.

**Settings** — working rows only, persisted values when supported; disabled rows describe what is missing; Back/Home always works.

**Offline / Error** — truthful failure cause, retry invokes actual retry, route home available. Offline AI calls must fail visibly, not silently pretend they succeeded.

**Clock / Battery / Connectivity** — status bar must not show permanently fixed `17:47` and `78%`. Implement a `ClockService`, `PowerStatusService`, `ConnectionStatusService` (or existing equivalents). Desktop uses host time or controlled simulated time; battery simulation must change and be labelled SIM if it is not measured. Firmware later uses validated RTC/NTP, PMIC/battery measurements where exposed. Show unknown values honestly as unavailable. Update at sensible intervals and repaint only the value region.

For every screen: define component hierarchy, layout grid, service bindings, empty/loading/error states, route entry/exit behavior, animation life cycle, and testable interactions.

## 9. SIMULATOR IS THE PRIMARY ACCEPTANCE ENVIRONMENT

The Windows simulator must run the **real LVGL screens and shared app logic**, not a separate webpage or pre-rendered video. It must support mouse-to-touch, keyboard text input where applicable, proper resizing/scaling, realistic service stubs, and a developer panel available only in simulator builds.

Provide deterministic controls to simulate:
- AI idle/listening/processing/answering/errors.
- Real Codex response vs fake fixture response, clearly distinguishable.
- Microphone/audio amplitude for wave animation.
- Audio playback complete / fail.
- Wi-Fi online/offline, reconnection success/failure.
- Battery level, charging and low power.
- Incoming mock Aoraton message and notification.
- Search results and empty search.

A `DEMO` indicator should appear **only when a simulated feature/state requires disclosure**, without dominating visual design. Do not show static DEMO text on unrelated real states.

Simulator-driven mock state changes must be visible in the interface and not rewrite real user data.

## 10. TEST STRATEGY — VERIFY ACTUAL TAP LOCATIONS

Create runnable integration tests through the simulator's **real input device path** (SDL mouse events or documented LVGL indev injection appropriate to the project). Directly calling event handler functions is not a substitute for hit testing.

Mandatory interaction matrix for each enabled action:
1. Tap center of visible icon.
2. Tap an edge safely inside the visual button.
3. Tap text label.
4. Tap gap between icon and label.
5. Check one action execution and the correct destination.
6. Verify Back/Home works.
7. Verify disabled controls are honestly inactive.
8. Verify no visual or transparent overlay intercepts input.
9. Repeat rapid taps and taps during animation to detect double firing and race conditions.

Mandatory flows:
- Home → interactive assistant → Listening → Processing → Answer → Home.
- Home → Quick Actions → each enabled action → Back.
- Settings → change a real value → verify persistence → Back.
- Audio → play test sound → change volume/mute → stop; UI stays responsive.
- Notifications → open/dismiss and verify counts.
- Aoraton mock → room → prepare message → confirm → success; never send to production.
- Offline → real retry/failure → restored state.
- Simulated time and battery changes reflect on screen.
- Error/timeout/cancellation paths always terminate to a coherent state.

Run automated unit tests for navigation state, action dispatch, animation lifecycle, service contracts, time/power bindings, event unsubscription, and memory/resource cleanup.

Produce a machine-readable action coverage report. Every **enabled visible action** must have passing interaction evidence. Screenshots alone do not prove clickability.

## 11. VISUAL QA — REAL 480×480 SCREENSHOTS AND ITERATION

Capture actual simulator screens for Home, Quick Actions, Listening, Thinking, Answer (short/long), Settings, Audio, Notifications, Aoraton, Search, Offline, and Error (where implemented).

For each check:
- No circular full-screen border/viewport.
- All content properly centered relative to the **square** canvas.
- No image overflowing/cropping incorrectly or layering over buttons.
- No text clipped (especially Greek), no ellipsis abuse, no illegible overlays.
- Real consistent typography and spacing; correct semantic icons.
- Status bar updates correctly and does not overlap UI.
- Disabled/placeholder items are not presented as working features.
- Screens look related, not like pasted concept images.
- Visuals remain coherent in idle and in transition mid-frame.

Take short **video or frame sequences** of Home breathing, Listening reaction, Thinking motion, Speaking/audio reaction and screen transitions. Static screenshots alone cannot verify animations. Inspect for flicker, runaway overlays, dropped frames, frozen states, orphaned timers, and unresponsive taps. Measure approximate FPS/frame timings if instrumentation permits.

If your execution environment cannot view/record GUI, explicitly state this and provide reproducible local commands and tests. Never claim that visual or interaction checks passed without running them.

## 12. HARDWARE FEASIBILITY AND PERFORMANCE

The physical board is not yet available. Build firmware when the ESP-IDF toolchain is present, but label it `BUILD VERIFIED / PHYSICAL DEVICE NOT TESTED`.

Use ESP32-S3 dual-core and 8 MB PSRAM responsibly; avoid excessive stack usage and unbounded allocations. Budget UI assets, fonts, draw buffers, network, audio, and Aoraton buffers. Use low-power modes without disabling necessary wake/listening services. Avoid full-screen repaint each animation frame; animate small changing regions and preserve static UI stability. Do not promise simulator FPS will equal physical device FPS.

Keep the production pipeline compatible with verified official Waveshare BSP, CO5300 display, touch driver, ES8311 audio and AXP2101 where used. Never make up hardware pin assignments or claim a physical speaker/audio circuit is tested before board arrival.

## 13. DELIVERABLES AND PROGRESS GATES

Update or create:
- `docs/ui/FEATURE_INVENTORY.md`
- `docs/ui/ROUTE_ACTION_MATRIX.md`
- `docs/ui/DESIGN_SYSTEM.md`
- `docs/ui/MOTION_SPEC.md`
- `docs/ui/TOUCH_HIT_TESTS.md`
- `docs/ui/VISUAL_QA.md`
- `docs/ui/REGRESSION_REPORT.md`
- `docs/ui/HARDWARE_LIMITATIONS.md`

Use existing documentation layout rather than creating duplicates.

Implementation gates:
**G0 — Recovery:** Save reversible baseline; produce audited feature inventory and action map.
**G1 — Functional skeleton:** Restore all real features/routes, fix global touch ownership and layout in square 480×480.
**G2 — Visual foundation:** Implement reusable design tokens/components/status bar; remove circular border, hardcoded data, off-center posters.
**G3 — Motion:** Implement true animated orb/waveforms/state transitions with observable runtime motion and correct lifecycle.
**G4 — Integration:** Bind UI to actual AI/audio/network/Aoraton capabilities; disable unsupported actions instead of faking them.
**G5 — QA:** Run emulator interaction tests, screenshots and motion recordings; fix failures and regressions.

Do not add unrelated applications or illustrative icon grids merely to fill space.

For each gate, build/run/test and report evidence. Prefer working, reviewable, incremental changes over a massive unverified rewrite. Preserve old functionality throughout.

## 14. STRICT DEFINITION OF DONE

Only declare completion if all applicable points are satisfied:

- [ ] Simulator launches and all prior working functions remain accessible.
- [ ] Display uses true **square 480×480** layouts with no full-screen circular frame.
- [ ] No off-center, oversized pasted image obscures controls or text.
- [ ] Clock, battery and network indicators derive from live or explicitly simulated data; none are unexplained hardcoded constants.
- [ ] Home orb visibly moves in a repeatable time-based animation.
- [ ] Listening animation visibly changes and reflects real/synthetic amplitude accurately labelled.
- [ ] Thinking animation visibly moves without blocking UI or getting stuck indefinitely.
- [ ] Speaking visuals react to actual playback when available; text-only state makes no false audio claims.
- [ ] Every enabled button works through its **whole visual area**, not just text.
- [ ] Every enabled action has a meaningful implemented destination or behavior.
- [ ] Disabled/unimplemented features are clearly indicated or hidden; no decorative dead controls.
- [ ] All screens have reliable Back/Home navigation.
- [ ] No previously working AI, audio, notifications, settings or Aoraton feature was removed.
- [ ] Animations do not block taps, degrade into flashing, or leak callbacks/timers.
- [ ] Simulator tests cover real pointer hits and service integration, not merely mocked action handlers.
- [ ] Current screenshots and motion samples were inspected and corrected.
- [ ] Hardware feasibility is documented honestly; firmware build and real-device verification are distinct.

Any unchecked criterion is a blocker, not a pass.

## 15. HOW TO EXECUTE — DO NOT STOP AT PLANNING

**Start immediately in the current AURA repository.** Perform the initial audit and preserve a recoverable baseline, then implement the smallest verifiable corrections first. Do not ask for approval on routine, reversible changes inside AURA; do not change unrelated repositories or production infrastructure. If an action risks irreversible changes or needs secrets, stop and request approval for that action only.

Do real work: edit code, build, run, inject pointer interactions, exercise UI states, capture screenshots and animation evidence, fix failures, repeat. If an external service is unavailable, use clearly disclosed deterministic simulator stubs while preserving the real service.

Your final report must include:
1. Exact files changed.
2. Which regressions were repaired and why they occurred.
3. Feature-parity comparison before/after.
4. List of actual enabled routes/actions and any intentionally disabled items.
5. Screenshots and animated state verification artifacts.
6. Real interaction test matrix results, including icon-center vs label taps.
7. Builds, test commands, pass/fail counts and honest unresolved limitations.
8. Simulator launch command and next hardware validation steps.

**Do not say 'done' after merely modifying styles or passing compilation. The acceptance criterion is a beautiful, animated, clickable, fully wired AURA that demonstrably works.**

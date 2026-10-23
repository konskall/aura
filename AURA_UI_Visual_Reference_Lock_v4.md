# AURA — UI VISUAL REFERENCE LOCK v4.0
## Pixel-conscious, reference-driven LVGL redesign for Waveshare ESP32-S3-Touch-AMOLED-2.16
### Paste this document into the CURRENT Codex or Claude Code session in the EXISTING AURA repository.

> **Primary instruction:** Rebuild the AURA UI to match the **attached 18-screen visual reference** as closely as physically and technically feasible on a 480×480 square AMOLED device. This is NOT a request for another interpretation, moodboard, minimalist alternative, or rough approximation. Implement real interactive LVGL screens, launch the existing Windows simulator, compare real screenshots with the reference, and iterate visibly.

## 0. AUTHORITY, INPUT AND PRIORITY

**Authoritative visual asset** (relative to the AURA repository root):

`design/reference/aura_ui_target.png`

It is a 1536×1024 design board with **six columns × three rows, 18 labeled circular UI concepts**. The image MUST actually be opened/inspected using the coding agent's image-viewing capability. Reading only its filename or describing it from the prompt is NOT sufficient.

If you cannot access or view the file, STOP visual work and report the precise blocker; do not fabricate what it looks like. The reference is an asset for design guidance, not something to show verbatim as a fake functional simulator.

**Precedence:** This v4.0 visual specification overrides earlier AURA UI directions wherever they conflict, **particularly v3 “Quiet Intelligence,” muted/minimal styling, the previous anti-neon guidance, and any generic square-card mockups**. The attached reference is the north star for visual composition, luminosity, navigation style, motion language and richness. All existing nonvisual functional and security requirements stay in force.

The user explicitly rejected recent UI implementations because they did not resemble the originally promised visual reference. Passing a compiler check, creating functional but generic LVGL screens, or saying that the visuals are “inspired by” the image is a **failure**. We must measurably and demonstrably close that gap.

## 1. PROJECT CONTEXT / DO NOT BREAK EXISTING WORK

- Product: **AURA — Advanced Universal Responsive Assistant**.
- Board: **Waveshare ESP32-S3-Touch-AMOLED-2.16** (480×480 AMOLED touchscreen, 8 MB PSRAM, 16 MB Flash, battery-equipped variant).
- Development host: Windows 10; interactive **native LVGL desktop simulator**.
- Firmware: ESP-IDF + existing project board/BSP drivers, version as actually found in repository.
- AI during desktop development: local installed Codex CLI via existing host bridge; future ChatGPT or Claude cloud integration remains modular.
- Existing or planned services: audio playback / ES8311 / amplifier management, microphone/wake word, network, battery/PMIC, settings, notifications, AI state machine, search, local storage, future Aoraton messaging.
- Aoraton independent source repository: `C:\Users\polis\Documents\incognitochat`. Read-only unless separately and explicitly authorized. Do not disrupt its E2EE architecture.
- Physical board has **not arrived**; simulator and firmware builds are not equivalent to on-device validation.

Before implementation: inspect git status and preserve all user changes; read actual project instructions, current simulator setup, LVGL version and available assets; inventory every existing screen, its navigation action, its real backend binding, and available test commands. Do not restart the repository, rip out functioning services, or switch the main UI to HTML/CSS/React. A supplemental designer preview is permitted, but the final implementation must be genuine LVGL running in the simulator and prepared for the device.

## 2. NON-NEGOTIABLE REFERENCE MATCH

The target image depicts a **black, almost edge-to-edge circular wearable-like interface**, with razor-sharp cyan/indigo/violet-magenta luminescence, real sense of light and depth, subtly beveled circular bezel, rich glowing AI orb/waveforms, elegant text, gradient-filled icon controls, tight but legible informational layouts, compact status indicators and thoughtfully differentiated screens.

Preserve the following visual DNA:

1. **Circular UI composition**, dark outer ring/bezel, inner black OLED canvas, elegant radial framing, no ordinary permanent four-button bottom navigation.
2. **Breathtaking luminous AI presence**: translucent layered orb on Home, fluid waveform on Listening, particles/spiral on Thinking, luminous flow surrounding orb on Speaking. These four states must have clearly DIFFERENT silhouettes and motion, not a static icon with swapped label.
3. **Polished neon palette**: black `#03050A` / midnight indigo backgrounds, electric blue/cyan highlights, royal blue/violet and occasional magenta glows. Use mint for success, amber for weather/warnings, warm bright colors for utility icons. High contrast white type.
4. **Convincing optical richness**: fine inner glow, luminous edges, soft falloff, graduated depth, occasional subtle particles, well-composed material-like surfaces. Not flat gradient-filled rectangles, clipped cyan borders, or legacy-looking controls.
5. **Premium compact type** with consistent Greek glyph rendering, precise baselines, meaningful hierarchy, restrained status text, and no scrambled Greek letters.
6. **Glassy dark surface treatments** on lists, small tiles and cards, with subtle gradients, very low-contrast dividers, rounded corners, colored icon badges, delicate glow for selected actions; NEVER pile on thick bright outlines everywhere.
7. **Clear micro-interactions**: luminous selected states, smooth transitions, natural wave motions, readable loading feedback, graceful animated navigation.
8. **Visual completeness**: no huge unintentional empty areas, tiny floating controls, mismatched padding, text overrun or unfinished placeholders.

The reference includes artifacts that are normal in an AI-generated concept (some tiny text may be distorted, timestamps differ, and certain features presume optional hardware). **Copy its structure, hierarchy, color, density, surface treatment and intent; replace garbled text with correct Greek/English and never pretend unsupported hardware is attached.**

## 3. THE CIRCULAR-ON-SQUARE DESIGN PROBLEM — SOLVE IT DELIBERATELY

**Hardware truth:** The selected Waveshare has a **480×480 square AMOLED panel**, not a physically round smartwatch screen. The reference artwork shows circular devices. Do not pretend the board becomes round and do not silently reinterpret the reference as square cards.

Implement a **circular dial composition INSIDE the real 480×480 square framebuffer**:

- Default simulator/device output must remain precisely **480×480 square**.
- Create a premium circular visual shell approximately **448–468 px diameter**, centered; tune from measured screenshot. Soft, dim perimeter bezel/ring and near-black square corner zones should produce the reference's circular appearance.
- The outer square corners should be visually unobtrusive (AMOLED black), not a contrasting frame.
- Reserve curved-edge safe zones for controls and text. Large horizontal content blocks should live within a central circle chord, not clip at x=0 or x=480.
- Keep small time/connectivity/battery indicators in the circle's upper chord; action controls/navigation within its lower safe region.
- **Content-heavy views** (Aoraton, results, settings) may use an intelligently scrollable central vertical panel while preserving the circular visual shell. The full 480×480 physical screen is usable if needed for text readability, but the overall appearance must still read as the photographed circular UI.
- No overdraw or cropping tricks that hide actual actionable controls near circular edges.
- In the desktop simulator, optionally show the 480×480 screen in a neutral window; never fake a round hardware screenshot as the only simulator mode. Provide the raw square framebuffer capture for validation.

Implement a reusable `CircularAppShell` / radial safe-area layout helper or adapt an existing equivalent. Decide whether masks, alpha bitmaps, ring images or drawing primitives best fit the actual LVGL version and memory budget. Avoid an expensive full-frame alpha compositor each frame.

## 4. SCREEN-BY-SCREEN VISUAL CONTRACT — EXACT REFERENCE ORDER

The reference board contains **18 concepts arranged left-to-right, top-to-bottom**. Build an explicit `docs/design/REFERENCE_SCREEN_MATRIX.md` and inspect/crop every concept with its caption. For each screen record: reference position, intended composition, typography, assets, controls, application state, backend readiness, edge cases and current implementation state.

### 01 — Home / Idle
- Hero: large, glasslike **cyan-blue-violet spherical luminous orb**, centrally positioned, visually layered with highlights and translucent volume, like the reference.
- Product identity `A U R A` or carefully letter-spaced `AURA`, small secondary subtitle e.g. `Ο προσωπικός σου βοηθός`.
- Top arc: time, Wi-Fi, accurate battery state (simulated and labeled in development).
- Bottom: ONE prominent glowing microphone / primary interaction pill (`Πες κάτι…` or `Μίλησέ μου`) rather than legacy button rows.
- Orb idle motion: subtle internal slow flow, not static or violently pulsing. Wake word activation and tap both map to real assistant state events.

### 02 — Listening
- Replace orb with **luminous vibrating sound-wave sculpture**: purple-blue-cyan center shape with thin horizontal waveform wings and surrounding dim sound bars, following the original image.
- Correct Greek status `Σε ακούω…`, one prominent central circular mic/cancel affordance near the lower arc.
- Wave amplitude must come from real local microphone level where available; simulator may use microphone input or clearly flagged simulated amplitude.
- Real listening/stop/cancel interactions and obvious privacy indication; don't falsely indicate active audio capture.

### 03 — Thinking
- Visually distinct **galactic/vortex-like purple-blue ring** with beautiful subtle particles and depth, not the Home orb enlarged.
- `Σκέφτομαι…`, small contextual status like `Επεξεργασία αιτήματος`, elegant progress indication only if actual asynchronous work exists.
- Animate without blocking UI. No fabricated tokens/sec, no fake provider activity.

### 04 — Speaking / Answer
- Floating glowing violet-cyan orb with **flowing luminous ribbon/wave**, a small horizontal audio equalizer below and a clear stop/pause action.
- UI follows real Audio Service `PLAYING/PAUSED/ENDED` events and real PCM envelope if available.
- Status should be correct (`Σου απαντώ…` / `Απάντηση`), not claim audio playback if unavailable.
- Automatically hand off to a text-first readable Answer screen for long responses.

### 05 — Quick Actions
- **Elegant circular 2×2 utility grid** like the reference: rounded dark raised icon wells with luminous blue/cyan/violet pictograms and concise labels.
- Starting actions: `Συνομιλία`, `Αναζήτηση`, `Πλοήγηση` or `Τοποθεσία`, `Εφαρμογές`.
- Only show active action as active if backed by working functionality; otherwise visibly indicate setup-required/disabled.
- Distinct, purposeful tap effects.

### 06 — Notifications
- Stacked, dark glass-like rounded notification rows with colored source icons, small secondary information and relative time, optimized for circle chord width.
- Include Aoraton/assistant/system examples using fictional fixture content in simulator.
- Real notification center events, unread counts, open/dismiss actions, privacy-controlled message previews.

### 07 — Aoraton Messages
- Match dark dense messenger appearance: prominent `Aoraton` identity/status, rounded avatar/room icons, rows with contact/room title and message snippet, time, unread indication, bottom compose affordance.
- IMPORTANT: Actual Aoraton may use anonymous rooms rather than conventional people-based contacts. Map visual avatars/names to real room metadata; do not invent contact-based identity semantics.
- Keep E2EE, pairing and explicit message-send confirmation; mock state clear in simulator. Never leak decrypted messages to remote AI provider by default.

### 08 — Navigation / GPS
- Visually match stylized dark map with bright blue highlighted route, prominent blue direction marker and floating navigation information.
- **No GPS receiver is built into the Waveshare by default.** Until GNSS or a location provider is connected, use a clearly designated sample/demo map or a meaningful `Σύνδεση τοποθεσίας` setup state. No fake real-time navigation and no invented ETAs.
- Architecture should be ready for future external GNSS module and map tiles/assets. Support map image caching only within resource constraints.

### 09 — Search / Internet
- Modern black/dark navy search view with rounded search pill and sophisticated category rows: web search, images, places/info.
- Actual results must come from connected provider; mock data must be distinguishable in simulator.
- Render useful results including phone/address/source/QR action where supported. Scrollable reader with legible Greek.

### 10 — Weather
- Reference's strong warm-yellow sun/icon, large white temperature, clean city label, elegant blue/violet atmospheric ambient glow and small three-column metrics.
- No hardcoded actual temperature. Drive from a weather provider, or clearly labeled demonstration data. Support offline fallback with timestamp.

### 11 — Settings
- Rich dark rounded rows with colored circular icons, thin separators, chevrons, tidy labels: `Wi-Fi`, `Bluetooth`, `Ήχος`, `Οθόνη`, `Τοποθεσία` (if available), `Σύστημα`.
- Open true nested settings screens and persist changes; do not show inactive toggles as working.
- Bluetooth LE exists but **Bluetooth Classic headphones / LE Audio are not available natively**; do not advertise headphone pairing in settings as a working feature.

### 12 — Music / Audio Player
- Attractive small artwork thumbnail, track title, artist/source, thin glowing progress, previous/play-pause/next and real volume output behavior.
- ES8311 audio playback is an intended subsystem; simulator output must use the PC audio backend. Supported media formats only; no pretend streaming services.
- Speaker/physical assembly status must be hardware-validated when board arrives.

### 13 — AI Tools / Applications
- A compact glowing 2×2 or 2×3 launcher grid: `Σημειώσεις`, `Υπενθυμίσεις`, `Μετάφραση`, `Υπολογιστής` as actually supported; bright distinctive gradient icon wells.
- No clutter or generic legacy app grid. Mark features in development honestly.

### 14 — Calendar
- Compact elegant month header, precise Greek weekday abbreviations, clear selected date and one bottom upcoming-event card; default calendar data from fixture or a connected calendar provider.
- No false claims of synchronized personal events when no connected source exists.

### 15 — Camera / Photo
- Mirror the aesthetic: large centered photographic preview and neat shutter control. **The target Waveshare board does not include a camera by default.** Display a tasteful `Απαιτείται κάμερα`/gallery setup state, or explicitly labeled reference/demo preview; do not claim live camera capture.
- Keep optional camera capability behind an implementation contract for future hardware.

### 16 — Offline Mode
- Spacious near-black circular center with dignified purple offline symbol, polished concise Greek explanation, a useful retry/available-offline-actions control, and clear connection diagnostics when requested.
- Functional degraded mode; no fake internet features.

### 17 — Battery & Status
- Distinct large **luminous mint/green circular energy gauge**, a bold percentage, charging indicator when real, and compact connectivity quick toggles where supported.
- Show realistic values from PMIC/firmware on hardware; simulator values clearly fictional. Never promise battery-runtime estimates without measured power profiles.

### 18 — Voice Settings
- Attractive stacked rows with luminous purple/blue selection accents, active voice choice, available language indicator and explicit provider/voice capabilities.
- Support installed voice/provider availability; do not imply arbitrary voices/languages exist. Greek voice option only if installed or available through configured provider.

### Additional states beyond the 18 tiles
Maintain full views for long Greek AI text answers, Aoraton room conversations, compose+confirm message, search result details, Wi-Fi onboarding, privacy permissions, error recovery, empty states, notifications detail, volume control and diagnostics. Style them as natural extensions of the corresponding reference tile. Do not hide working product features just because they are not shown on the 18-screen board.

## 5. DESIGN TOKENS — REFERENCE-EXTRACTED, NOT GENERIC

Before styling: sample colors and structural dimensions from the actual `aura_ui_target.png`, keeping a design-tokens note in `docs/design/REFERENCE_ANALYSIS.md`. The following are starting estimates; **adjust after visual inspection and side-by-side comparison**.

- OLED background: `#000105`, `#030712`.
- Deep navy surfaces: `#0B1424`, `#111D30`, `#182337`.
- Primary text: `#F8FBFF`; secondary: `#BACBE3`; muted: `#8295B0`.
- Primary vivid blue: `#178BFF`, electric cyan: `#25D6FF`.
- Orb indigo: `#4C52FF`, ultraviolet: `#8A45FF`, violet-magenta: `#DE58FB`.
- Accent mint: `#35ECC3`; warm amber: `#FFCB3F`; notification pink: `#FF579E`.
- Deep transparent-looking highlights: precomputed assets or physically feasible blend layers, not inaccessible CSS.
- Circular bezel/ring: dark steel navy with thin low-luminance edge light.
- Row surfaces: very subtle gradient and soft edges, radius ~14–24 px where appropriate.
- Standard touch targets: aim for ~44–52 px where practical (more for key mic/play controls).
- Main title/body sizing adapted to the exact 480px viewport; primary text normally ~18–25 px and secondary labels ~14–17 px where physically legible.

Use one cohesive vector icon set, consistent optical weight, high quality assets and licenses. **No Unicode emoji as shipped icons**, no inconsistent raster icons, no clipped icon font, no default unstyled LVGL controls.

Use a legal Greek-supporting font with full modern Greek + Latin punctuation, accents, numerals and technical characters. Subset only what is required for the firmware; verify words like `Σκέφτομαι…`, `Πλοήγηση`, `Ειδοποιήσεις`, `Επεξεργασία`, `Αποσυνδέθηκε` render cleanly, not as boxes. Scale headings sensibly; avoid two-line tiny labels when a shorter accurate Greek term will work.

## 6. ORB / WAVEFORM / PARTICLES — ASSET IMPLEMENTATION PLAN

The concept's rich imagery cannot be achieved by a single flat LVGL circle with a gradient. Use a hardware-feasible hybrid artwork system with measured budgets:

- **Home orb:** pre-rendered 24–64-frame RGBA or RGB565+transparency sprite/atlas / compressed asset sequence; alternatively limited layered procedural rendering if it demonstrably looks just as good and profiles better.
- **Listening:** real amplitude-driven bars and precomposed curved luminous wave textures; emphasize crisp, bright centerpiece and quiet background.
- **Thinking:** vortex/ring motif and sparse moving particles with limited dirty regions; scale particle count based on actual performance.
- **Speaking:** layered orb/ribbon with waveform controls, phase/light intensity tied to AudioService amplitude.
- **Transitions:** short crossfade or staged opacity/position transforms achievable in the project's LVGL version and display format.

**Asset pipeline:** store editable source assets and build/generation scripts in `design/` or `tools/`; keep converted LVGL-compatible assets under appropriate firmware asset folders; document RGB565, alpha/transparency, compression, bytes in Flash/microSD, RAM when decoded, cache strategy and legal origin. Do not embed the 1536×1024 reference sheet as the runtime UI, paint a screenshot into the simulator or play a video instead of using working widgets. Pixel-perfect-looking decorative layers MAY use reference-derived original generated assets while native LVGL hit targets/text/state remain real.

Do not assume advanced alpha blending/shader effects are free on ESP32-S3; measure. Use small display buffers, PSRAM sensibly, dirty-region updates, and small component-bound sprites instead of whole-screen redraws. Pause/reduce idle animation when display is dim or off; battery matters. Motion preference / reduced-motion setting must work.

## 7. INTERACTION / NAVIGATION SHOULD LOOK AND FEEL LIKE THE IMAGE

Build an intentional navigation model faithful to the 18 reference states:

- Main screen: immersive orb + single primary voice action.
- Quick access: beautiful four-action page/menu reached by one obvious interaction, NOT the old always-visible four-pill bottom bar.
- Compact radial/touch-friendly page controls; distinguish actual page indicators from decoration.
- Back/home actions always available without hidden-only gestures.
- Consistent tap, long press (if needed), swipe, scroll, selected state, busy state and animated transitions.
- Tool entry must open actual relevant UI with data or an honest setup state.
- Existing settings, notifications, search, Aoraton and audio features must remain reachable.
- Developer debugging controls must be isolated from the premium production interface, behind an explicit dev-mode route.
- Event/state architecture stays stable and reusable; do not thread AI/network work through LVGL render callbacks.

Implement natural Greek copy. Ensure real short and long states are legible and gracefully wrap. Do not cram five controls into a narrow arc.

## 8. SIMULATOR MUST BE ACTUAL RUNNING LVGL — NOT A PRETTY COMPOSITION

The target is the SAME real product UI in both:

**A. Windows 10 native LVGL simulator**, mouse/touch interaction, audio through Windows, mocked PMIC/GNSS/etc. with clear simulated state; and

**B. ESP32-S3 firmware** using the official board BSP and existing implementation architecture.

Do not create a standalone web mockup in place of the UI. Preserve and re-run the existing Codex CLI host bridge, audio backend, settings, AI state transitions, simulator control hooks, screenshot capture and event-driven services. Create the same reusable LVGL screen components for both targets. If real audio playback depends on hardware, the simulator must use its Windows audio backend.

For optional unsupported capabilities (camera, GPS receiver, cloud calendars, Bluetooth headphones), provide elegantly designed demo/disabled/setup states without inventing physical features or reporting fake connection success.

## 9. BOUNDED PERFORMANCE AND MEMORY — REALISTIC ESP32-S3

Render format and actual device constraints must be measured from the existing codebase; avoid unsafe assumptions.

Goals (not claims of measured performance):

- Responsive touch during Wi-Fi/AI/audio workloads.
- Visually smooth main interactions and 20–30fps *where feasible* for critical animation on device, reduced quality if necessary; high fluidity in simulator alone is not proof.
- Efficient partial rendering of moving orb, wave and particle areas, minimal static-text invalidation.
- Proactive budget for UI assets, compressed frames, LVGL cache, display buffers, audio DMA, future wake word models and Wi-Fi/TLS.
- An image cache policy that does not exceed PSRAM/Flash budgets; support SD assets as optional, not mandatory for core boot and recovery.
- Non-blocking asset decode/load with predictable fallback if an asset is missing/corrupted.
- No permanent giant full-screen RGBA framebuffers, repeated dynamic allocation or high-frequency full-screen clears.

Create `docs/design/ASSET_MEMORY_BUDGET.md` with empirical file sizes, calculated decoded sizes, startup memory and observed simulator performance; later add physical measurements when board arrives. Prefer an adaptive quality tier instead of a degrading experience that looks unrelated to the target image.

## 10. REFERENCE-DRIVEN VISUAL QA — THE MOST IMPORTANT ACCEPTANCE GATE

The old workflow failed because UI was implemented without sufficiently comparing to the exact promised art direction. Change the workflow.

### Mandatory process

1. **Read the uploaded reference image** directly and produce a detailed audit of the 18 panels. Name their positions (row/column), key objects, visual hierarchy and rough bounding boxes.
2. Optionally split it into individual concept crops for analysis. Preserve the source image unmodified.
3. Capture **before** screenshots of the actual current LVGL simulator for every existing AURA state; preserve baselines.
4. Define the radial/circular layout system and reference-extracted color/style tokens.
5. First implement the **Home / Listening / Thinking / Speaking** quartet at production-quality visual fidelity. They set the language for everything else.
6. Capture raw 480×480 simulator screenshots for each quartet screen; create a **side-by-side comparison** with the corresponding cropped target panel (target crop may contain conceptual bezel, text and caption; compare only the relevant UI design region).
7. Iterate the artwork/layout/typography until a human reviewer looking at both would clearly recognize the same design family, not merely “both are dark with a blue orb.”
8. Only then propagate the design to Quick Actions, Notifications, Aoraton, Search, Weather, Settings, Audio, Tools, Calendar, Camera placeholder, Offline, Battery, Voice.
9. Capture the full set of **18 screenshots** at **480×480**; generate an 18-screen contact sheet using the actual simulator framebuffers, with consistent screen labels.
10. Generate an **actual target-vs-implementation comparison** for all screens and a discrepancy log.
11. Test click/swipe/scroll/text entry, overflow, long Greek labels, error/offline states, message confidentiality and playback interactions.
12. Run builds and tests; correct failed checks. Reproduce the simulator launch command in the final handoff.

**Do NOT claim the redesign has matched the reference just because a function returns or the code compiles.** Provide the generated screenshot sheet and comparisons as evidence. Do not output only mockup concept art or screenshot replicas with no clickable LVGL controls.

### Measurable subjective QA rubric

Rate **each implemented screen** 0–5 for: (A) reference silhouette/composition, (B) light/material/color style, (C) typography and alignment, (D) affordance/interactivity, (E) runtime smoothness. Aim for **4/5+** for all A–D before considering the design visually ready, and document specific shortfalls. This self-rating is useful to prioritize fixes, not an objective proof of pixel similarity.

### Specific visual failure conditions

- A generic solid blue disk with an “A” instead of the luminous layered reference orb.
- Listening/Thinking/Speaking screens that look the same except for label.
- Square dashboard cards replacing the distinctive dark circular dial.
- Legacy four-button footer.
- Thick bright rectangular borders and debug-like control clusters.
- Tiny/garbled Greek or clipped strings.
- Artificial decorative controls that look clickable but do nothing.
- Entire screenshot embedded as UI instead of implemented native controls.
- Claiming GPS/camera/Bluetooth headset functionality without appropriate hardware or provider.
- Fake Aoraton live message states or leaked decrypted message data.

## 11. TEST REQUIREMENTS

Use existing test infrastructure plus targeted tests for:

- Navigation reachability among all 18 views and Back/Home behaviors.
- Correct AI state-to-screen synchronization and interruption recovery.
- Audio amplitude/speaking animation and mute/stop states.
- Scroll clipping and list focus on circular safe area, including long Greek labels and 2–5 paragraphs of answer text.
- Font glyph coverage for modern Greek.
- Battery/offline/simulator/demo status distinctions.
- No unexpected visual layout overflow or invisible touch targets.
- Behavior when SD assets absent, AI provider down, no GPS/camera/audio provider.
- Aoraton view/protocol boundaries and send confirmation where integrated.
- Screenshot capture of Home, Listening, Thinking, Speaking, Quick Actions, Notifications, Aoraton, Navigation, Search, Weather, Settings, Audio, Tools, Calendar, Camera/placeholder, Offline, Battery, Voice.

Maintain simulator determinism for visual regression: seed mock data, freeze timestamps/weather/battery to documented fixture values, disable random nondeterminism during screenshot runs, and capture at well-defined animation timestamps/frames. Runtime normal mode can animate naturally.

## 12. IMPLEMENTATION PLAN AND EXECUTION ORDER

### Pass 0 — Audit and design mapping
- Inspect repo, state machine, screens, LVGL version, simulator and prior design assets.
- View `design/reference/aura_ui_target.png` with image capability.
- Document exact reference panel map and design tokens.
- Create or reuse screenshot capture tooling.

### Pass 1 — Signature visual shell and visual assets
- Circular safe-area shell, ring/bezel, status typography, true AMOLED corner treatment.
- Asset-conversion workflow for layered orb/waves/particle textures.
- Greek fonts and icon assets with licenses.
- Implement reusable radial layout primitives and controls.

### Pass 2 — AI core states
- Home; Listening; Thinking; Speaking/Answer; long text reader.
- Screenshots and visual QA iterations until convincing reference fidelity.

### Pass 3 — Communication and utility screens
- Quick Actions, Notifications, Aoraton, Search, Weather, Settings, Audio.
- Real feature binding / honest mock state; end-to-end navigation and interaction tests.

### Pass 4 — Remaining features and states
- Tools, Calendar, Camera placeholder, Offline, Battery, Voice settings.
- Error/empty/setup/pairing details; full 18-screen screenshot gallery.

### Pass 5 — Performance and final verification
- Simulator end-to-end demo.
- ESP-IDF build where toolchain available (no unverified hardware claims).
- Budget/profiling, cleanup and report.

Do not stop at planning. Implement as much as possible within the environment; if constrained by context/time, deliver the finished highest-priority pass, accurate status and deterministic next steps. Never claim unfinished screens as complete.

## 13. REQUIRED OUTPUTS / REPO ARTIFACTS

Create or update in the existing repository (adapt exact paths to repo conventions):

```
design/reference/aura_ui_target.png    # original supplied source reference
docs/design/REFERENCE_ANALYSIS.md
docs/design/REFERENCE_SCREEN_MATRIX.md
docs/design/REFERENCE_COMPARISONS.md
docs/design/AURA_V4_DESIGN_SYSTEM.md
docs/design/ASSET_MEMORY_BUDGET.md
docs/design/REDESIGN_PROGRESS.md
artifacts/screenshots/before/*.png
artifacts/screenshots/after/01-home.png ... 18-voice.png
artifacts/screenshots/AURA_V4_ALL_SCREENS.png
artifacts/screenshots/reference_vs_actual.png
```

The final report must include:

1. Exact modified files and main design changes.
2. Which of the 18 screens are actually implemented and navigable.
3. Where actual 480×480 simulator screenshots live.
4. A side-by-side review of the reference versus the real LVGL UI, with remaining gaps.
5. Visual/layout and interaction test results.
6. Build command(s), simulator launch instructions and ESP-IDF compile status.
7. Observed performance and memory facts (vs unverified estimates).
8. Hardware-dependent validations still pending.
9. The next concrete implementation priority if anything is incomplete.

## 14. FINAL ACCEPTANCE RULE

**The image is the specification.** The design is not finished until opening the simulator feels like operating the SAME glowing circular blue-violet AURA UI shown in the supplied 18-screen concept board, adapted to the board's **real square 480×480 AMOLED** and implemented with live LVGL controls and honest capabilities.

Do not ask permission to make generic design decisions. Use the reference, analyze it, build it, screenshot it, compare it and improve it.

Start implementation in the existing AURA repository now. Preserve the audio, AI, and Aoraton architecture and the user's work. Do not create a separate replacement prototype, do not modify the production Aoraton repository, and do not declare visual completion without screenshot evidence.

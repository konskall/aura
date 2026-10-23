# AURA — PREMIUM UI/UX REDESIGN MASTER DIRECTIVE v3.0
## Production-grade visual and interaction redesign for the existing LVGL / ESP32-S3 AI companion
### Paste into the existing Codex or Claude Code project session. Implement in the existing repository; do not start over.

## 0. THE NON-NEGOTIABLE BRIEF

Act as a world-class product design director, embedded interaction designer, senior LVGL engineer, motion designer, accessibility specialist, and firmware performance engineer. Your responsibility is to turn **AURA** from a working but dated prototype into a cohesive, premium, contemporary AI companion with a distinctive identity and demonstrably polished interactions.

**This is a decisive redesign, not a color or font refresh.** Replace the current visual language and interaction hierarchy wherever necessary while preserving functional services, state handling, working integrations, and the existing application architecture.

Target: **Waveshare ESP32-S3-Touch-AMOLED-2.16**, 480 × 480 AMOLED touchscreen, ESP32-S3, 8 MB PSRAM, 16 MB flash, battery powered. Development runs on Windows 10 via a **real, interactive LVGL simulator**. The physical board is not yet available.

AURA already has or is developing: a Codex CLI development AI bridge, AI states, network and battery indicators, Greek localization, audio playback via ES8311 with PA_CTRL managed through a verified Waveshare BSP, notifications, settings, searches/results, and future Aoraton encrypted messaging integration. **Preserve these features and their contracts.** Do not redesign the firmware into a web app or replace the real LVGL simulator with a pretty HTML-only mockup.

The local Aoraton project is `C:\Users\polis\Documents\incognitochat`. Do not modify it as part of UI redesign; preserve the existing AURA-side integration and its privacy/security boundaries.

**Your task is to implement the redesign, launch the simulator, capture its actual output, critique it, iterate, test interactions, and report measurable results. Do not stop at moodboards, architecture notes, or static previews.**

## 1. FIRST: AUDIT THE EXISTING REPOSITORY AND CURRENT SCREENSHOTS

Read repository instructions, progress notes, existing UI components, current state/event contracts, LVGL version, simulator backend, fonts/assets, and test harness. Inspect git status and preserve uncommitted changes. Identify the actual source of each screen and how screen transitions are triggered.

Look for reference screenshots with these exact names, if available in the project or supplied separately:

- `aura-home.bmp`
- `aura-listening.bmp`
- `aura-thinking.bmp`
- `aura-answer.bmp`
- `aura-live-codex.bmp`
- `aura-dev-command.bmp`
- `aura-cards.bmp`
- `aura-notifications.bmp`
- `aura-settings.bmp`
- `aura-offline.bmp`
- `aura-error.bmp`
- `aura-smoke.bmp`

If you cannot access these BMPs locally, use the following verified audit of the current design and continue; do not pretend to have viewed unavailable files:

- The homepage uses a flat cyan/purple circle with an “A” and decorative orbit strokes; it looks like a dated logo rather than a living, responsive intelligence.
- Large unused black zones are not balanced with typography, function, or intentional negative space.
- The status strip uses a prominent `DEMO` pill and tiny information labels that compete with the actual product identity.
- Four persistent, cramped bottom buttons (`Αρχική`, `Βοηθός`, `Κάρτες`, `Ρυθμίσεις`) look like a legacy navigation bar and waste valuable 480 × 480 display space.
- A notification count appears over the Settings position, confusing navigation and notification ownership.
- Listening, thinking, and answer screens share nearly the same composition; the state change does not feel meaningful.
- Answer cards are rigid, over-bordered text rectangles with poor line length, hierarchy, padding, and reading space.
- Settings are squeezed into one large bordered panel; some controls and diagnostics feel bolted on rather than designed.
- Error, offline, search, and notification screens depend on the same generic outlined boxes and insufficiently differentiated emphasis.
- Label sizes, corner radii, border contrast, icon weight, and spacing are not part of one disciplined system.

Create `docs/design/UI_AUDIT.md` with a concise, specific before-state audit and priorities. Preserve the current screenshots as before-baselines wherever available.

## 2. CREATIVE DIRECTION: “AURA / QUIET INTELLIGENCE”

Choose and execute **one distinctive, fully coherent visual direction**:

**Calm intelligence + tactile precision + living light.** AURA should feel like an elegant physical AI artifact, not a dashboard from 2015, a gaming HUD, a generic smartwatch clone, or a neon sci-fi concept poster.

Visual qualities:

- OLED-native deep dark surfaces, with nuanced separation instead of ubiquitous outlined panels.
- Elegant, restrained cyan/ice-blue/lilac light as the identity; use gradients only where they serve a real focal point.
- A genuinely responsive *living presence* with organic motion, nuanced depth, and distinct listening/thinking/speaking behavior.
- High editorial quality in typography, hierarchy, spacing, rhythm, and alignment.
- Purposeful motion, elegant transitions, tactile buttons, and restrained haptic-like visual feedback.
- Tool-like clarity for search, messaging, and settings, distinct from the expressive assistant home screen.
- Minimal chrome: content first, not rows of permanent rectangular buttons.
- Warm, helpful microcopy in natural Greek; never technical placeholder prose in a production screen.

**Explicit anti-patterns:** no generic spinning neon rings; no perpetual rainbow orb; no oversized “A” inside a gradient circle; no 1 px outlines around everything; no fake glass blur that LVGL cannot reproduce; no cyberpunk grids, sci-fi hexagons, glowing wireframes, tiny unreadable text, stock gradients, overdecorated particles, inconsistent icon libraries, repeated pill badges, or gratuitous always-on animations. Do not clone an existing brand's exact UI.

Craft the overall aesthetic as a high-end *physical product interface*, tailored to a 480 × 480 touch display.

## 3. BUILD THE DESIGN SYSTEM BEFORE REPLACING SCREENS

Implement centrally defined tokens, not scattered literal colors and sizes. Prefer the repo's existing theme infrastructure; otherwise introduce clear reusable files such as `ui/theme/aura_tokens.*`, `ui/theme/aura_theme.*`, and reusable primitives.

Suggested starting palette (adjust only after screenshot review and contrast checks):

- Canvas / OLED black: `#05070B`
- Elevated surface: `#101722`
- Secondary surface: `#172131`
- Hairline separator: `#293548` with low visual emphasis
- Primary text: `#F5F7FA`
- Secondary text: `#A8B5C7`
- Tertiary text: `#78879B` (never use for important information at unreadable sizes)
- Identity light / electric ice: `#72D7FF`
- Accent lilac: `#AB9BFF`
- Calm mint / success: `#5EDBBB`
- Amber warning: `#F1BD73`
- Soft coral error: `#F18D94`

Use color for meaning; don't apply every accent to every screen. Define semantic colors, states, and accessible disabled/focus appearance. Prefer mostly matte dark surfaces with rare bright moments.

Grid: **4 px base with 8 px rhythm**. Start with 20–24 px main insets and consistent vertical spacings of 8/12/16/24/32 px. Adjust based on the display's actual active area, corner radius, and touch-controller mapping; do not assume all edge pixels are safely tappable.

Typography: select a legally usable, properly licensed font family with **complete Greek and Latin glyph coverage**, including tonos, dialytika, punctuation and numerals. Build only required LVGL font subsets/sizes to control flash usage. No placeholder missing glyphs. Use a deliberate scale, for example:

- Display / hero: 34–42 px
- Screen heading: 26–30 px
- Primary body: 20–22 px
- Secondary body: 16–18 px
- Compact supporting metadata: 14–16 px when legible

Do not force small text to fit; use responsive layout, truncation with clear affordances, and scrolling. Test long Greek words, accented uppercase, line breaks, text wrapping, mixed English/Greek, and 2–5 paragraph answers.

Iconography: one consistent family, optical sizing, consistent stroke width, meaningful labels. Package icons as LVGL-compatible assets and respect licenses.

Controls: aim for **at least 44 × 44 physical display pixels** for frequent touch targets where possible, with spacing against accidental touches; key actions may be larger. The target display is small, so prioritize rather than cram controls. Design all press, disabled, active, loading, and focus states.

Components: `AppShell`, `SystemStatus`, `AIPresence`, `ContextAction`, `IconButton`, `PrimaryAction`, `ActionSheet`, `SheetHeader`, `BottomSheet`, `ListRow`, `SearchResult`, `MessageBubble`, `NotificationToast`, `SettingRow`, `Switch`, `Slider`, `EmptyState`, `ErrorState`, `ProgressIndicator`, `Dialog`, `TextReader`, `PrivacyIndicator` (names indicative). Share tokens and primitives across screens.

## 4. NEW INFORMATION ARCHITECTURE — NO LEGACY 4-BUTTON FOOTER

Remove the existing permanent four-pill bottom navigation. Replace it with navigation that makes sense for a tiny premium smart device:

- **Home is the assistant presence**; it is the default and most immediate interaction.
- Contextual, clearly identifiable actions appear only when useful.
- A single deliberate entry to applications/utilities may open a compact launcher or bottom sheet.
- Settings live behind an unobtrusive but discoverable gesture/icon or launcher destination.
- Search results, answer view, Aoraton, and notifications have intentional screen-specific layouts.
- One consistent Back affordance / swipe-back behavior; no mystery gestures without a visible alternative.
- Use edge gestures only where supported by actual touch input and do not conflict with scrolling.
- A subtle status area shows time, connectivity, microphone/privacy status, and battery without overpowering the visual hierarchy.
- `DEMO`, `LIVE`, provider labels, developer diagnostics, fake notification injectors, and fixture names must not pollute production UI. Development mode must be unmistakably indicated *when enabled* but visually discrete, and inaccessible/disabled by default in the production build.

Define the navigation map and state transitions explicitly before writing screen code. Preserve all reachable existing functionality. Avoid navigation dead ends.

## 5. SIGNATURE HOME — A LIVING AI PRESENCE

Rebuild the main screen completely.

The main visual element must be a **beautiful, original, animated AI presence**, not a static monogram with neon rings. Create a layered luminous organic form that can plausibly be rendered on ESP32-S3. It may combine 2–4 layered gradients/textures, a soft moving contour, light dynamics, and subtle depth; use pre-rendered tiny atlases, bounded procedural drawing, and cached assets only after examining LVGL and memory constraints.

At rest it should feel alive but understated: slow breathing, very small organic deformation, subtle shading or internal light circulation. Do not drain the battery just to animate while idle. Define a dim ambient / screen-off policy compatible with always-on wake-word detection.

Homepage hierarchy:

1. Tiny, polished system indicators.
2. A focused hero presence with carefully composed negative space.
3. One short, warm status line in Greek, e.g. `Είμαι εδώ.` / `Πώς μπορώ να βοηθήσω;` depending on state.
4. A highly discoverable primary interaction (tap to ask / microphone / activate), consistent with actual available audio hardware.
5. One restrained way to open useful tools and recent activity.

Do not fill the entire screen with decorative artwork. The presence should be expressive, and the interaction should be obvious within 2 seconds.

## 6. DISTINCT AI STATE DESIGN — NOT A LABEL CHANGE

Design separate compositions and motion signatures with reusable foundations:

**Idle:** calm, subtle breathing, dark canvas, low power, welcoming single-line guidance.

**Wake detected:** a clear but non-jarring response to the wake word; a fluid 180–300 ms expansion/energy gathering transition and privacy indicator.

**Listening:** the presence becomes open and attentive; microphone activity is indicated by real sound level if available; text says `Σε ακούω…`; clearly visible cancellation and end-listening affordance; no false real-mic claims in simulator mode.

**Processing / thinking:** denser, inward motion, measured rhythm, no aggressive indefinite spinner; streaming partial results may be surfaced if available; show a gentle wait indication that remains responsive.

**Speaking:** animated presence reacts to *actual PCM envelope* received from the Audio Service, with smoothing and minimal lag; never rely on arbitrary random animation when audio samples exist; display clear mute/stop controls.

**Answer / reading:** prioritize the content. The presence shrinks or elegantly moves aside, giving most of the screen to scrollable, readable text or structured result cards. Stop treating a long answer as a tiny paragraph inside a rigid center panel. Provide contextual actions (back, ask follow-up, copy/QR when relevant) without stealing reading space.

**Offline:** remain graceful and usable, with concise explanation and real available offline actions. Do not replace the whole experience with a panicked error screen for transient connectivity loss.

**Error:** calm, human-readable, actionable recovery with `Δοκίμασε ξανά`, `Πίσω` when appropriate; no generic wall of error text and no fake success states.

**Muted / privacy / low battery:** explicit, honest, legible presentation.

Implement one coherent AI state-machine-to-visual-state mapping, interruptible transitions, and state restoration. Rapid transitions such as listening → cancel → idle or processing → network error must not leave orphan animations or overlays.

## 7. REDESIGN EVERY EXISTING PRODUCT SCREEN

Implement actual functional screens, not static placeholders.

**Conversation / answer:** beautiful Greek typography, constrained comfortable line length, ample padding, readable scroll; clearly differentiate user input, AI output, metadata, optional sources, and actionable items. Support long responses, mixed-script text, bullet points and safe basic formatting to the extent supported by actual text renderer. No clipping, overlaps, unusable tiny scrollbars, or unexpectedly hidden content.

**Search results / cards:** functional-first presentation with strong information hierarchy. For a business/telephone result show name, category/address, phone prominently and clearly marked actions: copy number, share/show QR if supported. Never display invented phone data as live. Use list states, pagination/scroll, loading skeleton only when genuinely fetching, and meaningful empty results.

**Notifications:** a clean prioritized list; accurate unread indicators attached to the notification entry point (not Settings); compact preview, timestamp, source, clear dismiss/open behavior. Apply Aoraton privacy settings.

**Settings:** modern grouped rows/sheets for Connectivity, AI Provider, Voice & Audio, Display, Battery & Power, Privacy, Aoraton, Developer options. Favor disclosure/navigation over cramming every control into one card. Actual sliders, switches and dialogs work and persist as appropriate. Diagnostics appear only when requested, never floating over primary controls.

**Aoraton messaging:** cohesive design with room list, unread indicators, encrypted-status clarity, conversation bubbles, robust Greek keyboard/input in simulator, compose-preview-confirm workflow, connection/pairing state, meaningful mock-vs-real distinctions. Preserve E2EE architecture, privacy and explicit confirmation before sending real messages. Do not show generic screenshots of the website as a substitute for native LVGL UI.

**Audio UI:** refined playback and volume controls; actual test audio, speaking animation, error handling and state visibility from the existing Audio Service. Preserve ES8311 firmware and GPIO46 PA control abstraction; no unnecessary audio driver refactor.

**System / onboarding / Wi-Fi / empty / error / update:** design all lifecycle edges so the product never falls back to raw debug-looking UI. Include first-boot onboarding or a minimal setup path only if supported by existing backend.

## 8. MOTION LANGUAGE: PREMIUM, PHYSICAL, RESPONSIVE

Define reusable easing and durations, then implement with LVGL-supported features:

- Micro-feedback/tap: ~90–160 ms.
- Content transition: ~180–280 ms.
- Sheet/overlay: ~240–360 ms where appropriate.
- Wake response: immediate visual acknowledgment, with a smooth continuation.
- Idle presence: deliberately slow, subtle, and power-aware.
- Speaking: updated from audio envelope, with smoothing rather than hard-frame jumps.

These are design starting points, not arbitrary timing obligations if actual hardware profiling reveals constraints.

Never place expensive blocking effects on the UI loop. Avoid layout recalculation every frame. Animate transform/opacity/limited invalidation where supported; cache expensive assets; apply partial redraws / dirty regions; avoid repeatedly redrawing static text. No fake full-screen shader if the ESP32 cannot run it.

Honor `Reduce motion`. Provide reduced-motion alternatives without losing state clarity. Reduce or stop idle animation in low-battery/ambient mode. Include screensaver/dim behavior and mitigation for AMOLED burn-in (e.g. subtle repositioning of persistent static elements if appropriate).

The desktop implementation should look refined at interactive frame rates. The hardware target must be profiled separately; do not promise 60 FPS without measurement. Aim for perceptually smooth high-priority transitions and reliable touch response under network/audio load.

## 9. LVGL / ESP32-S3 IMPLEMENTATION REALITY

Do not design UI effects that exist only in a browser or in a video render. Deliver the visual quality using the project's actual LVGL version and available C/C++ APIs.

Before coding, verify:

- LVGL major/minor version and enabled widgets/features.
- Actual simulator backend and display format.
- Display driver interface and color mode.
- Font memory footprint and Greek glyph coverage.
- Available heap, PSRAM and expected display/audio/network contention.
- Static and dynamic allocations, PSRAM vs internal heap, relevant DMA restrictions.
- Current rendering loop, locks, input, and event dispatch.

Use a common UI code path for Windows and embedded firmware; platform-specific drivers stay behind abstractions. Do not create a beautiful parallel web simulator that diverges from firmware.

At 480 × 480, one full RGB565 frame is about 450 KiB. The board has PSRAM but must also run networking, audio, and future Aoraton. Budget resources rather than allocating unlimited textures and double/triple full-screen buffers. Use tile/partial buffering where appropriate, compressed or preprocessed assets, and asset reuse. Never move large amounts of pixel data unnecessarily.

Implement a lightweight renderer/performance overlay for developer mode with measured FPS, frame/render time, heap, PSRAM and invalidated area where available. No developer overlay in production mode.

## 10. GREEK-FIRST MICROCOPY AND ACCESSIBILITY

Greek is the primary UI language. English can be selected when implemented; do not partially translate in the same screen unless the content is a proper name or technical term.

Improve every label, state caption and error message. Natural, concise examples:

- `Πώς μπορώ να βοηθήσω;`
- `Σε ακούω…`
- `Το επεξεργάζομαι…`
- `Έτοιμο.`
- `Δεν υπάρχει σύνδεση. Μπορείς να συνεχίσεις με τις διαθέσιμες λειτουργίες.`
- `Δεν στάλθηκε. Δοκίμασε ξανά.`
- `Έλεγχος ήχου`
- `Επιβεβαίωση αποστολής`

Never expose raw fixture words such as “deterministic demo”, “mock adapter”, “control panel”, “test fixture”, or “provider exception” to ordinary users. Such labels belong in developer diagnostics.

Ensure contrast, readable body type, visual indicators beyond color alone, predictable back/cancel behavior, generous touch targets, correct focus/state announcements where supported, no critical information shown for less than a readable interval, and clear loading/interruption behavior.

## 11. SIMULATOR IS THE DESIGN REVIEW TARGET

Implement in the **existing native LVGL simulator**, not as a separate product. The simulator must support pointer/mouse as touch, keyboard entry where relevant, state injection and navigation.

Provide a design review/dev panel (not visible in release UI) capable of switching across:

- Idle / home
- Wake detected
- Listening (silence, low input, strong input)
- Thinking (short and long)
- Speaking (actual simulated waveform/audio playback)
- Short answer
- Long multi-paragraph answer
- Search results (0, 1, 5+ items)
- Notifications (empty, unread, sensitive preview off)
- Settings and nested settings
- Aoraton room list / thread / composing / confirmation
- Offline / reconnecting
- Error / recovery
- Low battery / charging
- Dark OLED ambient mode
- Greek/English strings and long-text stress tests

All state changes must use real application state transitions where possible. Never achieve screenshots by rendering unrelated fake pages while the actual UI remains unchanged.

Capture genuine **480 × 480 PNG screenshots** from the simulator for every major state, at consistent scale. Maintain a gallery under `docs/design/screenshots/after/` and store existing before-images if available under `before/`.

Create a contact sheet and evaluate it, not only individual screens. A cohesive product must feel like one family across all states.

## 12. VISUAL QUALITY GATES — SELF-CRITIQUE AND ITERATE

A successful build is NOT a successful redesign.

After each major screen change:

1. Build and launch the actual simulator.
2. Navigate to the actual state with functioning controls.
3. Capture a 480 × 480 screenshot.
4. Inspect the image visually, preferably with the agent's image-view tools if available; otherwise record the limitation and provide screenshots for review.
5. Critique visual hierarchy, alignment, spacing, readability, contrast, touch affordances, atmosphere, distinct states, and consistency.
6. Fix defects and capture another screenshot.
7. Test interactions and any state transitions affected.

Conduct at least two complete review passes across the full screenshot gallery if tooling permits.

Reject screens with:

- Text smaller than necessary to cram content.
- Labels cut off or hidden behind overlays.
- Stranded/floating buttons or overlapping badge positions.
- Multiple competing primary CTAs.
- Badly aligned or inconsistent paddings/radii.
- Hollow/decorative cards without purpose.
- Giant empty space that makes useful content cramped elsewhere.
- Scroll areas that cannot reach the last line.
- Incoherent idle/listening/thinking/speaking designs.
- Buttons that are visually interactive but do nothing.
- User-facing `DEMO`/technical fixtures that ruin production polish.

Do not mark a screen complete until it passes visual and interaction checks.

## 13. FUNCTIONAL AND REGRESSION TESTS

Preserve and test working infrastructure:

- Codex CLI AI bridge and provider switching.
- Real audio playback in Windows simulator; optional TTS and audio-reactive visual states.
- ESP32-S3 firmware code and ES8311/PA_CTRL integration.
- Network and battery status.
- Existing notifications and action handling.
- Aoraton mock or real integration, E2EE boundaries, confirmation before send.
- Settings persistence.
- Simulator startup and input handling.
- OTA/firmware hooks if already present.

Test user journeys:

1. Launch → immediately understand how to ask AURA something.
2. Activate listening → cancel → idle; no stuck visual state.
3. Ask question → thinking → short answer → follow-up.
4. Long Greek answer → scroll to bottom → navigate back.
5. Network failure during response → recover or retry safely.
6. Open search results → read phone/address → copy if implemented.
7. Open settings → change brightness/volume → return without losing state.
8. Receive Aoraton mock message during AI response → view → return.
9. Compose Aoraton message → explicit confirmation → simulated send.
10. Mute and unmute audio during speaking animation.
11. Low battery/dim/reduced-motion behavior.
12. 10 rapid navigations without memory leak, dangling callbacks or corrupted UI.

Automate state/screenshot smoke tests where practical, and keep existing test conventions. Check memory after repeated screen transitions. Run both a simulator build and ESP-IDF compile if toolchains are available, or report blockers accurately.

## 14. REQUIRED IMPLEMENTATION WORKFLOW

**Milestone A — Product audit:** inspect repository and before screenshots; write `UI_AUDIT.md`; map current navigation and component dependencies.

**Milestone B — Design foundation:** write `DESIGN_SYSTEM.md` with tokens, palette, fonts, icon sizing, control specs, component library and motion rules. Implement central theme primitives.

**Milestone C — Signature presence:** implement new AURA identity and Idle, Wake, Listening, Thinking, Speaking scenes using shared animation architecture.

**Milestone D — Content experiences:** rebuild answer/reader, searches/cards, notifications and error/offline states with coherent layouts and natural Greek copy.

**Milestone E — Utility experiences:** redesign settings, audio, diagnostics, and Aoraton messaging views. Preserve all functionality.

**Milestone F — Integration and polish:** wire actual services/events, optimize rendering and power, implement reduced motion, test scroll/touch/text, capture complete gallery and correct defects.

**Milestone G — Handoff:** update docs, execute tests, report exactly which views work, what was visually inspected, build status, performance numbers actually measured, and what awaits the board.

Do not spend the whole session writing documents. Deliver tangible working changes early. If time or context is limited, finish a visually excellent Home + AI-state flow and the shared design tokens before moving outward, then leave a precise resume checkpoint. Never silently abandon incomplete screens.

## 15. DELIVERABLES

Provide or update the following using the repository's conventions:

- `docs/design/UI_AUDIT.md`
- `docs/design/DESIGN_SYSTEM.md`
- `docs/design/NAVIGATION_MAP.md`
- `docs/design/MOTION_SPEC.md`
- `docs/design/SCREEN_REDESIGN_CHECKLIST.md`
- `docs/design/screenshots/before/` (when available)
- `docs/design/screenshots/after/`
- `docs/design/AURA_REDESIGN_CONTACT_SHEET.png`
- Reusable LVGL UI primitives, theme, font assets and animations
- Redesigned, functioning application screens in the simulator
- Smoke/regression tests and build scripts as appropriate
- Project progress/changelog with actual implementation status

Do not create unnecessary duplicates of existing documentation; integrate cleanly.

Your final response must show:

1. Actual files modified and created.
2. What changed visually and in interaction design.
3. How to launch and operate the simulator.
4. Screenshots/contact-sheet location.
5. What interactions were tested and their results.
6. Simulator build and firmware build outcomes.
7. Actual, measured performance if available; otherwise state unmeasured.
8. Remaining work and physical hardware validation requirements.

## 16. DEFINITION OF DONE

The redesign is not complete merely because the app compiles or because the colors are different. It is complete only when:

- AURA unmistakably looks and feels like a modern, polished, high-end AI device.
- The default home has a distinctive, alive, performant assistant presence instead of the old `A`-in-a-circle logo.
- Listening, thinking, speaking, answering, offline and error have distinct, coherent visual identities.
- The legacy persistent four-button footer is replaced by elegant, functional navigation.
- Greek text looks excellent at 480 × 480, including long results and multiple paragraphs.
- Settings, notifications, searches, audio and Aoraton screens share one design system.
- Controls are responsive, real and touch-appropriate.
- Real Windows audio and the Codex workflow remain operational.
- LVGL simulator screenshots have been captured and visually reviewed.
- No clipping, collision, unusable scrolling, broken navigation or fake active controls remain in reviewed flows.
- Embedded resource constraints and power-saving modes are explicitly considered.
- Existing Aoraton encryption/security behaviors are not degraded.
- No production data is changed for the sake of visual testing.
- Hardware behavior is not claimed tested before device arrival.

### FINAL COMMAND

**Audit the existing AURA project and proceed with the implementation of this premium UI/UX redesign now. Do not ask the user to choose a style, do not restart the repository, do not replace working LVGL with a mock website, and do not stop after creating concept art. Make strong design decisions, ship functioning changes, capture the real simulator, critique your screenshots, improve them, and preserve every existing functional integration.**

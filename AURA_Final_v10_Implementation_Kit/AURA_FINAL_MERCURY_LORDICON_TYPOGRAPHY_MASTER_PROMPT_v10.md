# AURA — FINAL MASTER IMPLEMENTATION DIRECTIVE v10.0
## Mercury Orb + Complete Lordicon System + Premium Typography + Functional UI Recovery
### For OpenAI Codex CLI and Claude Code — existing project, Windows 10 LVGL simulator and Waveshare ESP32-S3-Touch-AMOLED-2.16

> **This is a final, consolidated corrective implementation specification.** It supersedes conflicting visual, animation, icon and typography instructions in v3–v9.1. It does **not** supersede security, hardware, functionality or project-continuity requirements. **Implement inside the existing AURA repository; do not recreate the project.**

## 0. MISSION AND USER-APPROVED DESIGN

Build a polished, functional, touch-first AI companion for the **480×480 square AMOLED** on **Waveshare ESP32-S3-Touch-AMOLED-2.16**, with native Windows LVGL simulator as the immediate verification platform. Preserve the existing AI/Codex bridge, audio playback, device settings, 18 screen flows, Aoraton mock/integration progress and ESP-IDF architecture.

The user has rejected previous outcomes involving cropped static hero art, rotating spokes and cheap neon rings instead of a real animated orb; unrelated icon styles; tiny/plain fonts; small hard-to-read Greek labels; placeholder screens or dead controls; SIM/cellular labels; and controls clickable only through their captions.

**Authoritative AI visual reference:** https://voiceorbs.vercel.app/orbs/mercury-orb — *Mercury Orb*, the soft liquid-metal/chrome, pearlescent fluid shader. The same living orb must appear in **Idle, Listening, Thinking, Speaking**, with state-specific motion, not four unrelated visuals. Preserve premium black/cyan/violet identity without crowding the small screen.

**Authoritative icons:** https://lordicon.com — a consistent, licensed iconography system covering existing controls on all 18 screens. **Authoritative typography:** a deliberately designed combination of **Space Grotesk for the Latin “AURA” wordmark** and **IBM Plex Sans with verified Greek support for all UI text**, with a documented Inter fallback if testing proves it preferable. No font downloads are included in this prompt.

Do not add new features just to show icons. Do not call a mocked capability “live”. Do not silently add an API/server requirement. No phone/SIM/mobile carrier interface: the hardware is Wi-Fi + BLE only.

## 1. FIRST: SAFE REPOSITORY AND FEATURE AUDIT

1. Open the current AURA repository; inspect Git status, all outstanding edits, current LVGL version, Windows simulator, ESP-IDF project and configuration. Create a **non-destructive, reversible checkpoint**; never use `git reset --hard`, `git clean`, overwrite unknown changes, or deploy production updates.
2. Read the original seed and existing v8, v9, v9.1 directives as history, then apply **this v10** for UI, animations, icons and fonts where they conflict.
3. Inspect user-supplied before/after screenshots and reference artwork if present in the repository; identify exact visual regressions, clipping sources, font definitions, old icon assets, and event hitbox problems. If reference assets are missing, report which inputs are missing, use provided URL as the animation reference and continue with source-based verification.
4. Inventory **every actual route and every active control**: screen ID, target, service called, callback, data provider, LIVE/MOCK/DISABLED designation, and regression test. Build `docs/ui/FEATURE_AND_ROUTE_PARITY.md` before changing layouts.
5. Identify actual implemented functions. Preserve working AI, Codex CLI host bridge, audio engine, notifications, network UI, settings, device diagnostics, Aoraton simulator, compose and confirmation. Do not silently erase them to make the interface more attractive.
6. The external Aoraton source, if the integration requires it, lives at `C:\Users\polis\Documents\incognitochat`. Treat it as **read-only by default**; do not modify or deploy it without explicit permission. Preserve its encryption and privacy boundaries.

## 2. VISUAL LANGUAGE / DESIGN SYSTEM

Create or improve a **single-source-of-truth Design System**. Use color tokens, typography tokens, spacing tokens, radii, motion tokens, elevations/glow intensities, icon roles and interaction states instead of arbitrary values per screen.

Visual direction:
- Background: true AMOLED black near `#05060B`; dark raised surfaces around `#101727` and `#16213A` where needed.
- Accents: electric cyan (`#41D8FF`), refined azure (`#328DFF`), violet (`#865CFF`), magenta (`#CF5DFA`), used selectively, not every component simultaneously.
- Text: off-white (`#F4F7FF`) primary; muted cool-gray (`#A3B3CD`) secondary; ensure measured contrast. Yellow/gold only for meaningful warnings.
- Depth: tightly controlled edge highlight and glow; avoid fake blur, unreadable glass, rainbow clutter and oversized glowing circles.
- Content is the priority on messages, answers, settings, errors and diagnostics; use hero graphics primarily in AI states.
- Design for **480×480 physical square**, not a round watch. **No full-screen circular ring or watch-face mask.** Small orb, circular buttons and battery gauges remain appropriate.
- Allocate header, content and action/footer regions intentionally; enforce safe margins and avoid clipping on **all four sides**. Never draw decorative elements over touch controls or text.

Use LVGL components and reusable styles. No separate React/Electron replacement for the real simulator. Desktop shader rendering is allowed **only as an offline asset-production tool**.

## 3. PREMIUM TYPOGRAPHY: REQUIRED COMPLETE REDESIGN

**The existing font treatment is rejected.** Stop using an undifferentiated default font for every label. Implement a proper typographic system that is modern, expressive, crisp and *correct in Greek*.

### 3.1 Type families — deliberately limited

- **AURA brand wordmark:** **Space Grotesk** Medium/SemiBold, uppercase Latin `A U R A`, measured tracking. Use for short *Latin branding only*, because Greek glyph coverage is not guaranteed by that font. Official source: https://github.com/floriankarsten/space-grotesk (SIL OFL 1.1).
- **Primary Greek + English UI:** **IBM Plex Sans**, with *verified Greek glyphs in the selected exact font files and weights*. Use Regular, Medium and SemiBold (avoid needing too many weights). Official source: https://github.com/IBM/plex (SIL OFL 1.1). No arbitrary font substitutions or fallback tofu boxes.
- **Alternative for A/B testing only:** **Inter** (https://github.com/rsms/inter; SIL OFL). Compare actual screenshots of IBM Plex Sans and Inter for small Greek labels; select ONE as final UI family, not a random mixture.
- **Diagnostics numbers / technical values:** optional **IBM Plex Mono** selectively if it improves data alignment; do not use monospace for ordinary Greek body text.

### 3.2 Type scale — starting specification for 480×480, tune with screenshots

| Role | Approx. size | Weight | Usage |
| --- | ---: | --- | --- |
| Brand AURA | 23–29 px | Space Grotesk 600 | Home identity only; deliberate letter spacing |
| Main hero metric | 34–44 px | IBM Plex Sans 500/600 | Temperature, battery percentage, prominent status |
| Screen title | 23–26 px | IBM Plex Sans 600 | Settings, Search, Aoraton, Offline, Error |
| Section heading | 18–21 px | IBM Plex Sans 500/600 | Cards and subsections |
| Body text / AI answers | 17–19 px | IBM Plex Sans 400 | Greek conversational content, wrapping |
| Interactive label | 16–18 px | IBM Plex Sans 500 | Buttons, rows, action tiles |
| Secondary / metadata | 13–15 px | IBM Plex Sans 400 | Status subtext, timestamps; never too tiny |
| Top status | 13–15 px | IBM Plex Sans 500 | Time, battery; must read clearly |

These are **starting values**, not permission to overflow. Validate against real physical-pixel screenshots at 100% and realistic physical size. Keep a *minimum readable level*, reduce text length or paginate rather than shrinking everything to 9 px. Use leading about 1.15–1.30× as appropriate, consistent margins and text wraps. Use semantic contrast, not gray-on-black microtext.

### 3.3 Typography interaction and micro-details

- Consistent baseline grid and left align for lists/messages; intentional center align only for hero and isolated status screens.
- Use actual Greek punctuation/tonos, `Ά`, `Έ`, `Ή`, `Ί`, `Ό`, `Ύ`, `Ώ`, `ϊ`, `ΐ`, `ΰ`, `ς`, `€`, en/em dashes, curly quotes, numerals and degree symbols. Verify UTF-8.
- Make status digits stable; avoid jittering when clock or battery changes. If OpenType tabular figures are unavailable after LVGL conversion, set fixed-width numeric containers and appropriate alignments.
- Use carefully restrained letter spacing for `AURA`, but do **not** letter-space Greek paragraph text.
- Text in Answer, Aoraton and Search should wrap with correct ellipsis only where appropriate; **long answers must scroll** with clear affordance, never overlap buttons.
- Do not burn text into bitmap backgrounds; keep it native LVGL text for updates, interaction, localization and readability.
- Maintain a `TypographyTokens` / `AuraFonts` registry and reusable components/styles; no ad-hoc font assignment across 18 screens.

### 3.4 Real LVGL font build pipeline — NOT CSS-only changes

Inspect the installed LVGL version and use its compatible `lv_font_conv` or documented custom-font API. Build a reproducible script (e.g. `tools/fonts/build_fonts.*`) from lawfully acquired font source files; include font source/version and OFL notice in documentation.

Generate only the needed sizes, weights and glyph ranges; cover basic Latin, Greek and monotonic Greek with tonos, punctuation, euro, numerals and dynamic user text. Consider U+0020–007E, U+00A0–00FF, U+0370–03FF, selected U+1F00–1FFF only when useful, and needed punctuation/symbol codepoints; **verify actual coverage by inspecting real font files**. Test composed and decomposed Greek inputs and normalize where appropriate. Do not waste tens of MB on unnecessary Unicode ranges.

Use suitable bits-per-pixel/antialiasing for the AMOLED and document footprint. Ensure the font storage and runtime strategy actually works in the embedded ESP32-S3 build with its Flash/PSRAM restrictions and without an inserted SD card where basic UI assets are required.

Verify font rendering on both Windows simulator and ESP-IDF. Do not silently rely on Windows host fonts that differ from the fonts deployed to the physical device.

**Required Greek font regression samples** (capture in real simulator):
- `Ακούω…  Σκέφτομαι…  Σου απαντώ…`
- `Δεν υπάρχει σύνδεση`
- `Δεν ολοκληρώθηκε — Προσπάθησε ξανά`
- `Σύνδεση Wi-Fi • Bluetooth • Μπαταρία 92%`
- `Ειδοποιήσεις και μηνύματα Aoraton`
- `Βάσια, μπορείς να με πάρεις τηλέφωνο άμεσα;`
- `Θερμοκρασία 22°C — υγρασία 45% — άνεμος 12 km/h`
- `Ά Έ Ή Ί Ό Ύ Ώ ά έ ή ί ό ύ ώ ϊ ϋ ΐ ΰ ς €`

**No release if Greek text is missing glyphs, clipped vertically, overscaled, illegible, inconsistently weighted, or mapped to generic fallback fonts.**

## 4. MERCURY ORB — FOUR TRUE ANIMATED STATES

Reference: https://voiceorbs.vercel.app/orbs/mercury-orb ; project source: https://github.com/amunozdev/voiceorbs . Inspect source and third-party shader/assets licenses and credit/notice obligations. The Mercury original is a **browser GPU shader**, not ESP-IDF/LVGL firmware. Never claim the ESP32-S3 executes React/WebGL directly.

Create a **single reusable `AuraMercuryView`** with the same liquid-metal material language and unified position/size across four states:

**Idle / Home:** slow continuous soft chrome flow and gentle optical breathing; visible full orb, accurately centered; premium AURA wordmark and primary “Πες κάτι…” action. No static wallpaper that hides a cropped orb.

**Listening:** the *same* Mercury orb responds to microphone envelope: attack/release smoothing, clear quiet state, stronger fluid ripples as amplitude increases. Show “Ακούω…” and functional cancel/end input. If mic capture is simulated, clearly indicate simulator status.

**Thinking:** the same liquid-metal body with more active internal flow/ripple deformation, not concentric radar rings or a giant unrelated vortex background. “Σκέφτομαι…” readable; cancellation if supported.

**Speaking:** the same orb reacts to actual PCM playback amplitude; keep pause/mute/stop semantics accurate. No fake lip-sync when no audio. Display text alongside or transition to answer where needed.

All states must have **real temporal change** in the dominant hero, not 8 snapshots of an almost static image with a moving spinner. Transition smoothly; resume a coherent phase when possible. Do not put a smaller unrelated waveform over a large static poster and call it the result.

### Rendering strategy — mandatory feasibility decision

Investigate, prototype and document one of:
1. **Offline rendering/export** of licensed Mercury shader states into a compressed embedded-friendly looping sequence.
2. **Hybrid**: pre-rendered orb frames plus small native, audio-reactive shader-like overlays.
3. A genuinely faithful native implementation, only if benchmarked and visually convincing.

Choose using measured visual quality, CPU, RAM, Flash, SD I/O, battery cost and refresh latency. If high-quality GPU rendering is feasible in the simulator but not on the ESP32-S3, keep compatible behavior with separately optimized assets rather than silently replacing the design with rotating spokes.

An initial embedded target may explore a hero area around 170–220 px at 12–20 FPS, but these are **provisional engineering targets, not promised results**. Benchmark and revise. Keep critical UI assets available without microSD; support optional microSD for large animation sets with clear offline fallback. Avoid full-screen uncompressed frame sequences and repeated heap churn.

No accidental right/bottom cropping; test all four edges, hero center, header overlap and CTA hitbox. Keep efficient LVGL partial invalidation and bounded draw buffers. Stop/reduce motion under low power and respect simulator reduced-motion setting.

## 5. FULL LORDICON ICONOGRAPHY — ALL EXISTING SCREENS

The previous 6-icon implementation is inadequate. Create a **semantic icon registry of roughly 40–50 roles** for the actions that actually exist; it does not imply adding 40–50 visible icons or screens. Prefer a coherent **Lordicon System Outline** family; permit an explicit licensed alternative if the required icon/format/license is unavailable. Avoid mixing unrelated solid/outline styles and awkward tiny glyphs.

### Roles to audit and map

- **Navigation:** home, back, forward, menu, close, apps, chevron, refresh, more.
- **AI:** microphone, mic muted, chat, search, send, cancel, thinking, copy, confirmation, voice.
- **Audio:** play, pause, stop, speaker, volume up/down, muted, test sound, record.
- **Aoraton:** message, inbox, unread, notification, compose, reply, send, room, lock, encrypted, connection.
- **Network/device:** Wi-Fi, Wi-Fi disconnected, Bluetooth/BLE, battery, charging, display, brightness, settings, storage/SD, CPU, memory.
- **Tools (only existing features):** weather/sun/rain, location, calendar, reminder, timer, camera, tools.
- **Alerts:** warning triangle, error, success, retry, help, information, offline.

Verified starting **Lordicon candidate pages**, to be checked again at implementation time:
- Wi-Fi: https://lordicon.com/icons/system/outline/64-wifi
- Microphone: https://lordicon.com/icons/system/outline/188-microphone
- Search: https://lordicon.com/icons/system/outline/19-magnifier
- Home: https://lordicon.com/icons/system/outline/63-home
- Inbox: https://lordicon.com/icons/system/outline/361-inbox
- Warning: https://lordicon.com/icons/system/outline/1140-warning-triangle
- Calendar: https://lordicon.com/icons/system/outline/364-calendar-dots
- Message: https://lordicon.com/icons/system/outline/4198-message
- Send: https://lordicon.com/icons/system/outline/4214-send-horizontal
- Settings: https://lordicon.com/icons/system/outline/39-cog
- Audio: https://lordicon.com/icons/system/outline/1041-speaker
- Location: https://lordicon.com/icons/system/outline/18-location-pin
- App grid: https://lordicon.com/icons/system/outline/1388-grid-bento

**Do not invent URL/IDs.** For each icon, document official URL, style, free/PRO status, lawful source/export, license, intended role, final bitmap/animated asset and fallback. Validate legal use at https://lordicon.com/licenses . Free uses generally require attribution; PRO assets require an appropriate acquired license. If not licensed, use a legally available fallback, do not scrape paid previews. Include `Settings > About > Credits` or equivalent project-site attribution as appropriate, and maintain an asset manifest.

**Important:** Lordicon's Lottie/web component cannot simply be embedded as a web player in ESP-IDF. Export eligible icons offline into compact LVGL-compatible assets, using static versions for persistent status icons and short on-press/on-state animations only where valuable. Icons must never consume more resource budget than the AI orb. Respect battery conditions.

### Offline and Error: user-rejected icons — rebuild properly

**Offline:** a clearly recognizable **Wi-Fi-off / Wi-Fi disconnect** outline glyph, accurately centered and sized (no crude circle crossed by an ambiguous slash); title `Δεν υπάρχει σύνδεση`, concise description, **Retry** genuinely checks connectivity or visibly indicates simulated mode. Align title, icon, description and button on a proper vertical grid.

**Error:** an unmistakable **warning triangle** / error symbol from the coherent icon family (not a large mismatched filled red dot); title `Δεν ολοκληρώθηκε`, useful error detail when available, Retry and Home actions both functional. Use restrained coral accent, not aggressive flashing red.

## 6. PREMIUM HEADER / LIVE DEVICE STATUS

Redesign a consistent touch-safe status bar; no old `SIM`, cellular bars, carrier text, fake mobile signal or fixed demo values presented as real. Use deliberate typography/icon sizes, not microtext:
- Left: real Wi-Fi state icon; BLE indicator only if meaningful.
- Center: HH:MM local time from the currently available RTC/NTP/system source; visible changes over time.
- Right: battery symbol + percentage from AXP2101 on actual hardware; clearly `SIM` or development indicator in simulator **outside actual device status**, without confusing simulated battery with measured battery.
- Respect a well-defined horizontal grid and adequate spacing; avoid overlap with back button and screen titles.
- Host simulator should show trustworthy host values when accessible, otherwise **explicitly simulated**, not frozen `18:24` or `92%` without context.
- Keep a consistent baseline across all 18 screens. No fabricated GPS, cellular, Bluetooth audio or charging status.

## 7. TOUCH-FIRST COMPONENTS AND ROUTING

This device has a capacitive touchscreen. Treat whole `ActionTile`/button rectangle as **one interactive target**: icon, label, background and the gap between them trigger the same action once. Use the exact installed LVGL version's event and flag API. Inspect child flags, overlapping decorations, hit-testing, pointer capture, event bubbling, parent callbacks and stacking order. Background graphics must not intercept touches.

Aim for comfortable ~44×44 px or larger hit regions when feasible and never rely on a tiny glyph's exact silhouette. Provide immediate pressed/selected feedback, sensible scrolling inertia, back navigation, no orphan screens, and a real disable state for unfinished features. Check touch-vs-scroll conflict in Aoraton messages, settings and long answers. Do not add pointless navigation buttons to visually fill space.

**Every enabled icon/control must do something truthful**, backed by an existing working feature/service. If it is MOCK, label it. If unavailable, disabled + reason; don't route to a misleading decorative page. No regression of AI, audio playback, Search, Aoraton mock flows, diagnostics, offline handling, Wi-Fi or settings.

## 8. SCREEN-BY-SCREEN PARITY — ALL 18

1. **Home:** centered living Mercury, brand typography, primary AI action, menu; no crop.
2. **Listening:** real active listening/Mercury response or marked simulator source; cancel/end.
3. **Thinking:** real liquid-metal processing motion; cancel where supported.
4. **Speaking:** audio-reactive Mercury, actual playback state and controls.
5. **Answer:** readable question/answer hierarchy, clear ask-again action, scrolling where needed.
6. **Answer long:** multi-line Greek readability, functional scroll, no text hidden behind CTA.
7. **Quick actions:** only actual destination icons, large full-tile touch regions, coherent labels.
8. **Notifications:** functional opening/dismiss, timestamps, no static fake live events.
9. **Settings:** row actions, provider/display/audio/network, status controls accurate.
10. **Audio:** actual Windows host audio playback in simulator; volume/mute/stop/test tied to service; ES8311 firmware backend preserved.
11. **Aoraton rooms:** preserve existing MOCK vs real distinctions and unread state.
12. **Aoraton chat:** message list and scroll, entry to composition, privacy labels.
13. **Aoraton compose:** Greek typing/keyboard, edit/preview, secure draft.
14. **Aoraton confirm:** recipient/room and exact message, explicit confirmation before sending, truthful outcome; no production sends in automatic tests.
15. **Offline:** new icon, clear message, real Retry behavior.
16. **Error:** new icon, truthful error status, Retry/Home.
17. **Battery/device status:** correct data provider or honest simulator label, clean gauge and relevant actions.
18. **Diagnostics:** font-readable, actual measured host/device counters with labels, no invented metrics, sensible scroll.

Also inventory any extra routes actually in repository; preserve real features rather than assuming exactly 18 is the entire application.

## 9. AUDIO, AI, AORATON — NO REGRESSIONS

- Preserve **SDL/Windows native audio**, test WAV/PCM play/pause/volume/mute and actual amplitude event output.
- Preserve **ESP-IDF + ES8311** audio subsystem and documented amplifier board control. **Do not arbitrarily drive GPIO46** without validating it against the official BSP/schematic and ESP32-S3 GPIO/strapping limitations.
- Preserve desktop **Codex CLI** as a host-only text AI provider. It does not run inside the ESP32. Keep a clean provider boundary for future OpenAI/Claude APIs and optional Greek TTS; no API keys in firmware/repo.
- Preserve Aoraton's existing mock/real implementation level; do not fabricate live Firebase or encrypted messaging. Existing external project remains read-only unless authorized. AI may prepare a message but must ask the user before the actual send.
- Incoming Aoraton message bodies must remain untrusted and must not become executable device/agent instructions or be uploaded to AI automatically.

## 10. PERFORMANCE, MEMORY AND POWER

Target hardware: ESP32-S3 with 8 MB PSRAM, 16 MB Flash, battery and optional microSD. The desktop simulator is a behavior/visual testbed, **not proof of firmware FPS or battery endurance**.

Track:
- Per-animation frame size, FPS, duration, compressed size, decoder cost, PSRAM cache and SD access.
- UI font size per weight/range, glyph count and Flash/PSRAM footprint.
- Icon asset size and animation loops.
- LVGL display buffer, audio buffers, Wi-Fi stack and navigation state.
- Average/max FPS and per-frame timing, heap/PSRAM headroom, UI responsiveness, battery-aware modes.

Use partial redraw/invalidation; no repaint of the whole 480×480 scene every tick just to animate a 180px orb. Keep static texts pixel-stable during animation. No expensive effects beneath scrolling text. Reduce FPS and optional icon animation for low battery; preserve the core assistant UX. Fail gracefully when SD or audio is absent. Profile actual device when received; never invent benchmarks.

## 11. IMPLEMENTATION ORDER — FOLLOW STRICTLY

**Milestone 0 — Preserve & audit:** snapshot existing work, enumerate all real controls, capture before screenshots, determine fonts/asset/rendering issues.

**Milestone 1 — Typography FIRST:** produce sample sheet with Space Grotesk `AURA`, IBM Plex Sans Greek at all sizes, Inter comparator, complete glyph tests. Convert to LVGL fonts and apply globally, rebuild status and shared text components. Fix truncations.

**Milestone 2 — Icon registry:** inventory ~40–50 semantic roles, pick legal Lordicon assets, convert for LVGL, style Back/Home/Audio/Aoraton/Settings and Offline/Error; make all hitboxes functional. Document license manifest.

**Milestone 3 — Mercury proof of concept:** inspect original shader source/licenses, render a licensed animation sample for one state, integrate it with LVGL simulator, prove multi-frame motion and no cropping; compare visually against reference. Do **not** proceed with all states if result resembles spokes or a static bitmap.

**Milestone 4 — Four states:** unify idle/listening/thinking/speaking, connect lifecycle and microphone/PCM envelopes, handle transitions/cancellation, ensure no animation intercepts input.

**Milestone 5 — All-screen polish:** adjust cards, lists, Greek text, system status, touch areas and route fidelity across 18 screens, without inventing features.

**Milestone 6 — Performance & regression:** simulator screenshots, multi-frame animation strips, click path tests, memory/FPS profiling, hardware build where toolchain is available.

Finish each milestone with a working simulator before moving on. Do not leave a half-migrated app that only renders one polished screen.

## 12. HARD ACCEPTANCE GATES — NO EARLY SUCCESS CLAIMS

### Visual & typography
- 18 screenshots at exactly **480×480**, consistent aesthetic, clean layout, no full-screen circular frame.
- **Typography proof**: Space Grotesk brand vs verified IBM Plex Sans Greek UI. Greek accents/glyphs all correct, hierarchy visible, no 9px placeholder labels; answer/chat typography readable and scrollable.
- Offline/Error feature the **new appropriate icons** and aligned layouts.
- No `SIM`/cellular indicators anywhere; clock/battery/Wi-Fi/BLE sources marked accurately.

### Animation
- Capture **at least 8 time-separated frames per state** (32+ total), plus a short video/GIF/strip if feasible.
- Compare Mercury flow: actual liquid material evolution in the main orb, not just unchanged background + moving rings.
- Listening changes with real or clearly simulated level; Speaking with actual PCM when sound plays; Thinking remains fluid; Idle gently alive.
- No right/bottom clipping; animation stays within UI safe area. Frame time, memory and simulator responsiveness measured.

### Interaction
- For every enabled button in every screen: click center of icon, circle/card edge, label, and empty tile interior. **Same action once per press.** Validate back routes and no invisible overlays.
- Aoraton compose/confirm remains protected; no accidental send. Offline Retry and Error Retry/Home actually work. Audio buttons work via real service.
- Automated regression tests for theme/font registry, routes, icon mapping, event dispatch and mock provider boundaries. Record limitations when GUI automation isn't available; don't claim tests were executed when not.

### Firmware integrity
- Build native simulator and firmware if installed; report exact commands, results and environment. No hardware-connected speaker, battery, touch performance or Wi-Fi tests can be marked passed before board arrival.
- No secret leakage; all used fonts/icons/assets are properly licensed with credit/notice manifest.

## 13. DOCUMENTATION & DELIVERABLES

Create or update (adapt to current repo layout):

- `docs/ui/AURA_V10_DESIGN_SYSTEM.md` — tokens, colors, components, spacing, screen grid.
- `docs/ui/TYPOGRAPHY_SPEC_AND_GREEK_GLYPH_TESTS.md` — chosen fonts, sizes, codepoints, rendering proof, conversion/build script.
- `docs/ui/ICON_REGISTRY_AND_LICENSES.md` — all semantic roles, verified Lordicon URLs, legal asset exports, fallbacks, credits.
- `docs/ui/MERCURY_IMPLEMENTATION.md` — source/license, asset conversion, motion states, codec, memory/FPS estimates/measurements.
- `docs/ui/FEATURE_AND_ROUTE_PARITY.md` — screens/controls/services, LIVE/MOCK/DISABLED.
- `docs/ui/QA_V10.md` — screenshots, animation frames, visual defects corrected, hitbox test coverage, build/test status.
- Updated project roadmap and progress report.

At completion, summarize exact changed files, executed commands, passing/failing tests, screenshots/artifact locations, actual visual match, outstanding blockers and hardware-validation steps. Do not report “done” merely because code compiles or one mock screen looks polished.

## 14. FINAL EXECUTION ORDER

1. Read this ENTIRE prompt.
2. Inspect and preserve the CURRENT AURA repo state.
3. Audit and document reality.
4. **Implement the new premium fonts and typography system, including native LVGL Greek glyph support.**
5. Replace/normalize iconography through a legally compliant complete Lordicon registry.
6. Implement one authentic Mercury state end-to-end; validate, then implement all four.
7. Fix all controls, layouts and status bar across the existing app, preserving all real features.
8. Run the native simulator, capture screenshots + time-separated frame evidence, perform real touch-path testing and regression tests, measure performance, correct failures.
9. Report findings honestly, with **SIMULATOR VERIFIED / FIRMWARE BUILD VERIFIED / HARDWARE UNTESTED** labels.

**Do not stop after an implementation plan. Do not recreate the project. Do not introduce cosmetic icons or dead UI. Implement and validate the complete v10 correction in the existing AURA application now.**

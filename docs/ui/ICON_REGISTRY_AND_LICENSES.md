# AURA v10 icon registry and licenses

## Active implementation

AURA exposes 74 semantic `IconRole` values backed by 71 unique Lucide 1.59.0 assets. The icons are generated as 48×48, one-channel A8 LVGL images, so a component can tint the same bitmap cyan, violet, mint, amber, coral or white without duplicating payload.

The complete machine-readable role/source mapping is `shared/assets/icons/manifest.json`. `shared/ui/src/aura_icons.cpp` is the only runtime role resolver, and `aura_ui_assets_tests` fails if a role is null, not A8, not 48×48, or lacks a name.

Role groups:

- Navigation: Home, Back, Forward, Menu, Close, Apps, Chevron, Refresh, More.
- Assistant: Microphone, MicMuted, Chat, Search, Send, Cancel, Thinking, Copy, Confirmation, Voice.
- Audio: Play, Pause, Stop, Speaker, VolumeDown, VolumeUp, Muted, TestSound, Record.
- Messaging: Message, Inbox, Unread, Notification, Compose, Reply, Room, Lock, Encrypted, Connection.
- Device: Wifi, WifiOff, Bluetooth, Battery, Charging, Display, Brightness, Settings, Storage, Cpu, Memory.
- Utilities and status: Weather, Location, Calendar, Reminder, Timer, Camera, Tools, Warning, Error, Success, Retry, Help, Information, Offline, Audio, Delete, Visibility, Power, List, File, Edit, Calculator, Image, Navigation, Globe.

The compiled native icon object is 168,160 bytes. Raw bitmap payload is 71 × 48 × 48 = 163,584 bytes; descriptors account for the remainder. Icons are static and do not consume animation buffers.

## Legal decision

Lordicon was reviewed because v10 requests that visual language. The manifest records 13 verified candidate page URLs. No purchased/pro export files or entitlement were supplied, and free Lordicon assets may not be scraped, modified and redistributed as generated firmware frames. AURA therefore uses the specification-permitted lawful fallback instead of pretending that Lucide files are Lordicon.

The active Lucide icons are ISC licensed. The full notice is `shared/assets/licenses/LUCIDE_ISC.txt`; attribution is also present in `shared/assets/THIRD_PARTY.md`.

If eligible Lordicon exports are supplied later:

1. Record the asset ID, original URL, account/license evidence, export settings and checksum.
2. Keep the source outside generated C and retain the applicable notice.
3. Convert only allowed exports into bounded static A8 or short state-transition sequences.
4. Preserve the existing `IconRole` API and rerun interaction, size and memory tests.

Do not embed Lordicon's web component or Lottie player into firmware and do not add perpetual decorative icon loops.

## Generation and interaction

`tools/icons/build-icons.ps1` invokes `tools/icons/generate-icons.mjs` to reproduce the A8 assets and manifest. Children are non-clickable; the surrounding button, circular well, card or row owns the callback. This makes the icon center, label, border and empty tile interior produce the same action once.

Offline uses `WifiOff` in a violet glass well. Error uses `Warning` in a coral glass well. Both are semantic assets rather than slashed text glyphs and both retain working Retry/Home routes.

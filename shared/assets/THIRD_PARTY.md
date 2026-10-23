# Third-party assets and notices

The repository keeps the complete license texts under `shared/assets/licenses/`. Generated C assets contain no credentials or personal data.

## IBM Plex Sans

AURA UI text uses IBM Plex Sans Regular, Medium and SemiBold; the clock and large numbers use IBM Plex Sans Light. Sources are pinned to IBM Plex commit `71f558992315a12ca1f06a2215d57962a1992162` and distributed under the SIL Open Font License 1.1.

- Source: <https://github.com/IBM/plex>
- Notice: `licenses/IBM_PLEX_OFL.txt`
- Generated assets: `fonts/aura_font_plex_*.c`

## Space Grotesk

The AURA wordmark uses Space Grotesk Medium, pinned to commit `03507d024a01282884232081fc6011c09ff4e849`, under the SIL Open Font License 1.1.

- Source: <https://github.com/floriankarsten/space-grotesk>
- Notice: `licenses/SPACE_GROTESK_OFL.txt`
- Generated asset: `fonts/aura_font_space_medium_26.c`

## Lucide

The active semantic icon set is generated from Lucide 1.59.0 SVG paths under the ISC License. AURA stores them as recolorable 64×64 A8 LVGL images.

- Source: <https://github.com/lucide-icons/lucide>
- Notice: `licenses/LUCIDE_ISC.txt`
- Role/source manifest: `icons/manifest.json`
- Generated assets: `icons/aura_icon_assets.cpp`

Lordicon candidates were audited, but no Lordicon animation export or PRO entitlement was supplied. The free Lordicon terms require attribution and prohibit derivative asset modification, so the project does not scrape, convert, or redistribute those files. Candidate URLs and the lawful Lucide fallback decision are recorded in the manifest.

## VoiceOrbs Mercury reference

The Mercury material and motion were studied from VoiceOrbs, commit `8b2799917734f52c9293a489e08982ca5b0187e9`, under the MIT License. AURA does not embed React, WebGL, Three.js, GLSL, or copied shader output. `AuraMercuryView` is a new bounded CPU/LVGL implementation adapted to the ESP32-S3 rendering model.

- Reference: <https://voiceorbs.vercel.app/orbs/mercury-orb>
- Source: <https://github.com/amunozdev/voiceorbs>
- Notice: `licenses/VOICEORBS_MIT.txt`

## Earlier generated visual layers

The v4 orb/wave/vortex/ribbon and fictional map images in `design/generated/` were created specifically for this project with OpenAI image generation on 2026-10-18. They remain in repository history for comparison, but the four primary AI states now render the shared procedural Mercury component.

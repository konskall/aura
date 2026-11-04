# Third-party assets and notices

The repository keeps the complete license texts under `shared/assets/licenses/`. Generated C assets contain no credentials or personal data.

## IBM Plex Sans

AURA UI text uses IBM Plex Sans Regular, Medium and SemiBold; the clock and large numbers use IBM Plex Sans Light. Sources are pinned to IBM Plex commit `71f558992315a12ca1f06a2215d57962a1992162` and distributed under the SIL Open Font License 1.1.

- Source: <https://github.com/IBM/plex>
- Notice: `licenses/IBM_PLEX_OFL.txt`
- Generated assets: `fonts/aura_font_plex_*.c`

## Space Grotesk

The legacy typography proof retains Space Grotesk Medium, pinned to commit `03507d024a01282884232081fc6011c09ff4e849`, under the SIL Open Font License 1.1. Production wordmark placements use the approved owner-provided artwork below.

- Source: <https://github.com/floriankarsten/space-grotesk>
- Notice: `licenses/SPACE_GROTESK_OFL.txt`
- Generated asset: `fonts/aura_font_space_medium_26.c`

## Owner-provided Aura identity

The approved Aura wordmark was supplied by the project owner in `Aura_Selas_Logo.zip`. It is project identity artwork, not a third-party icon or font. The vector is authoritative; the large transparent PNG is retained only as a reference and is never compiled or decoded on the device.

- Sources: `branding/source/Aura_Selas.svg`, `branding/source/Aura_Selas_Transparent.png`.
- Source SHA-256: SVG `54052b36775a5010b91d6aada545da15e7a95a651e24e403e99b9cd980efc1da`; PNG `4b7d26de269503eba24203b7859efbda526058d5c83c044ecbe3ca0e98f4296b`.
- Generated assets: `aura_brand_aura_compact` (144×53) and `aura_brand_aura_hero` (176×65), static ARGB8888, 76,288 bytes total.
- Generator and manifest: `tools/icons/generate-brand-assets.mjs`, `branding/manifest.json`.
- Placement and regeneration: `docs/BRANDING.md`.

## Appearance fonts (Μάρμαρο, Σέλας, Γραφίτης)

The three appearances added on 2026-10-29 draw with these families, all under the SIL Open Font License 1.1. Variable sources are pinned to google/fonts commit `b68b528a75da7d9b73f3928899c07a23083a6633`, made static by `tools/fonts/instance-fonts.py` (fontTools 4.65.0) and converted with `lv_font_conv` 1.5.3. A character a face lacks (arrows, a few dashes and combining marks) is drawn by the IBM Plex font of the same role.

- Noto Serif Display: Marble titles and numerals. Notice: `licenses/NOTO_SERIF_DISPLAY_OFL.txt`.
- Commissioner: Marble text. Notice: `licenses/COMMISSIONER_OFL.txt`.
- Manrope: Aurora text and numerals. Notice: `licenses/MANROPE_OFL.txt`.
- Roboto Flex: Graphite text and condensed numerals. Notice: `licenses/ROBOTO_FLEX_OFL.txt`.
- Generated assets: `fonts/aura_font_{commissioner,notoserif,manrope,robotoflex}_*.c`

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

The v4 orb/wave/vortex/ribbon and fictional map images (`design/generated/`) were created specifically for this project with OpenAI image generation on 2026-10-18. Nothing used them after the AI states moved to the procedural presences, so they were removed on 2026-10-30; they remain in repository history.

## Appearance icon families

Each appearance draws its own icon family, converted like Lucide to 64×64 A8 LVGL images by `tools/icons/generate-appearance-icons.mjs` from pinned Iconify packages. Each manifest lists the role-to-glyph map and any Lucide stand-in.

- Phosphor Light (Marble), `@iconify-json/ph` 1.2.2, MIT, by Phosphor Icons. Notice: `licenses/PHOSPHOR_MIT.txt`. Assets: `icons/aura_icon_assets_phosphor.cpp`, `icons/manifest-phosphor.json`.
- Solar Bold Duotone (Aurora), `@iconify-json/solar` 1.2.13, Creative Commons Attribution 4.0, by 480 Design. Attribution: "Solar icon set by 480 Design". Notice: `licenses/SOLAR_CC_BY_4.0.txt`. Assets: `icons/aura_icon_assets_solar.cpp`, `icons/manifest-solar.json`.
- Material Symbols Rounded (Graphite), `@iconify-json/material-symbols` 1.2.97, Apache 2.0, by Google. Notice: `licenses/MATERIAL_SYMBOLS_APACHE_2.0.txt`. Assets: `icons/aura_icon_assets_material.cpp`, `icons/manifest-material.json`.

## Aoraton and Google sign-in branding

The owner requested the current Aoraton logo and Google sign-in mark on AURA's account connection screens. These are brand artwork, kept separate from the theme-dependent semantic icon families and never recolored.

- Aoraton: `branding/aoraton-logo.png`, from <https://aoraton.com/favicon-96x96.png>. Its SHA-256 matches the owner's read-only `incognitochat/public/favicon-96x96.png`. No application implementation was copied from that repository.
- Google: `branding/google-g.png`, the official full-color gradient G from <https://developers.google.com/static/identity/images/g-logo.png>. It identifies the existing Google sign-in action. Google branding guidance: <https://developers.google.com/identity/branding-guidelines>. The mark's colors and aspect ratio are preserved on a white button; no ownership or general-purpose license to Google's trademarks is claimed.
- `tools/icons/generate-brand-assets.mjs` converts the originals into two static ARGB8888 LVGL assets shared by the simulator and firmware (40,960 bytes total). Regenerate with `node tools/icons/generate-brand-assets.mjs` after installing the existing pinned icon-build dependencies. Source hashes are in `branding/manifest.json`.

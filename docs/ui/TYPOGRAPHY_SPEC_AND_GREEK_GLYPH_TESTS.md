# AURA v10 typography specification and Greek glyph tests

## Implemented families

- UI: IBM Plex Sans Regular, Medium and SemiBold, pinned to IBM Plex commit `71f558992315a12ca1f06a2215d57962a1992162`.
- Brand: Space Grotesk Medium, pinned to commit `03507d024a01282884232081fc6011c09ff4e849`.
- Proof comparator only: Inter Variable, pinned to commit `353b61b9f4430d5f420d56605a6e7993e0941470`. Inter is linked by the native simulator proof page and excluded from the firmware component.

All three are licensed under SIL OFL 1.1. Pinned source SHA-256 values:

| File | SHA-256 |
|---|---|
| `IBMPlexSans-Regular.ttf` | `975DCDA37D80F038DCD143C22E33CA2D97A0CC5A929AACE1C749153B0FE1AFA5` |
| `IBMPlexSans-Medium.ttf` | `331C8639D7598B2CDE62A911A71DB195E30CB655CD6BDF2E324A7E984955F907` |
| `IBMPlexSans-SemiBold.ttf` | `A20CAF8286023A6A7A85E40B1D2A4AE9FC3E3B1F9EDA8F4C542DD4986AF67BB1` |
| `SpaceGrotesk-Medium.ttf` | `3183B7EB0B4241476360E2CD8E527868616CC16D3CE332C4376505989B772B44` |
| `InterVariable.ttf` | `4989B125924991B90D05B2D16E0E388C48F7D5BB8B30539BBF9C755278D0CCAF` |

## LVGL generation

`tools/fonts/build-fonts.ps1` downloads only the pinned inputs, runs `lv_font_conv@1.5.3` at 4 bpp without compression, adds the LVGL 9.3+ immutable-bitmap metadata, and then runs `verify-font-sources.py`.

The UI range is:

`U+0020–00FF, U+0300–036F, U+0370–03FF, U+2010–2026, U+20AC, U+2122, U+2190–2193, U+2212`.

It covers Basic Latin, Latin-1, combining marks, monotonic Greek and Greek symbols, punctuation, euro/trademark, arrows and minus. Polytonic Greek Extended `U+1F00–1FFF` is intentionally not compiled because it is not used by current localized copy; add the range and re-run the proof before introducing polytonic copy.

Generated production fonts:

- IBM Plex Regular 14/18
- IBM Plex Medium 14/17/40
- IBM Plex Medium 64 numeric weather subset
- IBM Plex SemiBold 20/24
- Space Grotesk Medium 26

The 64 px weather face intentionally contains only space, hyphen/minus, digits, `C` and degree. Its generated C source is 58,393 bytes and its native object is 10,295 bytes. The native object-code estimate is 280,631 bytes for the eight IBM UI objects plus 800 bytes for the small AURA Space Grotesk subset. The native-only Inter comparator is 26,496 bytes. These are Windows object measurements, not an ESP-IDF linked-image measurement.

## Automated coverage

Source inspection reports 928 glyphs in each IBM face, 731 in Space Grotesk and 2,852 in Inter, with no missing required samples.

`aura_ui_assets_tests` queries every general-purpose production font through LVGL and requires Latin, degree, precomposed and combining accents, all modern Greek tonos/dialytika forms, final sigma, em dash, bullet, ellipsis, euro and trademark. It separately validates every permitted glyph in the 64 px weather subset and the Space Grotesk `A U R A` glyph set.

The three native 480×480 proof pages exercise:

1. Space Grotesk AURA and IBM Plex/Inter A/B at assistant-state sizes.
2. Greek error, connectivity and Aoraton copy.
3. Accented sentence, degree/percent/units, uppercase/lowercase accented Greek and combining marks.

Evidence:

- `artifacts/v10/AURA_V10_TYPOGRAPHY_PROOF.png`
- `artifacts/v10/typography/typography-proof-1.bmp`
- `artifacts/v10/typography/typography-proof-2.bmp`
- `artifacts/v10/typography/typography-proof-3.bmp`

Visual inspection found no tofu, missing accents, clipped baselines or encoding corruption. Answer and chat body copy use the 18 px face and remain scrollable.

## Rebuild command

```powershell
.\tools\fonts\build-fonts.ps1
.\scripts\build-simulator.ps1 -Configuration Release
.\build\native\aura_ui_assets_tests.exe
```

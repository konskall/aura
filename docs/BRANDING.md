# AURA visual identity assets

The approved Aura wordmark is owner-provided artwork. Its shapes, proportions,
transparency and cyan-blue-violet gradients are not theme-dependent and must not
be recoloured, outlined, cropped, stretched or given another glow or symbol.

## Sources and generated assets

- `shared/assets/branding/source/Aura_Selas.svg` is the authoritative vector source.
- `shared/assets/branding/source/Aura_Selas_Transparent.png` is the supplied
  3408×1260 transparent reference. It is not compiled or decoded on the device.
- `aura_brand_aura_compact` is 144×53 ARGB8888 for the temporary Home wordmark.
- `aura_brand_aura_hero` is 176×65 ARGB8888 for Boot and Power-off.

Both generated images use the project's existing static LVGL full-colour asset
format. They are built into the same `aura_ui`/`aura_shared` source on Windows and
ESP-IDF. `AuraUi::Impl::aura_wordmark` is the single UI-thread helper used by all
appearances; it disables recolouring and input/scroll handling.

Regenerate from the repository root with:

```powershell
node .\tools\icons\generate-brand-assets.mjs
```

The pinned `@resvg/resvg-js` and `pngjs` packages are installed by
`tools/icons/build-icons.ps1`. The generator records source hashes, dimensions
and payload sizes in `shared/assets/branding/manifest.json`. Repeated generation
must produce byte-identical C++, header and manifest files.

## Placement

The image replaces typography only where “AURA” was a visual wordmark:

- Boot: centred 176×65 at `(152,26)`.
- Power-off: centred 176×65 at `(152,48)`, retaining the screen's low-energy opacity.
- Home while the clock is unavailable: 144×53, centred in Halo/Marble, aligned
  with the left clock column in Aurora/Graphite.

The name inside sentences, localized copy, identifiers, protocol fields and
configuration keys remains text.

## Cost

The two Aura payloads are 76,288 bytes in flash: 30,528 bytes for 144×53 and
45,760 bytes for 176×65. Together with the existing Aoraton and Google brand
assets, the generated branding payload is 117,248 bytes. The original 423,670-byte
PNG and the SVG are source files only. Static ARGB8888 pixels are read from const
asset storage; there is no permanent decoded logo buffer in RAM. ESP-IDF linker
placement and transient draw cost remain to be measured in an activated 5.5.5
firmware build.

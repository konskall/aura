# Local photo decoders

`stb/stb_image.h` is unmodified stb_image 2.30 from the upstream header's last
commit `013ac3beddff3dbffafd5177e7972067cd2b5083`:
https://github.com/nothings/stb/blob/013ac3beddff3dbffafd5177e7972067cd2b5083/stb_image.h

SHA-256: `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3`.
The original MIT/public-domain dual license is retained at the end of the header.
AURA uses its MIT option. Only PNG/JPEG, memory input, integer output and the
bounded AURA allocator are enabled. No network or LVGL allocator is involved.

`tjpgd/` comes from the existing pinned LVGL 9.6.0 distribution
(`src/libs/tjpgd`, ChaN TJpgDec R0.03). Its original license is retained in
every source and `LICENSE.txt`. The marked adaptations remove LVGL header/config
dependencies and use standard integer types. Its output is LVGL-specific BGR888;
the adapter explicitly converts to RGB before RGB565 packing. LVGL removed the
codec's internal scale implementation, so `JD_USE_SCALE` is disabled and AURA
samples original MCU coordinates into the bounded 480x480 destination instead.
Baseline JPEGs decode by MCU into a bounded RGB565 image without allocating a
full original raster. Small progressive JPEGs use stb_image's fallback.

Both platforms compile the same decoders. No firmware SDK or managed component
is edited. Tests use only explicitly synthetic DEMO fixtures.

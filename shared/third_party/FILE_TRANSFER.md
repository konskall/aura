# QR encoder for local file transfer

`qrcodegen/` contains Project Nayuki's MIT-licensed C QR encoder from the
already pinned LVGL 9.6.0 distribution (`src/libs/qrcode`). The original copyright
and license are retained in the source, header and `LICENSE.txt`.

Marked AURA adaptations use standard C headers/assertions, remove the LVGL
configuration dependency and prefix exported symbols with `aura_qrcodegen_` so
the encoder can coexist with the BSP's own QR widget. Only a bounded version-6
working buffer and a 216x216 RGB565 UI image (a plain `lv_image`, not an `lv_canvas`) are used. The encoded data is the
public numeric local URL, never the PIN or bearer token. There is no image fetch,
cloud QR service or generated bitmap asset.

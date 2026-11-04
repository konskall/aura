# Synthetic photo fixtures

The `DEMO` fixtures contain no personal photos. Added color/upload regressions:

- `DEMO-color-bars.png` and `DEMO-color-bars.jpg`: 192x64 with three equal
  vertical RGB bands, `(240,20,10)`, `(10,230,30)`, `(15,35,245)`. Generated
  with Pillow; JPEG quality 98, no chroma subsampling. Tests sample the band
  centers to distinguish BGR/RGB inversion from JPEG edge ringing.
- `DEMO-large-bars.jpg`: the same synthetic chart enlarged to 2048x1024,
  saved at quality 98 without subsampling. Verifies spatial content and color
  channels when decoded to the bounded 480x240 image.
- `DEMO-tall.png`: 1024x1536 RGB blue with a red rectangle. Its compressed
  bytes are small, but its resolution exceeds the native PNG expansion cap.
  The browser regression fits it to 320x480 and checks the source is unchanged.

Fixtures are intentional test inputs, not generated build output. Optional
Chromium QA uses its own temporary storage under the ignored `artifacts` folder.

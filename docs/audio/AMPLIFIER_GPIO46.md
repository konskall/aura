# Amplifier control on GPIO46

GPIO46 is an ESP32-S3 strapping pin, so AURA never configures it as an independent generic output.

The audited BSP 2.0.1 passes `BSP_POWER_AMP_IO` (GPIO46) to the ES8311 `esp_codec_dev` configuration with `pa_reverted=false`. `BspAmplifierControl` therefore wraps codec mute/unmute only. The BSP remains responsible for pin mode, active polarity and enable sequencing.

Playback sequence:

1. Initialize BSP codec and leave output muted.
2. Open the codec stream with the requested PCM format.
3. Enable/unmute through `esp_codec_dev`.
4. Write bounded PCM chunks.
5. Mute before closing the stream.

Boot-level behavior, audible pop/click performance and idle-current savings need measurement on the physical board. Direct GPIO fallback is intentionally absent.

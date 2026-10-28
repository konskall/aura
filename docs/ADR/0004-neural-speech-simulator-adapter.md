# ADR 0004: Optional neural speech adapter for the simulator

Status: accepted, 2026-10-25

## Decision

The Windows simulator may use an optional, separately installed `edge-tts==7.3.1` process as a development TTS adapter. It is selected with `AURA_TTS_PROVIDER=edge-neural`. Amended 2026-10-28: the launcher never selects it automatically (it uses an unofficial endpoint); `-TtsProvider auto` chooses official Azure Speech (ADR 0005) when configured and otherwise installed Windows voices, and `-TtsProvider neural` warns that the service is unofficial.

The shared product model exposes two stable voice profiles, `athina` and `nestoras`. The bridge alone maps each profile and interface language to a fixed allowlisted provider voice. User text is UTF-8/Base64 encoded, limited to 2,000 bytes and passed through an argument array with `shell:false`. The helper writes only MP3 bytes to stdout. The bridge caps output at 8 MiB, validates the MP3 signature, and the simulator decodes it with the same bounded CC0 `minimp3` implementation used by Internet radio before sending PCM to the normal audio backend.

The neural client and consumer endpoint are not compiled into or called directly by ESP32 firmware. Production device TTS remains an authenticated gateway capability with a revocable device credential. The voice profile remains shared state so the same setting can be honoured by that gateway later.

## Rationale

This provides a real Greek neural voice in the native LVGL simulator without forking the UI or replacing the existing audio path. The separate process keeps Python and provider-specific behavior out of the portable C++ core and firmware. A fixed mapping prevents arbitrary provider identifiers from reaching process arguments.

## Consequences

- `scripts/setup-neural-voice.ps1` installs the pinned client under ignored `.tools/edge-tts` and performs a live Nestoras probe.
- Neural synthesis requires Internet access and the Edge Read Aloud consumer endpoint has no service-level guarantee. A failed request is reported as unavailable/failed; AURA does not claim that audio played when it did not.
- `el-GR-NestorasNeural` was verified live. `el-GR-AthinaNeural` remains selectable, but its upstream endpoint was intermittently returning no audio at the time of this decision and must not be reported as verified until a live probe succeeds.
- The `edge-tts` package is GPL-3.0 and stays an optional external installation; it is not vendored, statically linked or distributed in AURA artifacts. Redistribution/product use needs a separate dependency and provider-terms review.
- Windows OneCore/SAPI synthesis remains available with `-TtsProvider windows` and needs no network.


# ADR 0003: Streaming AI gateway and device secrets

Status: accepted, 2026-10-25

## Decision

AURA keeps ESP-IDF 5.5.5, LVGL 9.6 and the existing shared UI. AI providers publish cumulative, bounded text through `AssistantStreamMailbox`; only the UI thread consumes it and mutates `AssistantController`/LVGL. The first completed Greek or English sentence may be queued for speech while the final reply is still arriving. The final reply remains authoritative for conversation memory and typed action parsing.

The Windows development bridge supports two backends:

- the existing authenticated Codex CLI; and
- an opt-in Groq backend selected with `AURA_BRIDGE_AI_PROVIDER=groq`.

The Groq key is read only from the bridge process environment. It is never compiled into the ESP32 image, committed to Git, placed in an LVGL snapshot or logged. The firmware contract is an authenticated HTTPS gateway that keeps upstream provider credentials server-side. Device provisioning may install a revocable, device-scoped gateway token in NVS; it must not install the upstream Groq credential.

Speech recognition can opt into Groq's official `audio/transcriptions` endpoint through the local bridge. The recorder sends one validated, bounded 16 kHz mono WAV per utterance. We do not split short speech into artificial micro-requests because the provider accepts a file request and independently billing/rate-limits requests.

Greek speech output originally continued to use supported Windows voices in the simulator. [ADR 0004](0004-neural-speech-simulator-adapter.md) later adds an optional, out-of-process neural development adapter without changing the production gateway decision or embedding it in firmware.

## Rationale

An API key in source control is immediately reusable by anyone with the repository. A key in firmware can also be extracted from an image or physical flash and cannot identify or revoke one device safely. Free-tier status does not change quota exhaustion, account abuse, rotation or provider-policy risk.

The latest-response mailbox coalesces intermediate tokens, so a slow 480×480 renderer cannot create an unbounded queue. Provider workers never retain LVGL pointers. The same NDJSON decoder is portable shared code for Windows and the future ESP-IDF gateway adapter.

## Consequences

- Existing Codex CLI behavior remains available and is the default live backend.
- Groq development requires `GROQ_API_KEY` in the launching process environment; tests use fake keys and fake HTTP responses only.
- The leaked key supplied in an external prompt is not used and must be revoked/rotated.
- Direct ESP32-to-Groq calls are deliberately rejected. Device AI remains unavailable until an HTTPS gateway URL and a revocable device credential are provisioned and verified on hardware.

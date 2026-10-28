# ADR 0005: Official Azure Speech provider for the simulator

Status: accepted, 2026-10-25

## Decision

The Windows simulator bridge supports official Azure Speech text to speech with `AURA_TTS_PROVIDER=azure`. The provider reads `AZURE_SPEECH_KEY` and either `AZURE_SPEECH_REGION` or `AZURE_SPEECH_ENDPOINT` only from its process environment. It sends bounded SSML to the documented Speech REST endpoint and requests 24 kHz mono MP3. The existing shared Athina/Nestoras profiles map to fixed, allowlisted provider voices, and the existing bounded `minimp3` path decodes the response.

Only HTTPS hosts ending in `.tts.speech.microsoft.com` or `.cognitiveservices.azure.com` are accepted. A Foundry project URL (`*.services.ai.azure.com/api/projects/...`) and an Azure OpenAI URL are different services and are rejected as Speech synthesis endpoints. Response size and MP3 framing are validated before audio reaches the simulator.

The direct Azure request is desktop development functionality. No Azure credential is stored in source, configuration, command-line arguments, runtime logs, LVGL state, firmware or device NVS. Production ESP32 speech still uses an authenticated gateway with a revocable device-scoped credential.

## Rationale

Azure Speech is the supported production-grade route for the Greek `el-GR-AthinaNeural` and `el-GR-NestorasNeural` voices. It provides documented authentication, status codes and service endpoints, unlike the optional consumer Edge adapter. Keeping the provider in the loopback bridge preserves simulator/firmware UI parity and the existing audio pipeline while maintaining the device-secret boundary.

## Consequences

- `-TtsProvider auto` prefers Azure only when a key and a Speech region/endpoint are already present in the launching process, then falls back to the optional Edge adapter and finally Windows voices.
- `scripts/start-azure-speech-simulator.ps1` accepts a Speech region or approved Speech endpoint and supports hidden or ephemeral clipboard input. For repeat owner-requested development runs, `dev-simulator.ps1` also imports a strict allowlist from the Git-ignored local `.env`; this plaintext convenience file remains simulator-only and must never be committed or copied to a device.
- Authentication, quota, timeout, upstream and malformed/oversized audio failures are explicit; AURA never reports synthesized audio on failure.
- Automated tests use an injected HTTP implementation and a conspicuous test-only key. A live Azure request requires a newly issued credential and is recorded separately.

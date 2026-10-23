# AURA v2 system overview

AURA keeps application state and LVGL rendering portable while platform adapters own I/O.

```text
Codex host bridge ──> assistant provider ──> AssistantController ─┐
SDL / ES8311 ───────> IAudioService ───────> AudioDiagnostics ───┼─> AuraUi (UI thread only)
Aoraton adapter ────> IAoratonService ─────> AoratonSnapshot ────┘
```

The Windows target uses SDL2 display/input/audio and the loopback Codex bridge. The firmware target uses the maintained Waveshare BSP, `esp_codec_dev`, FreeRTOS tasks and the same core/UI. Network, crypto, storage and audio code never run inside LVGL callbacks.

Current trust boundaries:

- AI output is untrusted display text and cannot execute Aoraton actions.
- Aoraton mock messages are local fictional fixtures. There is no production transport in AURA.
- Sending always passes through `prepare_send` and an explicit confirmation state.
- Decrypted Aoraton content and room secrets are not sent to Codex.
- The Codex bridge remains authenticated loopback-only and host-only.

See [PLATFORM_ABSTRACTION.md](PLATFORM_ABSTRACTION.md) and [MEMORY_BUDGET.md](MEMORY_BUDGET.md).

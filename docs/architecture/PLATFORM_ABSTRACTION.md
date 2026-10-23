# Platform abstraction

| Contract | Windows simulator | Firmware | Automated tests |
|---|---|---|---|
| `IAssistantProvider` | Mock or loopback Codex CLI | No host CLI | deterministic mock |
| `IAudioService` | `WindowsAudioBackend` using SDL callback audio | `Esp32AudioBackend` using BSP ES8311/`esp_codec_dev` | `MockAudioService` |
| `IAoratonService` | `MockAoratonService` | same mock until compatible transport exists | deterministic mock |
| LVGL display/input | SDL2 | Waveshare BSP | renderer smoke captures |

Shared code owns legal state transitions, bounds and snapshots. Adapters own threads, devices, files and networking. The Windows audio callback only consumes a bounded PCM buffer; it never calls LVGL. The ESP32 audio task writes fixed chunks and publishes diagnostics that the UI reads while holding the BSP display lock.

The Aoraton real adapter remains a separate future module. It must implement the audited protocol without importing browser APIs or silently changing encryption.

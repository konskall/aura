# Audio architecture

`IAudioService` exposes initialization, PCM/WAV playback, pause/resume/stop, volume, mute, progress, envelope and diagnostics. `AudioStateMachine` enforces:

```text
UNINITIALIZED → READY → BUFFERING → PLAYING ↔ PAUSED
                                  → STOPPING → READY
                                  → ERROR → READY
```

The simulator backend opens an SDL2 signed-16 audio device and feeds it from a callback. Volume and mute are applied to the samples actually delivered. A smoothed peak envelope drives the LVGL meter and orb glow. Test tone and in-memory WAV paths both use real Windows audio output; missing devices fail without blocking the UI.

The firmware backend uses a dedicated FreeRTOS task and fixed 512-sample writes. It accepts mono/stereo signed-16 PCM at 16 or 24 kHz, parses bounded PCM WAV files, tracks cancellation generations and delegates codec/I²S ownership to the Waveshare BSP.

The current WAV ceiling on firmware is 512 KiB. Long network audio and TTS streaming need a producer/ring-buffer API in the next audio increment. Greek TTS remains optional and is disabled until a Windows voice provider can be detected and integrated without bypassing the PCM/envelope path.

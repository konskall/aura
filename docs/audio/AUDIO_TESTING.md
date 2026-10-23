# Audio testing

Automated host tests cover legal transitions, rapid duplicate commands, volume bounds, mute, progress, pause/resume, completion, invalid WAV input and shutdown through `MockAudioService`.

Interactive simulator checks:

1. Open **Ήχος** from Home.
2. Press **Δοκιμή ηχείου** and verify the low-volume tone.
3. Move volume while playing; mute/unmute; pause/resume; stop.
4. Press **Αρχείο WAV** to exercise SDL WAV parsing and playback.
5. Confirm the cyan envelope follows delivered PCM and stops immediately.

Commands:

```powershell
.\scripts\test.ps1
.\scripts\capture-screenshots.ps1
```

Automated CI does not require an audio device. Actual audible output is an interactive host check. ES8311 checks are listed in `docs/development/HARDWARE_VALIDATION.md`.

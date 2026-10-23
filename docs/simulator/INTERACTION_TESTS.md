# Interactive scenarios

1. **Audio:** open Audio, test tone, move volume, mute, pause/resume, play WAV, stop.
2. **Codex:** launch with `-Provider codex`, send Greek text, verify the response remains display-only.
3. **Incoming:** inject a fictional Aoraton message, verify badge, open Aoraton and select the room to clear unread.
4. **Compose:** choose a room, type a message, preview exact destination/text, cancel once, then confirm.
5. **Reconnect:** disconnect Aoraton from the panel, keep navigating, reconnect and verify the connected chip returns.
6. **Security failure:** inject invalid ciphertext; verify no message appears and the diagnostic is visible.
7. **Concurrent:** play audio, inject a message, open Aoraton, return Home; playback/UI remain responsive.

Captures are real SDL renderer output under `simulator/screenshots/` and are intentionally Git-ignored.

# Desktop simulator

## Setup and run

Windows 10/11, PowerShell 5.1+, Git and network access are needed for first setup. The setup downloads pinned portable w64devkit, CMake and Ninja archives under ignored `.tools`; CMake fetches pinned LVGL 9.6.0 and SDL2 2.32.10 sources. It does not modify global `PATH` or the registry.

```powershell
.\scripts\setup-windows.ps1
.\scripts\dev-simulator.ps1
```

For everyday testing, `run.bat` in the repository root runs `build-simulator.ps1` (incremental) and then `dev-simulator.ps1 -Provider codex`; `run.bat mock` selects Demo Mode. It refuses to start a second copy while a simulator window is open, and it pauses on failure so a double-clicked window does not vanish.

Demo Mode is the default of `dev-simulator.ps1`. To use the locally authenticated Codex CLI for Assistant turns:

```powershell
codex login status
.\scripts\dev-simulator.ps1 -Provider codex
```

The same native UI can use the opt-in Groq development backend. The local bridge streams chat deltas and sends the validated utterance WAV to the official Whisper endpoint. Speech synthesis is independently selectable:

```powershell
$env:GROQ_API_KEY = '<a newly rotated key>'
.\scripts\dev-simulator.ps1 -Provider groq
Remove-Item Env:GROQ_API_KEY
```

To keep Codex CLI for answers and use Groq only for speech recognition:

```powershell
$env:GROQ_API_KEY = '<a newly rotated key>'
.\scripts\dev-simulator.ps1 -Provider codex -SpeechProvider groq
```

The secret is process-only and is not shown in bridge health or logs. Never reuse a key pasted into a prompt: it is already exposed. This loopback bridge is not the production ESP32 gateway.

The second command starts hidden loopback helpers, opens the control panel and runs the visible simulator in the foreground. Closing the simulator tears down helpers owned by the script. When `codex` is not on `PATH`, the script uses the newest `codex.exe` shipped with the Codex desktop app (`%LOCALAPPDATA%\OpenAI\Codex\bin\…`) for that run only; `AURA_CODEX_PATH` overrides it.

## Your aoraton.com account

Run `.\scripts\setup-aoraton.ps1` once. It reads the public Supabase URL and anon key from `..\incognitochat\services\supabaseConfig.ts` (read-only) into the git-ignored `config\aoraton.local.json`. After that, `run.bat` and `dev-simulator.ps1` start Aoraton in live mode.

1. Open Aoraton and choose **Σύνδεση με Google**.
2. Finish signing in in the browser that opens.
3. Open **Κλειδιά δωματίων** and type the aoraton key-vault passphrase. Alternatively, copy an invite link on aoraton.com and choose **Πρόσκληση**.

If the browser stays on aoraton.com, add `http://127.0.0.1:4779/**` to Supabase → Authentication → URL Configuration → Redirect URLs.

Rooms refresh every 10 s (polling, not realtime). Sending always goes through the confirmation screen. Without the config file, Aoraton shows DEMO rooms.

**inco (opt-in).** On aoraton the device that sent a message mentioning inco answers it. To let AURA answer its own messages, add `"inco_enabled": true` to `config\aoraton.local.json`. This sends the question and the room's recent messages, decrypted, to Gemini through aoraton's `inco-ai` function.

Then a message like «@inco τι ώρα είναι;» sent from AURA, in a room where inco is enabled, gets inco's reply in a few seconds. The reply can also be read on aoraton.com. The terminal shows `AURA inco: answered <id>`, or a code such as `RATE_LIMITED (429)`.

## Voice and companion features (v13)

For repeat Groq + Azure tests, put newly rotated credentials in the repository's ignored `.env`:

```text
GROQ_API_KEY=<new Groq key>
AZURE_SPEECH_KEY=<new Azure key>
AZURE_SPEECH_REGION=swedencentral
AURA_GROQ_MODEL=openai/gpt-oss-120b
```

Run `.\scripts\dev-simulator.ps1 -Provider groq`. The launcher imports only its fixed allowlist of names, never evaluates the file as PowerShell, validates key shape, and lets an explicit process environment value override the file. This is plaintext local development storage; it is Git-ignored and must never be committed, copied to firmware or shared.

Speech stays on this PC. Install the local recogniser once (about 800 MB under the ignored `.tools\whisper`, SHA-256 pinned). It uses whisper `large-v3-turbo`, which gets Greek right (about 8–9 s for a short question on an i7-4790K), and falls back to `small` (faster but error-prone) when turbo is missing:

```powershell
.\scripts\setup-voice.ps1
```

Install the optional neural voices once, then start normally:

```powershell
.\scripts\setup-neural-voice.ps1
.\scripts\dev-simulator.ps1 -Provider codex -TtsProvider neural
```

For the supported Azure Speech path, first rotate any key that has appeared in chat, then copy the resource **Region** or the Speech endpoint from its **Keys and Endpoint** page. A Foundry project endpoint and an Azure OpenAI endpoint are not Speech synthesis endpoints. Start with one of:

```powershell
.\scripts\start-azure-speech-simulator.ps1 -Region westeurope -Provider codex
.\scripts\start-azure-speech-simulator.ps1 -Endpoint https://YOUR-RESOURCE.cognitiveservices.azure.com -Provider codex
```

The script asks for the replacement key with hidden input, validates that a one-character console paste is not accepted, passes the key only through the child-process environment and clears it when the simulator closes. If the Windows console cannot paste into hidden input, first copy the key and add `-KeyFromClipboard`; the launcher reads the complete value without displaying it and immediately clears the clipboard. `-TtsProvider auto` is the default: it prefers configured Azure Speech, otherwise selects the optional local neural adapter when installed, and finally uses Windows voices. `-TtsProvider windows`, `neural`, or `azure` forces one path. In AURA, open **Ρυθμίσεις → Ήχος → Φωνή βοηθού** to choose **Αθηνά** or **Νέστορας** and preview the localized phrase. The simulator control page exposes the same two real preview requests under Audio Engine.

With the bridge running (`dev-simulator.ps1`), tapping the orb records from the PC microphone, stops after a pause, recognises Greek with whisper.cpp or Groq, asks the provider with device facts and recent conversation, and speaks the answer through the selected TTS path. Explicit Greek or English requests such as `ψάξε για πίτσα στη Σπάρτη` opt the single Codex turn into live web search; ordinary turns remain non-browsing and Groq chat fails closed because it has no search tool. Without the recogniser, Listening falls back to the labelled synthetic input. Voice notes, translation and "read aloud" use the same path. A voice note is dictated: it listens for up to 30 s and stops after a 3.5 s pause (8 s if nothing is said), and its transcription runs beside a spoken question instead of being dropped by it.

Neural audio is returned as bounded MP3 and decoded to PCM by the shared `minimp3` path. It requires Internet access. Nestoras is the verified default. Athina can fail on the optional consumer Edge endpoint; official Azure Speech is the supported path for both Azure voice identifiers. No neural client or provider credential is placed in firmware.

The apps grid now holds Ρολόι, Σημειώσεις, Μετάφραση and Σήμερα. Alarms, reminders and notes are kept in the ignored `simulator\runtime\companion` folder. The AURA LAB "Companion" section drives what the PC cannot do: a 10 s timer, an alarm or reminder a minute ahead, the morning briefing, IMU raise / face down / face up, screen sleep, DEMO phone notifications and calls, DEMO pairing, a DEMO update, typed translation input and typed notes. After 30 s without input the screen dims and then shows the always-on clock; the first click only wakes it.

## Product-surface interaction

- Mouse: touch/click and drag scrolling. On Home, swipe up or left opens the apps and swipe down opens notifications; elsewhere swipe right goes back on list/detail screens (not on screens with sliders, horizontal strips or a keyboard). Long-press the back button to return Home.
- Home: tap the orb to talk; the weather glance and the apps button sit below it.
- Physical keyboard: type into the focused text field (compose, city search, Wi-Fi password).
- On-screen keyboard (full width, 28 px legends): Greek and Latin letters, digits and two symbol pages, one-shot shift (double tap: caps lock), a Greek tonos/diaeresis key (΄) and enlarged key pop-overs. OK submits the form. The Wi-Fi password layout is Latin/ASCII only.
- Wi-Fi: tap the Wi-Fi glyph in the status bar or `Ρυθμίσεις → Wi‑Fi`. The list is a real, read-only scan of this PC's adapter, and the simulator's own link is shown as "through this computer". Joining another network in the simulator is a labelled `ΠΡΟΣΟΜΟΙΩΣΗ`: the password is validated by the shared rules, but it is neither stored nor checked, and the PC's connection is never changed. The control panel's `Next Wi‑Fi connect fails` button exercises the failure path.
- Weather: open `Καιρός` from Home or the apps, pick a preset city or tap the search field to type with the Greek keyboard, confirm a result, refresh, or change the saved default from Settings.
- Listening records the PC microphone when the bridge and the local recogniser are available (see above). Otherwise it shows a labelled synthetic input signal and `Ολοκλήρωση` turns it into the fixed sample prompt (`Πες μου κάτι σύντομο.`), so the turn still reaches a real provider answer.
- `Esc`: cancel the active assistant state.
- `F12`: save the actual SDL renderer to `simulator/screenshots/aura-latest.bmp`.
- `Ρυθμίσεις → Ήχος`: volume, mute, the new-message sound, spoken replies and a test sound. The choices are kept in the ignored `simulator/runtime/companion/sound.txt`. Alarms, timers and reminders ring even when muted or at volume 0, at least at half volume. The speaker test tone and the bundled WAV are in `Σύστημα → Διαγνωστικά`.
- `Ρυθμίσεις → Σύστημα`: software update, diagnostics, and restart, power off and factory reset, each confirmed first. In the simulator a restart happens in place: the window stays open, and the conversation, playback and recording are dropped behind a short start-up screen. Power off leaves a black screen; press and hold it to start again. Factory reset erases alarms, reminders, notes and their recordings, sound and display settings, the weather city and cache, the saved Wi‑Fi, the phone link and the Aoraton sign-in with its keys, then restarts. It never changes the PC's own Wi‑Fi.
- Back returns a list to where it was scrolled.
- Conversation shows the last four in-memory turns for five minutes. `Ιστορικό` can ask a follow-up, start a new topic or clear the bounded memory; it is not persisted to disk. Cancelling an active conversation returns to the apps launcher.
- Radio favourites can be reordered one position at a time from `Ραδιόφωνο → Αλλαγή σειράς`; the stored order is restored on the next run. A search that finds no Greek station is asked again worldwide, and "βάλε ραδιόφωνο" without a station plays the last one (or the first favourite).
- A radio stream that drops is joined again automatically (pauses of 1, 2, 4 and 8 s, shown as `Σύνδεση…`) before an error is shown; switching stations cuts a blocked read short; Shoutcast v1 servers that answer `ICY 200 OK` play through a small HTTP/1.0 fallback.
- Fuel prices refresh on their own: `dev-simulator.ps1` renews the ignored EU bulletin cache in the background when it is missing or more than a week old, and the simulator asks the bridge again every 6 hours (every 15 minutes while prices are missing or stale).
- The apps grid can be rearranged: tap the pencil in `Εφαρμογές`, tap an app, then tap the place it should move to (the others shift). Tap ✓, Back or swipe away to finish. The order is saved at once in the ignored `simulator/runtime/companion/app-layout.txt` (device: `app-layout.txt` on the microSD card) and factory reset restores the default order.
- `Ρυθμίσεις → Κατάσταση συσκευής` reports the trusted local assistant, recognition and voice provider names after their health checks. It never displays credentials.
- Home `Aoraton`: open fictional local `DEMO` rooms, conversation, compose and confirmation screens.
- Home `Εργαλεία`: open the compact launcher for Assistant, Aoraton, Audio and Notifications; Settings remains available from the gear control.
- The `DEV` marker is off in normal runs; only the status-matrix captures show it.
- A normal run starts with no notifications; captures and tests start with one labelled `DEMO` notification.

The browser control panel injects assistant, connectivity, audio and Aoraton scenarios through an ignored UTF-8 event log. It is deterministic development infrastructure, not production IPC.

The Weather controls can open either shared Weather route, request an Open-Meteo city search, or refresh the saved city. They do not expose a GPS/current-location action. A one-shot live UTF-8 provider check is also available:

```powershell
.\build\native\aura_simulator.exe --weather-live-city Αθήνα --screenshot artifacts\weather-live.bmp
```

To use an already running local `tlesdetector` without changing it, set its base URL. AURA calls
its existing `/api/weather` and `/api/opportunities` routes; no provider key is copied into AURA:

```powershell
$env:AURA_TLESDETECTOR_URL='http://127.0.0.1:3000'
.\build\native\aura_simulator.exe --weather-live-city Αθήνα --screenshot artifacts\weather-live-tlesdetector.bmp
```

Remote and ESP32 server URLs must use HTTPS. If the enhanced weather route is unavailable, the
Windows simulator falls back to Open-Meteo.

## Verification commands

```powershell
.\scripts\test.ps1
.\scripts\capture-screenshots.ps1
.\scripts\verify-live-simulator.ps1
.\scripts\capture-v10_3-weather-evidence.ps1
```

Eighteen named views are rendered to genuine 480 × 480 captures, converted losslessly to PNG and assembled into `artifacts/v8/AURA_V8_PRODUCTION_SCREENS.png`. The set includes the four animated AI states, short/long answers, quick actions, notifications, settings, Audio, Aoraton rooms/chat/compose/confirm, offline/error, battery and diagnostics. More detailed steps are in [simulator/WINDOWS_SETUP.md](simulator/WINDOWS_SETUP.md) and [simulator/INTERACTION_TESTS.md](simulator/INTERACTION_TESTS.md).

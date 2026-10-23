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

The second command starts hidden loopback helpers, opens the control panel and runs the visible simulator in the foreground. Closing the simulator tears down helpers owned by the script. When `codex` is not on `PATH`, the script uses the newest `codex.exe` shipped with the Codex desktop app (`%LOCALAPPDATA%\OpenAI\Codex\bin\…`) for that run only; `AURA_CODEX_PATH` overrides it.

## Your aoraton.com account

Run `.\scripts\setup-aoraton.ps1` once. It reads the public Supabase URL and anon key from `..\incognitochat\services\supabaseConfig.ts` (read-only) into the git-ignored `config\aoraton.local.json`. After that, `run.bat` and `dev-simulator.ps1` start Aoraton in live mode.

1. Open Aoraton and choose **Σύνδεση με Google**.
2. Finish signing in in the browser that opens.
3. Open **Κλειδιά δωματίων** and type the aoraton key-vault passphrase. Alternatively, copy an invite link on aoraton.com and choose **Πρόσκληση**.

If the browser stays on aoraton.com, add `http://127.0.0.1:4779/**` to Supabase → Authentication → URL Configuration → Redirect URLs.

Rooms refresh every 10 s (polling, not realtime). Sending always goes through the confirmation screen. Without the config file, Aoraton shows DEMO rooms.

## Voice and companion features (v13)

Speech stays on this PC. Install the local recogniser once (about 200 MB under the ignored `.tools\whisper`, SHA-256 pinned):

```powershell
.\scripts\setup-voice.ps1
```

With the bridge running (`dev-simulator.ps1`), tapping the orb records from the PC microphone, stops after a pause, recognises Greek with whisper.cpp, asks the provider with the device facts and the recent conversation, and speaks the answer with the installed Windows voice (Greek "Stefanos"; English "Zira" for translations). Without the recogniser, Listening falls back to the labelled synthetic input. Voice notes, translation and "read aloud" use the same path.

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
- Home `Ήχος`: open real Windows audio controls and play a tone or bundled WAV.
- Home `Aoraton`: open fictional local `DEMO` rooms, conversation, compose and confirmation screens.
- Home `Εργαλεία`: open the compact launcher for Assistant, Aoraton, Audio and Notifications; Settings remains available from the gear control.
- The centered `DEV` marker appears only when the simulator explicitly enables developer mode.

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

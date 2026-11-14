# The first flash

How to put AURA 1.0.0 on the Waveshare ESP32-S3-Touch-AMOLED-2.16 the first
time, and what to check on the board. Everything here uses the development
build. Nothing in it can lock the board.

## Never use the production build for this

`scripts/build-firmware-production.ps1` and `sdkconfig.production.defaults`
turn on secure boot v2 and flash encryption in **release** mode.

- Those are written into the chip's eFuses on the first boot and **cannot be
  undone**.
- After that the board accepts only images signed with that secure-boot key,
  and USB flashing of a plain image stops working.

Use them only after every check below has passed, and only once the secure-boot
key is kept safely outside the repository.

## What you need

- ESP-IDF 5.5.5 at `C:\Users\polis\esp\v5.5.5\esp-idf` (`docs/HARDWARE.md`).
- A USB-C data cable, and the board's COM port: Device Manager → Ports, for
  example `COM5`.
- A microSD card is optional. Notes, photos, the radio's favourites, the app
  order and the settings live on it, formatted FAT32. Without a card the device
  runs, the assistant listens and answers, and those stay in memory only.

## 1. Build and flash

In PowerShell:

```
. C:\Users\polis\esp\v5.5.5\esp-idf\export.ps1
cd C:\Users\polis\Documents\AiCube
.\scripts\build-firmware.ps1
cd firmware
idf.py -p COM5 erase-flash
idf.py -p COM5 flash monitor
```

- `erase-flash` is for the first time only. It clears whatever the factory put
  in NVS. Later flashes keep NVS, which holds Wi-Fi, the keys and the Aoraton
  account.
- If the port is not found, hold BOOT, tap RESET, release BOOT and run the
  command again.
- `Ctrl+]` leaves the monitor.

## 2. What the serial log should say

In this order, within a few seconds:

- `AURA boot: Waveshare ESP32-S3-Touch-AMOLED-2.16 profile ...`
- `Board clock ready, power manager ready`: the PCF85063 and the AXP2101.
  `unavailable` means the I²C bus or an address is wrong.
- `ES7210 microphone ready`
- `Audio backend initialization: ready`
- `Network: Wi-Fi radio started`
- `UI ready N ms after boot`
- `Assistant: no cloud keys yet ...`: expected until step 4.
- Then `metrics ...` every 10 s for two minutes: frame and loop times, the
  presence's frame rate, and free internal memory and PSRAM. Copy those lines
  into the report; they are the first real performance numbers.
- With each, `stack main_min_free=N`: the least stack the UI loop ever had
  left, of 12 KiB. Below about 2000 it needs more
  (`CONFIG_ESP_MAIN_TASK_STACK_SIZE` in `sdkconfig.defaults`).
- The same numbers are on the device: Ρυθμίσεις → Σύστημα → Διαγνωστικά.

If it crashes, the monitor prints the backtrace. Without a monitor, the crash
is kept in the `coredump` partition: `idf.py -p COM5 coredump-info` reads it.

The screen should show Home in the default look (Άλως). Settings → Σύστημα
shows `AURA 1.0.0` under the software update row.

## 3. Wi-Fi

Settings → Συνδέσεις → Wi-Fi, pick the network, type the password. The clock
sets itself from the network within a minute and is written to the board clock.
From then on the board keeps the time across restarts without Wi-Fi.

## 4. The assistant's keys

On the device: Settings → Συνδέσεις → Μεταφορά αρχείων → start. It shows an
address and a six-digit code. On the PC, on the same network:

```
.\scripts\send-device-keys.ps1 -Address http://<address shown> -Code <code shown>
```

It sends GROQ_API_KEY and AZURE_SPEECH_KEY from `.env` once; nothing is
printed. The log says `Assistant: Groq answers, Azure speaks`, and Settings →
Βοηθός → Πάροχος AI shows `Groq · φωνή Azure`.

Then try, on the talk target:

- «Τι ώρα είναι;» and «Πόση μπαταρία έχω;» (answered from the device facts);
- «Βάλε χρονόμετρο πέντε λεπτά» (it asks first; confirm it, and Ρολόι shows
  the timer);
- «Βάλε Σφαίρα» (the radio plays without asking the assistant);
- after a spoken answer, ask a follow-up without touching anything (Ήχος →
  continuous conversation), and once speak over an answer to stop it
  (barge-in; if it ever cuts an answer by itself, turn it off in Ήχος and note
  it);
- a voice note with the card in: its words appear under it after a few
  seconds;
- «Στείλε στην Οικογένεια ότι αργώ» once Aoraton is signed in: Aoraton's
  confirmation opens for that room.

## 5. The update signing key

Updates over the air install only images signed with the owner's key:

```
node scripts/update-signing.mjs keygen D:\keys\aura-update.pem
```

- Keep the `.pem` outside the repository, with an offline copy. Without it no
  update can ever be signed for this device.
- Paste the `CONFIG_AURA_OTA_PUBLIC_KEY=...` line it prints into
  `firmware/sdkconfig.defaults`.
- Delete `firmware/sdkconfig`, build again, and flash with step 1 but without
  `erase-flash`.

`releases/README.md` describes publishing an update.

## 6. Checks on the board

Each check's result goes into `docs/VERIFICATION.md`, replacing NEEDS
HARDWARE.

| Area | What to check |
|---|---|
| Display | No tearing or stripes; the header's glass is smooth; brightness 5–100% in Οθόνη |
| Touch | Every corner reacts; scrolling lists; swiping back |
| Audio | The test tone in Σύστημα → Διαγνωστικά; volume and mute; a spoken answer; the radio for 10 minutes |
| Microphone | A question, a voice note (with a card), a voice message in Aoraton |
| Power | Κατάσταση συσκευής: percentage, charging stage, voltages and temperature against a USB meter; power off and on with the side button |
| microSD | Free space after a minute; Διαμόρφωση κάρτας on a spare card |
| Motion | Raise to wake; face down mutes |
| Clock | The time survives a restart without Wi-Fi |
| Phone | Pairing an iPhone and a notification arriving |
| Aoraton | Sign-in from the phone, reading, a reply |
| Memory | The `metrics` lines after 10 minutes of use: internal free memory must not keep falling |
| GPIO46 | The speaker stays silent during boot and reset |

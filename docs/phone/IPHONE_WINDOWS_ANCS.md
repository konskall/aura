# iPhone ANCS through the Windows simulator

Status: **implemented experimentally and validated with a real paired iPhone in the Windows simulator**. Physical ESP32-S3 validation still needs the board.

The Windows simulator now uses the same ANCS packet codec as the ESP32-S3 firmware. The native Windows transport enumerates an authorised ANCS GATT service, subscribes to Notification Source and Data Source, requests bounded attributes through Control Point, and hands notifications to `PhoneNotificationModel` through a bounded queue. Bluetooth callbacks never touch LVGL.

Notification titles and bodies remain in memory. The transport does not print them and does not write them to the simulator control-event log. Only the ordinary in-memory phone screen and assistant privacy gate can consume them.

## Pair and test

1. Open Windows Bluetooth settings:

   `./scripts/start-iphone-ancs-simulator.ps1 -OpenBluetoothSettings`

2. Pair the iPhone and approve any Bluetooth notification-sharing prompt shown by iOS/Windows.

3. Check whether Windows exposes the authorised ANCS service:

   `./scripts/start-iphone-ancs-simulator.ps1 -ProbeOnly`

   For a controlled live window, build the probe and run `./build/native/aura_ancs_probe.exe --seconds 60`, then generate a new notification while it is listening. Apple does not guarantee replay of notifications that existed before subscription.

4. Start the simulator with the real transport:

   `./scripts/start-iphone-ancs-simulator.ps1`

   To run the same native simulator with the real Codex CLI bridge and ANCS together:

   `./scripts/dev-simulator.ps1 -Provider codex -EnableIphoneAncs`

   Pairing and notification permission are owned by Windows Phone Link. Once Windows exposes the authorised ANCS service, AURA attaches automatically; there is no second pairing step inside AURA. In a normal interactive run the AURA Pair button opens Windows Bluetooth settings and cannot fabricate a successful connection. Labelled DEMO pairing remains available only to deterministic evidence and explicit web-control demo events.

If the probe reports `ANCS_SERVICE_NOT_FOUND`, the USB adapter is usable but Windows has not exposed an authorised iPhone ANCS service. Re-pairing may be required. This is different from the labelled DEMO injection available in the web controls.

The simulator's Pair button opens Windows Bluetooth settings. Forget stops the local transport immediately and also opens those settings so the user can explicitly remove the Windows bond. Incoming records are marked live (`demo=false`); no phone number, message or device result is fabricated. Notification actions are sent to ANCS only from the existing explicit UI buttons.

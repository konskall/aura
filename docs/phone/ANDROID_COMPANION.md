# AURA Link for Android (specification)

Status: **SPECIFIED, NOT BUILT.** iPhones need no app: AURA reads them through Apple's Notification Center Service (`firmware/components/aura_companion/src/esp32_phone_link.cpp`). Android has no equivalent built in, so a small companion app forwards notifications over BLE. The device side already has everything except this transport: `PhoneNotificationModel`, the screens, and the assistant's phone context.

## What the app does

1. Asks for the **notification access** permission (`NotificationListenerService`) and Bluetooth permissions (`BLUETOOTH_CONNECT`, `BLUETOOTH_SCAN` on Android 12+).
2. Scans for the AURA advertisement (local name `AURA`), bonds (LE Secure Connections, Just Works on the watch side, numeric comparison on the phone) and keeps a GATT connection.
3. Forwards posted, updated and removed notifications, and the incoming-call state from `TelecomManager`, to AURA.
4. Executes AURA's commands: dismiss a notification (`cancelNotification(key)`), answer or decline a call (`TelecomManager.acceptRingingCall` / `endCall`, with `ANSWER_PHONE_CALLS`).
5. Lets the owner choose which apps are forwarded (default: messaging, phone, calendar). Nothing is stored or sent anywhere else.

## GATT service on AURA (peripheral)

Service UUID `6E7A0001-5A55-4155-5241-4C494E4B0001` (`AURA LINK`).

| Characteristic | UUID suffix | Properties | Direction |
|---|---|---|---|
| Inbox | `…0002` | Write without response, Write | phone → AURA |
| Commands | `…0003` | Notify | AURA → phone |
| Version | `…0004` | Read | `1` |

All values are TLV records: `type (1 byte) | length (2 bytes, little endian) | value`. Strings are UTF-8 and are bounded by AURA again (app 40, title 80, message 300 bytes).

### Inbox records (phone → AURA)

| Type | Meaning | Fields (nested TLV) |
|---|---|---|
| `0x01` | Notification posted/updated | `0x10` uid (uint32), `0x11` category (`PhoneCategory` id), `0x12` app name, `0x13` title, `0x14` message, `0x15` flags (bit0 important, bit1 can accept, bit2 can decline) |
| `0x02` | Notification removed | `0x10` uid |
| `0x03` | Incoming call | `0x10` uid, `0x13` caller |
| `0x04` | Call ended | `0x10` uid |
| `0x05` | Hello | `0x16` platform `2` (Android), `0x17` device name |

A record larger than the negotiated MTU is split across writes; AURA reassembles by the outer length (maximum 1 KiB) like `ancs::AttributeAssembler`.

### Command records (AURA → phone)

| Type | Meaning | Fields |
|---|---|---|
| `0x81` | Perform action | `0x10` uid, `0x18` action (0 positive / answer, 1 negative / decline or dismiss) |
| `0x82` | Resend all | none (after reconnect) |

## Device-side work still needed

- A NimBLE GATT server for the service above next to the ANCS client, mapping records to `PhoneEvent`s (the same queue the ANCS client feeds) with `PhonePlatform::Android`.
- A TLV parser in `shared/core/src/phone_link.cpp` with unit tests mirroring the ANCS ones.

## Privacy

Notification text exists only in AURA's RAM, is never logged, and reaches the assistant only when a question is about the phone or its messages. Unpairing on either side clears it.

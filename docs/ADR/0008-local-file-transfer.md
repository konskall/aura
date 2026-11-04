# ADR 0008: Temporary local file transfer

Status: accepted, 2026-10-31, owner-requested.

File transfer is a separate, explicitly enabled LAN service. It does not change
the AI bridge's loopback binding, bearer token or capabilities. Opening its screen
does not open a listener. Enable creates a fresh cryptographically random six-digit
code and an independent 256-bit bearer, valid for ten minutes. The QR contains only
the numeric local URL. Five failed code attempts lock the session; Stop, expiry,
power off and reset revoke access. No credentials or payloads are logged/persisted.

Both platforms use shared request validation, session policy, streaming protocol,
localized browser page and LVGL screens. Windows owns Winsock/Unicode file handles;
ESP32 uses esp_http_server and FAT32. Background workers never mutate LVGL. Only
private IPv4 peers and the advertised Host/Origin are accepted; there is no CORS,
cookie auth, public hostname, remote proxy or cloud dependency. The UI/server page
describes trusted local Wi-Fi; HTTP is not claimed to provide transport encryption.

Only upload is exposed, into aura/photos (JPG/PNG, 4 MiB, validated by the bounded
decoder) or aura/files (passive files, 16 MiB). Requests cannot access preferences,
credentials, other directories, directory listings or downloads. Names are bounded
display-safe UTF-8 basenames, with traversal/reserved names rejected. One file is
streamed at a time through 4 KiB chunks into a private staging file, flushed and
then renamed without replacing any destination. Partial/invalid files are removed;
already committed files are preserved. Stop revokes future work; an already
committed final rename remains complete. Accepted photos trigger owner-thread
catalog refresh without interrupting an open viewer.

The device listener uses a dedicated manager and HTTP task. It closes on Wi-Fi
loss and stays disabled after boot. The simulator chooses a private Wi-Fi/Ethernet
address, falling back to loopback for access from that PC. It never adds Windows
firewall rules. Real phone access and card/display behavior require hardware/LAN
validation; all automated files and codes are synthetic.

## Owner amendment: file management and photo preparation

The owner requested file/photo deletion after reporting JPEG channel inversion
and compressed PNG resolution rejection. The same authenticated service now
offers POST `/api/files` with a bounded basename cursor and POST `/api/delete`
with an exact validated area/name and explicit `confirm:true`. The localized
browser asks for confirmation; native photo settings use a separate confirmation
screen and a typed bounded file-worker job. Only ordinary files under the two
allowed areas can be removed. Windows deletes a validated non-reparse handle;
directories, preferences, staging and other paths remain inaccessible. No
download API is added. Lists contain at most 128 entries per page and inspect
at most 2,048 entries per scan. Removing an ambient photo clears its persisted
selection; deleting externally invalidates cached pixels on the UI thread.

The browser prepares larger photos locally into at most 480x480 pixels, preserving
the source file and aspect ratio. Device byte/decode limits remain unchanged;
resolution and byte-size errors have distinct codes. JPEG BGR conversion and
MCU sampling are shared across simulator and firmware. Reset still preserves
owner files. Hardware color/timing/removal checks remain NEEDS HARDWARE.

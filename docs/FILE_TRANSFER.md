# Local file transfer

Open **Ρυθμίσεις → Μεταφορά αρχείων** or the transfer button in **Φωτογραφίες**.
Press **Ενεργοποίηση**. The device shows a QR code, a local address and a six-digit
code. On a phone/computer on the same trusted Wi-Fi, scan the QR or open the
address, enter the code and choose/drop files. Press **Αποστολή στη συσκευή** to
confirm the selected queue. The browser page supports Greek and English and
works at desktop/mobile widths without external assets or an installed app.

- Automatic destination: JPG/JPEG/PNG go into `aura/photos`; other files into
  `aura/files`. The browser can also select the destination explicitly.
- On the Windows simulator these folders are under
  `simulator/runtime/companion` (or the configured companion store).
- Photos: the browser automatically fits larger JPG/PNG images to the 480x480
  display, preserving aspect ratio, filename and the original phone/PC file.
  PNG keeps alpha; JPEG is exported at high quality with orientation normalized.
  Source photos up to 32 MiB can be prepared by the browser. The actual upload
  remains at most 4 MiB and is validated by the device decoder. A direct API
  client must meet the [photo format/expansion limits](PHOTOS.md) itself.
- Other files: at most 16 MiB. They remain passive stored files; uploading a
  document/program does not add a viewer or execute it.
- Up to 128 uploads per activation; the photo library holds 128 photos.
- Existing filenames are rejected. Rename the source and select it again to
  keep both copies. Completed queue entries are removed; a failed item and
  remaining entries stay available for retry.
- A completed photo appears in the gallery automatically. An open viewer is
  not interrupted; reopening the gallery refreshes it.

The same session also gives the device its assistant's keys (ADR 0009): with File
transfer open, run `scripts/send-device-keys.ps1 -Address <address> -Code <code>` on the
PC. It reads `GROQ_API_KEY`, `AZURE_SPEECH_KEY` and `AZURE_SPEECH_REGION` from the
ignored `.env`, signs in with the code and posts them to `POST /api/keys`. The device
checks their format and keeps them in encrypted NVS; no response echoes them and
nothing logs them. The simulator has no such endpoint (it reads `.env` itself).

On the device the same session also signs it in to aoraton.com (ADR 0010). `node scripts/aoraton-device-sign-in.mjs
--address <address> --code <code>` posts the project's public values from `config/aoraton.local.json`
(`POST /api/aoraton/config`), asks for a sign-in (`/api/aoraton/sign-in`), reads the authorize URL
(`/api/aoraton/status`), opens it in the PC browser, listens on 127.0.0.1:4779 and hands Google's
one-time code back (`/api/aoraton/code`). An invite link goes to `/api/aoraton/invite`. Every call needs
the session; none echoes a key, code or invite.

The owner's phone can do the same without a PC (ADR 0010, 2026-11-07). The account screen's button
starts File transfer, and once the server is ready the QR holds
`http://<device>/aoraton/phone?s=<secret>` instead of the address. `GET /aoraton/phone` sends the
phone on to Google's page. Google's page returns it to a page on aoraton.com (Supabase returns a
phone to no local address), which hands the one-time code to `GET /aoraton/callback`. These two pages
need no code: the QR's one-time secret stands in for it, and eight wrong secrets end the sign-in. They
keep the private-peer and Host checks, run no script, send no referrer and never show the code.

After code login, **Αποθηκευμένα στη συσκευή / Saved on the device** lists photos
or other files, 128 per page. Select the destination, refresh or use Next page.
Press Delete beside an exact filename, then confirm it in the dialog. Cancel
does nothing. Deletion is permanent; directories, settings and other storage
areas cannot be listed/deleted. No file content can be downloaded through this API.
Deleting a photo refreshes the gallery and clears a removed ambient selection.

The lease ends after ten minutes, even during a transfer. **Διακοπή πρόσβασης**,
expiry, reboot/reset and device Wi-Fi loss revoke access. Five incorrect codes
lock it until enabled again. Incomplete/invalid staging files are removed;
completed files are preserved. Access is off after boot. The QR contains the
address only (during a phone sign-in, the sign-in page with its one-time secret);
the code is entered separately. Credentials/payloads are not logged or persisted.
Uploads involve no cloud, assistant or AI bridge.

This is a local HTTP service, not encrypted transport. Use trusted local Wi-Fi.
Host/Origin/private-peer checks and a memory-only bearer protect the upload API.
Listings/deletion require the same bearer as uploads and are scoped only to
`photos`/`files`; downloads, settings and other device actions are unavailable.
See [ADR 0008](ADR/0008-local-file-transfer.md).

The Windows simulator advertises an active private Wi-Fi/Ethernet IPv4 address;
without one it falls back to loopback and clearly says access is from that PC.
The globe button opens the local page on that PC. Windows may ask to allow the
simulator on private networks before a phone can connect; no firewall rule is
installed automatically. The device uses its existing Wi-Fi station address on
port 8788. No access-point/hotspot mode is added.

Automated checks use synthetic loopback storage. Optional real-browser QA:
`node tests/interop/run_file_transfer_browser.mjs` uses a fresh isolated browser
profile and a test-only native server, not the owner's account or photo folder.
Physical phone/QR/card throughput and removal tests remain NEEDS HARDWARE.

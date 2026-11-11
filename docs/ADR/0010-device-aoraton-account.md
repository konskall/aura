# ADR 0010: The device's aoraton account, signed in through the owner's phone or PC

Status: accepted, 2026-11-06 (the owner's request and choices). It completes the
device part that `docs/design/AURA_AORATON_ACCOUNT_LOGIN_DESIGN.md` left for later
("the device will later sign in through the phone"). The simulator's own sign-in
is unchanged. Device pairing through aoraton.com is not used: it needs changes to
aoraton.com's server, so it remains the owner's decision.
Amended 2026-11-07 (the owner's request): the phone signs the device in without a PC.
Amended again 2026-11-07, after the first live try: Supabase returns a phone to no
local address, so the phone returns through a static page on aoraton.com (the
owner's choice).

## Decision

The ESP32 runs the same shared account worker as the simulator
(`AoratonAccountWorker`) behind its five platform interfaces:

- **HTTP:** `esp_http_client` with the certificate bundle. It is https only, follows no redirects and caps the answer.
- **Crypto:** mbedTLS and the hardware random generator.
  - PBKDF2 runs in slices and yields, so the vault's 600,000 rounds never starve the watchdog or the UI.
  - A known-answer self-test (`aoraton_crypto_self_test`) runs before the account starts. The PC tests run it against the Windows adapters, which keeps its vectors honest.
- **Secrets:** encrypted NVS, namespace `aura_aoraton`.
  - One bounded record holds the refresh token and the room keys (`aoraton_encode_secrets`).
  - The project's public URL and anon key are stored beside it.
- **Realtime:** `esp_transport` (TLS and WebSocket) on its own task. It uses the shared Phoenix framing (`AoratonRealtimeProtocol`), a heartbeat every 20 s and reconnection with backoff.
- **Sign-in:** through the owner's phone or PC (`AoratonDeviceLink`). A screen of 2 inches cannot show Google's page.

### Sign-in through the PC

1. On the device, the owner opens **Μεταφορά αρχείων** (the account screen's button does this). The device shows its address and a six-digit code.
2. On the PC, the owner runs `node scripts/aoraton-device-sign-in.mjs --address <address> --code <code>`. It signs in to that code-protected session, then posts the project's public values from `config/aoraton.local.json` to `POST /api/aoraton/config`.
3. The PC asks for a sign-in (`POST /api/aoraton/sign-in`). The device's worker creates the PKCE verifier and the authorize URL, whose return address is `http://127.0.0.1:4779/aoraton/callback`. Supabase accepts a loopback return address without a Redirect URLs entry.
4. The PC reads the URL (`POST /api/aoraton/status`) and checks two things:
   - the URL belongs to the configured project;
   - its return address is this PC's loopback.

   It then listens on 127.0.0.1:4779 and opens Google's page in its browser.
5. Google's redirect brings the one-time code to the PC. The PC hands it to the device (`POST /api/aoraton/code`), or reports a refusal.
6. The device exchanges the code with its own verifier. Its session and refresh token never leave it.

The room keys are unlocked on the device: the owner types the vault passphrase on its keyboard. An invite link can come from the PC with `--invite <link>` (`POST /api/aoraton/invite`).

The helper prints no code, token, key or invite. The device's responses echo none of them.

The settings are marked as stored as soon as the device keeps them. A sign-in or invite the PC sends while the account restarts with them waits until it is ready (`aoraton_link_pump`). Before 2026-11-06 the PC could hit that restart and be told the device could not start the sign-in.

**The simulator as the device.** `run.bat device` runs the same link, pump and worker in the simulator (`AoratonDeviceMode`). Windows stands in for the device's TLS, crypto and NVS, so the sign-in can be tried live before the board arrives ([SIMULATOR.md](../SIMULATOR.md)).

### Sign-in from the phone (amended 2026-11-07)

The owner's request: sign in from the phone, without a PC, and without code on aoraton.com.

1. **The project's public values.** Without a PC the device needs them already. The firmware build reads them from the git-ignored `config/aoraton.local.json` (written by `scripts/setup-aoraton.ps1`) into a generated header. Values the PC helper stored in NVS come first. The simulator's device mode takes the same values from `AURA_AORATON_URL`, `AURA_AORATON_ANON_KEY` and `AURA_INCO_ENABLED`, which `dev-simulator.ps1 -AoratonDevice` sets from that file.
2. **The QR.** The owner taps **Σύνδεση με Google**. File transfer starts. Once its server is ready, the QR holds `http://<device>:<port>/aoraton/phone?s=<secret>` instead of the transfer page's address.
   - The secret is 16 random bytes (22 characters of base64url), new for each tap.
   - The address and the six-digit code stay beside the QR for the PC's way.
3. **To Google.** The phone, on the same Wi-Fi, opens the QR. `GET /aoraton/phone` asks again every second while the account starts the sign-in, then sends the phone on to Google's page. The authorize URL's return address is aoraton.com's relay page, `https://aoraton.com/aura-phone/?d=<device>:<port>&s=<secret>` (`IAoratonSignInChannel::redirect_url`, `AoratonDeviceLink::kPhoneRelay`).
4. **Through aoraton.com.** Google, then Supabase, send the phone to that page, and Supabase adds `code` to its query. The page (`public/aura-phone/` in aoraton's repository) takes the code out of its own address and history, then sends the phone on to `http://<d>/aoraton/callback?s=<secret>&code=<code>`, or `&error=<error>` for a refusal.
   - It forwards only to a private IPv4 address with a port (10/8, 172.16/12, 192.168/16) or 127.0.0.1, with a 22-character secret, a code of at most 256 characters and an error of at most 64. Any other address gets a page that says the link leads to no Aura.
   - It stores, logs and sends nothing else, loads only aoraton's own scripts and sends no referrer. aoraton's service worker leaves it to the network, so the app never sees the code.
5. **Back to the device.** `GET /aoraton/callback` hands the code to the account, which exchanges it with its verifier, as in step 6 above. A refusal (`error`), or an answer with neither a code nor an error, ends the sign-in as refused.

The two pages need no PIN: the secret stands in for it.

- They still answer only the local network, and only at the device's own address (the Host check).
- The link compares secrets in constant time, and eight wrong ones end the phone sign-in. A malformed address reaches no link.
- The secret ends with the sign-in, with File transfer, or with the PC's own sign-in, which replaces the phone's.
- The pages never echo the code. They carry no script and send no referrer, so neither Google nor Supabase sees the secret in a `Referer` header. They are in Greek with English beneath. Since 2026-11-11 they look like aoraton.com's relay page, with aoraton's mark and Aura's wordmark embedded in them: the transfer server's CSP allows `data:` images and nothing from elsewhere.

Signed in from File transfer, by the phone or by the PC, the device goes back to where the sign-in began and says so. File transfer itself stays on, as before, until its ten minutes end or the owner stops it, because the PC helper may still send an invite.

**Why a page on aoraton.com.** Since 2026-06-01 (supabase/auth #2479, after RFC 9700), Supabase accepts no IP address as a return address except loopback, whatever the project's Redirect URLs say, and falls back to the Site URL instead. The first live try (2026-11-07), with `http://192.168.*.*/**` among the Redirect URLs, ended on aoraton.com, and the device's sign-in waited until it timed out. The project's own domain is always accepted, so the page needs no Supabase setting. It is one static page and one line in aoraton's service worker (the owner's choice).

- If the page is down or changed, the phone stops on aoraton.com and the device's sign-in times out. The PC's way does not use it.
- **The risk.** The page forwards to any private address, so a sign-in link that someone else started could hand that person a session at a host on the network the phone is on, if the owner signs in through that link. PKCE does not stop this, because such a link carries the other person's own challenge. Sign in only from the QR your own device shows.

### Hybrid sync (the owner's choice)

Realtime runs while the screen is lit, or while the device is on USB power (the AXP2101 reports it). On battery with the screen dark (`realtime_sync_allowed`):

- the WebSocket closes;
- the room overview is checked every five minutes, whatever the polling setting.

Lighting the screen catches up at once and reconnects realtime.

### Photos and voice messages (the owner's choice)

Files are encrypted exactly as aoraton.com does it (`utils/attachmentCrypto.ts`):

- each file has its own AES-256-GCM key, wrapped under the room key's `inco/e2ee/v2/file` subkey;
- the file lives in the public `attachments` bucket under the SHA-256 of the room key.

AURA fetches only that bucket of the configured project, at most 4 MiB a file, and opens it with the room's key.

- **Photos.** The newest six of a conversation get a thumbnail of at most 216 px. A tap opens the photo at screen size, and leaving the photo frees it.
  - At most twelve thumbnails stay in memory. The oldest outside the open conversation go first, then the open one's oldest.
  - Opening a conversation again fetches its newest dropped thumbnails once more, at most six.
- **The microSD card.** Every attachment fetched is kept on the card exactly as the public bucket serves it: the same AES-GCM bytes, named by the SHA-256 of its address.
  - A photo whose thumbnail arrived opens without a second download.
  - A thumbnail dropped from memory comes back from the card.
  - A voice message plays again without the network.
  - Opening checks the copy with AES-GCM. One that fails is dropped and fetched again.
  - The cache holds at most 128 MiB, the least recently used file out first, and Αποσύνδεση empties it.
  - The simulator keeps the same cache beside its vault.
- **Voice messages.** A tap plays the message and the next tap stops it.
  - Voice plays as 16 kHz mono, at most two minutes. Faster rates are averaged down as they are decoded.
  - The shared demuxer reads MP4 (whole or fragmented: Safari's AAC, Chrome's Opus or AAC), WebM (Firefox's Opus) and WAV.
  - The platform decodes:
    - the device: Espressif's `esp_audio_codec` 2.6.2 (AAC and Opus), whose license allows use only on Espressif chips;
    - the simulator: Media Foundation (AAC everywhere; Opus where an Opus decoder is installed).

## Rationale

- **Almost no change to aoraton.com.** The PC's way reuses the same PKCE flow, return address and rows as the simulator's approved design. The phone's way adds only the static relay page: no server code, no database change and no Supabase setting.
- **Secrets stay on the device.** The PC relays only a code that is useless without the device's verifier. A copied session would also break: Supabase rotates refresh tokens and revokes reused ones, so sharing one session between the PC and the device would sign both out.
- **The session is one that already exists.** The owner already opens File transfer with the six-digit code, five wrong codes lock it, and it lasts ten minutes. The account link adds five small JSON calls inside that session.
- **The battery.** A dark screen on battery is the device's longest state, and a five-minute check costs less there than a socket held open.

## Consequences

- The local network carries the PKCE code, which is useless without the verifier, and any invite link, which carries one room's key, in plain HTTP. The phone's way carries the code and the QR's secret the same way, after they pass through aoraton.com's page over HTTPS. Use File transfer on a trusted Wi-Fi, as for files and the assistant's keys.
- The device holds a normal session of the owner's account. Signing out everywhere on aoraton.com (supabase-js `global` scope) also ends it. Αποσύνδεση on the device wipes the record and calls `logout?scope=local`.
- The account stays signed out until it has the project settings: built in from `config/aoraton.local.json`, or sent by the PC. It never falls back to DEMO.
- The phone's way depends on aoraton.com's relay page (above), and carries the risk described there.
- The card keeps only what the public bucket already serves to anyone, but it shows how many files were opened, and their sizes.
- **Memory.**
  - The worker has a 16 KiB internal stack, because it writes NVS.
  - The realtime task has 8 KiB, and TLS allocates from PSRAM.
  - Voice decoding runs on a short thread with a 24 KiB stack in PSRAM.
  - A photo or voice file of up to 4 MiB and its decoded copy live in PSRAM, after a check that the heap has room.
  - Twelve thumbnails take about 1.1 MiB, and two minutes of voice 3.7 MiB.
  - None of this is measured on the board.
- **Firmware size.** The account worker, its adapters and the decoders left 10% of the 6.5 MB app slots free, below the 15% floor (13% before the decoders). The owner chose two 7 MB slots: 16% free (`docs/PERFORMANCE_BUDGET.md`, `docs/HARDWARE.md`).
- **Not verified on hardware.** None of this has run on the board yet (`docs/VERIFICATION.md`, NEEDS HARDWARE):
  - the self-test, the vault's PBKDF2 time and the TLS and WebSocket behaviour on the ESP32-S3;
  - the phone's way on the board (on 2026-11-11 the simulator ran it live with a real phone, Google and aoraton.com's page);
  - the five-minute check's power.

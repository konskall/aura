# Aoraton live architecture

Updated: 2026-10-21

## Implemented boundary

```text
LVGL UI (UI thread)
  ⇅ AoratonSnapshot / typed user intents
RealAoratonService (shared, UI thread)
  ⇅ bounded AoratonMailbox commands/events
platform worker (Windows/ESP32; transport pending)
  ⇅ crypto adapter + secure vault + TLS REST/Realtime
isolated Aoraton/Supabase TEST project
```

`RealAoratonService` accepts only authorized room records and authenticated/decrypted message events. It enforces pairing, per-room key availability, UTF-8/size bounds, destination confirmation, idempotency identifiers, dedupe, 100-message memory bounds, unread counts and privacy-minimal notifications. Background code never receives LVGL pointers.

## Implemented platform pieces

- Windows: CNG HKDF-SHA256 and AES-256-GCM, CSPRNG IVs, DPAPI current-user vault.
- ESP32 source: matching mbedTLS HKDF/AES-GCM adapter and `esp_fill_random`; compile and hardware validation remain pending.
- UI: not-paired landing, QR challenge, fallback code, expiry, SAS confirmation, locked-room semantics, safe cancellation.
- Simulator composition: `AURA_AORATON_MODE=mock|test|live` is explicit. TEST/LIVE instantiate `RealAoratonService`; an invalid value terminates rather than downgrading to mock. Until a reviewed transport is present, a platform fail-closed adapter consumes bounded commands and returns typed failures without fabricated state or network access.

## Pending transport contract

The worker must own one serialized auth refresh path, update Realtime auth after JWT rotation, and publish bounded events. On reconnect it must subscribe before history fetch, then REST-backfill missed rows. Phase 1 fetches 50 newest text messages, stores at most 100 in the snapshot, orders by `created_at,id`, and dedupes by server `id` plus `client_msg_id`.

The dashboard strategy mirrors the audited web client: authorized room discovery, then one `room_overview` RPC round trip with per-device `lastRead_<room_key>` semantics. A native implementation may use accurately labeled adaptive polling if measured ESP32 Realtime cost is unacceptable; it may not claim instant delivery while polling.

## Activation gate

Live/test transport must not activate until all are present:

- explicit `TEST` mode and isolated project identity;
- verified schema/RLS/RPC export;
- device-scoped auth/grant backend;
- DPAPI/NVS vault provisioned through the reviewed pairing protocol;
- negative RLS and revocation tests.

There is no mock fallback in `RealAoratonService`.

# AURA ↔ Aoraton pairing protocol proposal

Updated: 2026-10-21

Status: client state/UI implemented; cryptographic relay and Aoraton-side backend `BLOCKED` pending review and isolated deployment.

## Security goals

- QR contains public, short-lived data only: version, pairing id, approved TEST origin, device ephemeral P-256 public key and expiry.
- Device retains a high-entropy polling secret that is never in the QR.
- Browser must already have a supported Supabase session, real room membership and each selected room's RK.
- Browser and device derive a transport key with ephemeral ECDH P-256 + HKDF-SHA256 bound to the complete transcript.
- Both display the same six-digit SAS; both sides require explicit human confirmation before accepting the grant/key package.
- Relay stores only public parameters and authenticated ciphertext, enforces one use/short expiry and cannot decrypt room keys.

## Proposed flow

1. Device creates an anonymous Supabase device identity and ephemeral ECDH keypair.
2. `pair-start` returns a pairing id, poll secret and 120-second expiry. QR omits the poll secret.
3. Signed-in browser opens the existing Aoraton app's proposed `/pair` route, scans the QR, checks origin/expiry and displays the AURA device name.
4. Browser selects exact rooms and permissions (`read`, `notify`, optionally `send`). It verifies current membership and local RK possession for every selection.
5. Browser creates its ephemeral P-256 key. Transcript is canonical CBOR/JSON containing version, id, both public keys, expiry, device uid and selected grant digest.
6. Transport key: HKDF-SHA256(ECDH secret, salt=SHA256(transcript), info=`aura/aoraton/pair/v1/transport`). SAS material uses info `aura/aoraton/pair/v1/sas`; first 20 bits render as a zero-padded six-digit number.
7. Browser uploads AES-256-GCM ciphertext with a fresh 12-byte IV and transcript as AAD. Payload contains device-scoped session response, selected grant ids/permissions and selected 32-byte RKs. It never contains the user's browser refresh token.
8. Browser and device compare SAS. Browser approval plus on-device confirmation atomically consumes the one-use challenge.
9. Device writes rotated session material and RKs to secure storage, erases ephemeral private/shared secrets, then performs first authorized sync.
10. Cancel, expiry, mismatch or replay erases ephemeral state and produces no grant.

## Proposed Aoraton-side change (not applied)

Reviewable changes belong in a future authorized Aoraton branch:

- `components/PairAuraDevice.tsx` and an App route for scan/manual-code, room selection, permissions, SAS and revocation;
- `utils/auraPairing.ts` for WebCrypto P-256/HKDF/AES-GCM and strict transcript encoding;
- Supabase migration for `aura_devices`, `aura_device_room_grants`, one-use `aura_pairing_challenges`, expiry/replay constraints and narrowly scoped RLS;
- Edge Functions `aura-pair-start`, `aura-pair-approve`, `aura-pair-poll`, `aura-pair-confirm`, `aura-device-revoke` with auth, origin validation, rate limits and metadata-only logs;
- RPCs for authorized device rooms/overview/send. Each must bind `auth.uid()` to `device_uid` and the exact granted room/permission.

Rollback disables the Functions, revokes all device sessions, removes grant/subscriber rows created for devices, and only then drops pairing objects. Existing browser rooms/messages remain untouched.

## Current AURA implementation

`RealAoratonService` models start/challenge/SAS/confirm/cancel/expiry/paired/revoked/unpair through typed bounded commands/events. LVGL renders the real states and a QR with quiet zone. Screenshot QR data is synthetic `TEST`; no production endpoint or secret is present. The actual ECDH/relay is intentionally not claimed.

# Aoraton integration audit

Audit date: 2026-10-17. Source inspected read-only at `C:\Users\polis\Documents\incognitochat`, branch `main`. No source, production configuration, rules or data were modified. An unrelated untracked marketing PDF was preserved.

## Application and transport

- React 18, TypeScript, Vite 5 PWA; Supabase JS 2 provides Auth, Postgres, Realtime, Storage and Edge Functions.
- Anonymous Supabase sign-in is the default. Google OAuth is optional; there is no required phone identity or conventional contact list.
- Membership is granted through `SECURITY DEFINER` RPCs, principally `join_or_create_room_v2`; RLS gates room data.
- Message history comes from `messages` rows. Realtime subscribes to `messages:<room_key>` with `postgres_changes`, subscribing before the initial fetch and serializing async decrypt/apply operations.
- Private channels include `presence:<room_key>`, `calls:<room_key>`, `mesh:<room_key>` and room/user status topics.
- Sends are optimistic and idempotent through a unique `(room_key, client_msg_id)` contract. Failed responses can be reconciled without duplicate delivery.

Relevant source paths: `services/supabase.ts`, `hooks/useChatMessages.ts`, `hooks/useRoomPresence.ts`, `hooks/useWebRTC.ts`, `utils/optimisticSend.ts`, `types.ts`.

## Room and message model

New E2EE room IDs are opaque `v2.` plus base64url of 18 random bytes. A room key is not a display name. Room metadata includes name, creator, E2EE flag, expiry/TTL, approval and appearance fields.

Core message row fields observed in `useChatMessages.ts` are `id`, `room_key`, `uid`, `username`, `avatar_url`, encrypted `text`, `created_at`, `type`, `attachment`, `location`, `reactions`, `reply_to`, `client_msg_id`, edit flag, optional poll/grounding and encrypted mesh metadata. Page size is 50.

## E2EE v2

Source: `utils/e2ee.ts`, `utils/keyStore.ts`, `utils/roomCipher.ts`, `utils/joinFlow.ts`.

- Content key: random 256-bit Room Key (RK), never derived from the PIN.
- Storage/transfer: RK is kept in IndexedDB and carried in the invite URL fragment; fragments are not sent to the server.
- Cipher: AES-256-GCM with a fresh 12-byte IV.
- Wire: `v2:<base64url iv>:<base64url ciphertext+tag>`.
- Subkeys: HKDF-SHA256 with a 32-byte zero salt and domain-separated info strings for message, file, authentication and safety-code material.
- The optional PIN is an access gate. Its verifier is SHA-256 over a stable tagged/name/PIN input and is bcrypt-protected server-side; it does not decrypt content.
- The server stores verifiers, not RK. Missing/wrong RK or failed GCM authentication produces a locked/rejected result.
- Legacy rooms use PBKDF2-derived AES-CBC and lack GCM authentication. AURA must support legacy only if an explicitly reviewed compatibility requirement remains.
- Calls use WebRTC mesh and seal signaling payloads with the room cipher before sending them over the private `calls:<room_key>` channel.

## Compatibility and risks

The repository is GPL-3.0-only. AURA has not copied its implementation. Any code reuse needs an explicit licensing decision; an independently implemented wire-compatible adapter is preferred.

The browser depends on Supabase session refresh, WebSockets, RLS, IndexedDB and WebCrypto. On ESP32, AES-GCM/HKDF are feasible through vetted mbedTLS APIs, but Supabase Auth/Realtime lifecycle, protected key storage, reconnect/idempotency and TLS memory cost must be proven. The original repository does not contain committed database migrations, so deployed schema/RLS compatibility cannot be reconstructed solely from this checkout.

## Integration status

Implemented: bounded service contract, fictional mock rooms/messages, unread behavior, connection loss/reconnect, invalid-payload rejection, compose/preview/explicit confirmation and simulator UI.

Not connected: production Supabase, real room pairing, E2EE encrypt/decrypt, storage, presence, calls and real delivery. The next safe step is a separate Supabase emulator/test project plus browser-generated known-answer vectors. Production conversations remain out of scope without explicit authorization.

## v11 verified delta — 2026-10-21

The prior status paragraph above describes the pre-v11 baseline. v11 now implements and tests the clean-room E2EE v2 codec, bounded `RealAoratonService`, Windows DPAPI vault, mbedTLS firmware codec source, and native pairing/locked-room UI states. See `INTEROPERABILITY_TEST_REPORT.md` and `PROGRESS.md` for precise status.

Additional source inspected read-only: `App.tsx`, `components/LoginScreen.tsx`, `components/DashboardScreen.tsx`, `components/ChatScreen.tsx`, `components/ChatInput.tsx`, `hooks/useRealtimeRepair.ts`, `utils/optimisticSend.ts`, the E2EE tests, and `supabase/functions/notify-room`/`send-push`. No `.sql` or core Supabase migration exists in the checkout. `supabase` and Docker tooling are absent locally. The configured remote backend was not contacted.

The exact v2 message context is `inco/e2ee/v2/msg`; auth and safety contexts are likewise domain separated. WebCrypto appends the 128-bit GCM tag to ciphertext and uses no AAD for message v2. PIN verifier input is `inco/e2ee/v2/pin|<trimmed lowercase room name>|<trimmed PIN>`. New room ids are `v2.` plus unpadded base64url of 18 random bytes. The room id is not included in v2 message HKDF derivation.

Remaining integration is not production-connected: pairing relay/device grants, real Supabase REST/Realtime, deployed RLS verification and browser UI exchange are blocked on an isolated environment and authorized Aoraton-side changes.

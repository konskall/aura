# Aoraton Auth and RLS review

Updated: 2026-10-21

## Verified from source

| Area | Observed contract |
|---|---|
| Auth | `supabase.auth.getSession()`, `onAuthStateChange`, Google OAuth, and `signInAnonymously()` are used. A stale signed-in Google identity is not silently replaced by a new anonymous uid. |
| Realtime auth | The current access token is supplied through `supabase.realtime.setAuth(token)` before reconnect/rebuild. |
| Join | `join_or_create_room_v2` receives `p_username`, name/PIN verifier fields, room-key verifier fields, create/force flags and returns authoritative `e2ee`/room metadata. |
| Room list | Owned rooms use `rooms.created_by = user.uid`; joined keys come from `subscribers.uid = user.uid`, followed by a `rooms ... in(keys)` read. |
| Overview | `room_overview(p_items)` receives `{room_key,since}` entries and returns unread/last-message metadata. Dashboard refresh is visibility-aware and approximately every 30 seconds. |
| History | `messages` is filtered by `room_key`, ordered newest by `created_at,id`, limited to 50, then shown chronologically. |
| Realtime | Topic name `messages:<roomKey>` registers `postgres_changes`, table `messages`, filter `room_key=eq.<roomKey>`, events `*`. Events are serialized before async decryption. |
| Send | Insert fields include `room_key`, authenticated `uid`, username, ciphertext `text`, `type='text'`, and stable `client_msg_id`; `(room_key,client_msg_id)` uniqueness is relied upon for retry reconciliation. |
| Notifications | Edge Functions authenticate the bearer user, call `is_member`, then use service role only server-side. Message plaintext is not included in notification bodies. |

## Not verifiable from the checkout

There are no `.sql` files, `supabase/migrations`, `seed.sql`, or local Supabase config in the inspected checkout. Consequently the exact deployed RLS policies, grants, function ownership/search paths, constraints and indexes cannot be reproduced or security-approved from this source alone. The hardcoded/ENV configuration points to a remote project; it was deliberately not queried.

## Required device authorization

Do not copy an account/browser refresh token. The proposed backend creates a separate Supabase anonymous device user. A signed-in browser explicitly grants that `device_uid` read/notify/send permissions for selected rooms. Server-side RPCs verify owner membership, device grant, expiry/revocation and requested room on every operation. Room keys are never returned by those RPCs; the browser transfers them through the ECDH channel.

Until those policies and RPCs exist in an isolated project and negative tests pass, device auth, room discovery and sending are `BLOCKED`.

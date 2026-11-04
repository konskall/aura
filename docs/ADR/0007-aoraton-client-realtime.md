# ADR 0007: Aoraton client realtime

Status: accepted, 2026-10-31, owner-requested. No Supabase or Aoraton backend changes.

## Decision

The account worker can consume a portable `IAoratonRealtime` adapter. Windows implements it with native WinHTTP TLS WebSockets and the published Supabase/Phoenix 1.0 JSON protocol. It joins one `messages:<room_key>` channel per authorized room, capped at 64, for `public.messages INSERT` filtered by that room. These are the existing Postgres Changes channels, with normal account JWT/RLS authorization. No global unfiltered messages subscription, SQL, publication, policy, server function, backend file or dependency is changed.

WebSocket frames only invalidate local state. The shared account worker immediately performs its existing authorized REST/overview/history reconciliation, encryption checks and message deduplication. The bounded mailbox and UI-thread ownership remain unchanged. Acknowledged subscriptions and reconnects trigger backfill. The existing 10-second REST timer remains a repair/fallback path; connection alone never claims realtime. Initial history and reconnect recovery remain bounded to the existing newest-50-row fetch.

The native adapter has one network owner and one blocking receiver. Readiness requires all requested room subscriptions to be acknowledged. It coalesces updates into two flags, caps frames at 64 KiB, rejects non-text frames, and uses a 20-second Phoenix heartbeat with a 10-second acknowledgement deadline. Join readiness has a 15-second deadline. Failures reconnect with 1–30 second backoff; changed room membership or JWT starts a fresh generation and discards old-generation events. Tokens remain in worker/adapter memory, are cleared on stop/sign-out and never reach LVGL, URLs, logs or files. The required URL API key is the existing public anon key; the account JWT is only in the join payload. TLS certificate validation stays enabled and upgrade redirects are disabled.

## Owner-controlled periodic checks (2026-10-31)

The account settings now expose an independent periodic-check switch and 10/30/60/300-second intervals. The default remains enabled at 10 seconds. Realtime remains active when this check is off. Explicit user refreshes, initial synchronization, reconnect backfill, failure recovery and JWT maintenance remain independent of the periodic timer. A quiet realtime-only session renews its account JWT before expiry. With periodic checks off, a failed REST synchronization can still recover after the existing 30-second connection retry; it does not become a permanently stopped stream.

Preferences are validated in shared code and travel as a typed bounded command to the worker. The simulator persists a versioned, bounded `aoraton-sync.txt` beside its other local preferences. Firmware uses its existing bounded storage worker for the same preference. No backend setting or account credential is changed. Connection-only status updates no longer enqueue redundant overview refreshes when REST is already connected.

The main room list shows the Aoraton brand beside its title and starts directly with room cards. Subscription status appears on the account card. Acknowledged outgoing delivery is labeled simply “Sent” / “Απεστάλη”; pending, failed and uncertain deliveries retain their distinct states.

## Platform boundary and verification

Business reconciliation, protocol validation and visible state/copy stay under `shared/`; Windows owns socket I/O. An ESP32 adapter is not claimed or fabricated. Until provisioned, it retains the existing fallback behavior through the optional interface. No platform dependency is added to firmware.

Tests use a synthetic worker adapter and a loopback RFC6455 server. They exercise readiness, immediate synchronization, deduplication, fallback, backfill, token rotation, sign-out, fragmented input, heartbeat, reconnect and oversized input. The complete project suite and ESP-IDF compilation are recorded in `docs/VERIFICATION.md`.

Protocol references: [Supabase Realtime protocol](https://supabase.com/docs/guides/realtime/protocol), [WinHttpWebSocketReceive](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpwebsocketreceive).

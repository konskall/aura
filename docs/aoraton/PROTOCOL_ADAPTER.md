# Aoraton protocol adapter

`IAoratonService` is the application boundary. It exposes connection, room selection, bounded incoming fixtures, message preparation, explicit confirmation, cancellation, reconnection and snapshots. UI code never sees credentials or crypto keys.

`MockAoratonService` is implemented and deterministic. A future real adapter should compose:

- session/auth manager;
- Supabase REST and Realtime transport;
- room/pairing manager;
- E2EE v2 crypto adapter;
- idempotent message service using `client_msg_id`;
- bounded local storage and notification adapter.

The real adapter must reject malformed UTF-8/JSON, oversized fields, unknown wire versions and failed GCM tags before creating UI models. Realtime decrypt/apply operations must remain serialized. No chat payload may call the Codex bridge.

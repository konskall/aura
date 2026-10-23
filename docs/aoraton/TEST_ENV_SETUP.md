# Isolated Aoraton test environment

Updated: 2026-10-21

Status: `BLOCKED` — no isolated project/schema export or local Supabase tooling is available.

## Current native TEST-mode checks

The simulator never selects TEST implicitly. With no reviewed backend available, the following command exercises the real-service composition and actual SDL/LVGL pairing touch route, then proves that it fails closed instead of showing fabricated data:

```powershell
$env:AURA_AORATON_MODE = 'test'
.\build\native\aura_simulator.exe --aoraton-pairing-smoke --screenshot .\artifacts\v11\aoraton-pairing-failclosed.bmp
Remove-Item Env:AURA_AORATON_MODE
```

For an interactive unpaired TEST landing, set the same environment variable and run `build\native\aura_simulator.exe`. This is only client-state/UI validation; it does not contact Supabase.

## Safe setup prerequisites

1. Obtain the authoritative core schema, functions, indexes and RLS migrations from the Aoraton owner. Do not reconstruct production RLS from client queries.
2. Create a new Supabase project explicitly named/labeled for AURA integration testing. Never reuse the remote project configured in `incognitochat`.
3. Apply the authoritative migrations to the isolated project, then the proposed device-pairing migration from `PAIRING_PROTOCOL.md` after review.
4. Seed only synthetic TEST users, room names and messages. Every visible fixture must contain `TEST`.
5. Configure Google OAuth only with isolated callback origins, or document a temporary isolated auth method. Production OAuth credentials are out of scope.
6. Store the public project URL/anon key outside Git. Store a paired test device session and selected room keys only through the DPAPI vault.
7. Add an authenticated `aura_test_environment()` marker returning a random environment id and literal `AURA_ISOLATED_TEST`. The native worker must verify it before any room/message mutation.

## Required RLS tests

- device A cannot list, fetch, subscribe to, overview, or send to device B's room;
- account membership without a provisioned key yields locked metadata and no plaintext;
- key possession without membership/grant yields no rows and no sends;
- revoked/expired grants fail REST, RPC and Realtime and trigger local key erasure;
- duplicate `client_msg_id` reconciles rather than inserts a second message;
- anonymous and Google owner paths are tested separately;
- service-role keys never appear in browser/device configuration.

## Local tooling result

`supabase` and `docker` commands are unavailable on this host. Node is available and is used only for the offline WebCrypto oracle. No network request was sent to the configured remote backend.

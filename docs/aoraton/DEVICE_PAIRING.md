# Device pairing

The audited v2 browser flow places `room`, opaque `key` and base64url RK in the invite URL fragment. The server validates an RK-derived verifier before the browser persists a link-provided key.

Proposed device flow:

1. User explicitly chooses **Pair device** in a trusted Aoraton browser session.
2. Browser displays a short-lived QR containing the existing invite material plus version/expiry metadata.
3. AURA validates framing and expiry, derives the verifier and joins only the isolated test project during development.
4. Persist RK only after server verification; store it encrypted in internal flash, never plaintext on microSD.
5. Unpair wipes the encrypted record, derived-key caches and session tokens.

This is a design, not an implemented protocol. Replay protection/expiry is not present in the current permanent room invite format and therefore needs a backward-compatible Aoraton change proposal before production device pairing.

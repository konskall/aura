# Aoraton E2EE v2 interoperability test report

Updated: 2026-10-21

## Result

Codec compatibility is `TEST VERIFIED` offline. Live Aoraton/browser messaging is `BLOCKED` because no isolated backend/device pairing grant exists.

The synthetic fixture uses RK `00..1f`, IV `a0..ab`, and UTF-8 text containing Greek, emoji, newline and a decomposed combining accent. Node WebCrypto and Windows CNG independently produce the exact wire:

`v2:oKGio6Slpqeoqaqr:88QR8avMSZBFI1Q9FKfJPRK1uILPjmc1reJIqJwRxRnHrPg52Jyb8axi5UAP8Jz4byJFW00W`

This is synthetic test material and grants no access to any Aoraton room.

## Verified contract

- AES-256-GCM; fresh 12-byte IV; no AAD; 16-byte tag appended to ciphertext.
- HKDF-SHA256 with 32 zero salt bytes and message info `inco/e2ee/v2/msg`.
- Auth and safety contexts match browser outputs.
- Unpadded base64url framing `v2:<iv>:<ciphertext+tag>`.
- PIN verifier matches lowercase/trim behavior for the source's ASCII room-name rule.
- Greek, emoji, multiline and combining text round-trip exactly.
- Altered tag, wrong key/version/IV/base64, truncation, invalid UTF-8 and oversize inputs are rejected.
- Two production CNG encryptions of the same plaintext use different IVs.

## Reproducible checks

- `node tests/interop/aoraton_webcrypto_oracle.mjs`
- `build/native/aura_aoraton_crypto_tests.exe`
- `build/native/aura_aoraton_real_tests.exe`
- `build/native/aura_aoraton_fail_closed_tests.exe`
- `build/native/aura_aoraton_vault_tests.exe`
- Fixture: `tests/fixtures/aoraton_e2ee_v2_vectors.json`

The Node oracle is browser-compatible WebCrypto, but it is not proof that the unmodified Aoraton web UI exchanged a message with AURA. That requires the missing isolated backend and browser pairing patch.

# E2EE compatibility plan

Compatibility means byte-for-byte agreement with Aoraton v2; it does not mean selecting a similar cipher.

Required independent implementation:

1. base64url without padding;
2. 32-byte RK and opaque 18-byte room ID;
3. HKDF-SHA256 with the audited zero salt and exact domain labels;
4. AES-256-GCM with 12-byte random IV and the existing `v2:` framing;
5. RK/PIN verifier rules and invite-fragment decoding;
6. strict authenticated-decryption failure.

Before any real connection, add synthetic browser→AURA and AURA→browser known-answer vectors for Unicode, wrong key, modified tag, malformed IV and maximum message size. Keys and plaintext fixtures must be synthetic. mbedTLS is the preferred ESP-IDF primitive provider; no custom cryptography will be written.

## v11 implementation result

Windows CNG and Node WebCrypto now pass a byte-for-byte synthetic known-answer vector in both directions. Tests cover Greek, emoji, newline, a combining accent, maximum 1500-byte plaintext, CSPRNG IV uniqueness, wrong key, changed tag, unknown version, malformed/non-canonical base64url, wrong IV length, truncation and invalid UTF-8. The 1500-byte bound produces a v2 envelope within the audited 2048-character database guard.

The pure framing/base64/UTF-8 contract is in `shared/core/src/aoraton_crypto.cpp`; Windows primitives are in `simulator/app/src/windows_aoraton_crypto.cpp`; ESP-IDF mbedTLS source is in `firmware/components/aura_aoraton`. The browser oracle and fixture are under `tests/interop` and `tests/fixtures`.

This verifies the codec, not a live room. Browser application ↔ AURA delivery remains blocked until a separate test backend and device pairing grant exist.

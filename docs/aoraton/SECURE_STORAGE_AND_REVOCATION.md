# Secure storage and revocation

Updated: 2026-10-21

## Windows simulator — TEST VERIFIED

`WindowsAoratonVault` encrypts the complete session/key bundle with Windows DPAPI, current-user scope and `CRYPTPROTECT_UI_FORBIDDEN`. It uses a versioned bounded binary payload, atomic temporary-file replacement and write-through, and zeroizes temporary plaintext, tokens and room-key buffers. The raw file was verified not to contain synthetic access token, refresh token or room id.

The vault stores at most two 16 KiB tokens and 64 room records. Load rejects wrong magic, inconsistent lengths, trailing data and oversized fields. Rotation writes a complete new DPAPI blob before replacing the old file. Unpair deletes the vault; state-machine revocation immediately removes rooms, messages, previews, notifications and pending plaintext from the UI snapshot.

DPAPI protects against other Windows users and offline file disclosure. It does not protect a fully compromised process running as the same user. Production diagnostics must never print vault contents or crypto errors with inputs.

## ESP32 — HARDWARE UNTESTED / storage BLOCKED

The mbedTLS codec is present, but an encrypted NVS vault is not yet implemented. Required design:

- ESP-IDF NVS encryption/flash-encryption + Secure Boot for production units;
- versioned dual-slot record with monotonic generation and CRC/authenticated wrapper;
- tokens and RKs in an Aoraton-only component, never generic settings/events;
- zeroization after decrypt/use and on unpair/revocation;
- rollback-safe refresh-token rotation;
- documented factory reset and physical possession threat model.

No plaintext key/token persistence may be enabled before that design builds under ESP-IDF 5.5.5 and is tested on hardware.

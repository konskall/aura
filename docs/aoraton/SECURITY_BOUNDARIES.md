# Aoraton security boundaries

- No production Aoraton endpoint is contacted by the simulator.
- Mock fixtures contain no real people, room identifiers or conversations.
- Room keys, PINs, invite fragments and decrypted history never enter Codex prompts or telemetry.
- Message text is not logged by AURA services.
- A pending send contains at most 512 UTF-8 bytes and requires local confirmation.
- Invalid authenticated payloads update a diagnostic code but never create messages.
- The loopback Codex service is not reachable from Aoraton events.
- Future remote TTS must require explicit consent before receiving private text.

Threats still requiring hardware/service work include stolen-device key extraction, secure boot/flash encryption policy, session revocation, replay-resistant pairing, SD corruption and rollback-safe OTA.

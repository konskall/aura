# Aoraton calls feasibility

Aoraton currently uses small-group WebRTC mesh. Signaling travels on a private Supabase Realtime channel and is encrypted with the room cipher. Browser peers use SDP, ICE, STUN/TURN, DTLS/SRTP and media tracks.

This is not a Phase 1 ESP32 capability. Native compatibility would require an Espressif WebRTC stack, codec negotiation, TURN credentials, microphone/speaker coordination, echo control, large TLS/media buffers and measured CPU/battery behavior. No call UI or working-call claim is included in AURA. The capability remains explicitly unsupported until a separate feasibility prototype passes interoperability tests.

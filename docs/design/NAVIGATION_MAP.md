# Navigation map

```text
Home / AI presence
├─ Tap presence or Ρώτησέ με → Wake → Listening
│  ├─ Ολοκλήρωση → Transcribing (awaits actual input/provider event)
│  ├─ Ακύρωση → Home
│  ├─ Thinking → Answer / Speaking
│  └─ Offline / Error → Retry or Home
├─ Recent notification → Notifications → Aoraton rooms
└─ Εργαλεία → Tools launcher
   ├─ Aoraton → Rooms → Conversation → Compose → Confirm → Conversation
   ├─ Audio → playback controls
   ├─ Cards / search result
   ├─ Notifications
   └─ Settings → Audio / Diagnostics / provider / reduced motion
```

Every non-Home product screen has a visible Back action. Tools replace the legacy permanent four-button footer. Notification count is attached only to the notification entry. Settings and diagnostics are reachable without occupying permanent screen area.

Assistant state transitions remain owned by `AssistantController`; visual mapping does not invent a second state machine. Aoraton send still requires service `prepare` followed by explicit `confirm`.

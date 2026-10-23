# Messaging workflows

Simulator receive:

```text
developer fixture → validate/bound → MockAoratonService → unread + notification → room view
```

Simulator send:

```text
compose → PREPARED → exact destination/text preview → explicit confirm → SENT (DEMO)
```

Cancel invalidates the pending action. A second prepare is rejected while confirmation is pending. Offline confirmation fails without claiming delivery and without duplicating the draft. Incoming payloads are displayed as untrusted text and never interpreted as device commands.

The future real path adds local AES-GCM before transport, `client_msg_id` idempotency and server-echo reconciliation. It must not auto-retry a non-idempotent operation.

# Windows simulator setup

```powershell
cd C:\Users\polis\Documents\AiCube
.\scripts\setup-windows.ps1   # first run only
.\scripts\dev-simulator.ps1
```

Use `-Provider codex` for the authenticated host-only Codex bridge. Home now opens **Ήχος** and **Aoraton**. The browser panel injects bounded audio/Aoraton development events. It binds to loopback and requires the per-run bearer token.

No Aoraton production credential or endpoint is needed. Closing the simulator tears down helper processes created by the launcher.

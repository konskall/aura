# Product specification

AURA is a premium, configurable AI companion with an obsidian AMOLED visual language, cyan/teal and violet accents, Greek-first copy, clear privacy states, and a lightweight animated entity.

Phase 1 acceptance is desktop-first: the native 480 × 480 LVGL screen must launch without hardware, accept mouse interaction, render deterministic mock conversations, and expose a separate developer control surface. Live Codex mode is optional at runtime and must fail back honestly. The firmware target must compile once ESP-IDF is installed, but physical capabilities remain unverified.

Core views are boot/onboarding, home, listening/thinking/answer, information cards, notifications, settings, diagnostics, and integrated offline/error recovery. Persistent UI elements move subtly and idle refresh is reduced to mitigate AMOLED burn-in.


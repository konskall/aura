# AURA v2 roadmap

## v10 production UI status

- V10-M0 preserve/audit: complete; source checkpoint and before captures recorded.
- V10-M1 typography: complete in shared LVGL; native Greek proof and glyph tests pass.
- V10-M2 semantic icons: complete with lawful Lucide ISC fallback; Lordicon PRO/export substitution remains optional and requires eligible user-supplied assets.
- V10-M3/M4 Mercury: complete in shared LVGL for all four assistant states; simulator motion/performance evidence passes.
- V10-M5 all-screen polish and routes: complete in the simulator; 18-screen and 160-check pointer evidence passes.
- V10-M6 regression: native suite is automated; ESP-IDF 5.5.5 build and all physical hardware measurements remain pending on the required environment/board.

- V2-M0 audit: complete.
- V2-M1 audio core/Windows backend/tests: implemented and build-tested.
- V2-M2 audio LVGL/PCM-reactive UI: implemented and visually inspected.
- V2-M3 ES8311 backend: implemented against BSP 2.0.1; firmware build and hardware tests pending.
- V2-M4 Aoraton client foundation: implemented with isolated mock backend.
- V2-M5 interactive mock messaging: implemented; real typing, confirmation, unread and reconnection simulation included.
- V2-M6 real interoperability: blocked on isolated Supabase test environment, schema/RLS export, pairing decision and cross-client crypto fixtures.
- V2-M7 AI messaging tools: planned after real privacy boundary tests; no received content will become an instruction.
- V2-M8 profiling/power: pending board.

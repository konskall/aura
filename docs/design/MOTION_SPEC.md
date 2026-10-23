# Motion specification

- Press feedback: immediate, 2 px inset transform; target duration 90–140 ms where LVGL animation is used.
- Content/state transition target: 180–240 ms. Current screen reconstruction is immediate and interruptible; presence continuation supplies continuity without blocking the UI loop.
- Idle: slow 3.2 s breathing cycle, local presence region only, capped near 12 Hz updates.
- Wake: brighter expanded geometry; transition acknowledgment is immediate.
- Listening: open twin-lobe geometry with measured/simulated-input honesty in copy.
- Thinking: compact inward geometry with slow internal light circulation, not a spinner.
- Speaking: geometry and meter react only to `AudioDiagnostics::envelope`; no random waveform substitutes for PCM.
- Reduced motion: fixed geometry and light opacity; audio state remains textually and spatially clear.
- Low battery / sleep: idle motion stops. Firmware performance and power require board measurement.

Animation updates change object size/position/opacity only and do not recreate layouts per frame. UI updates remain on the LVGL owner thread.

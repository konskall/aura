# ADR 0011: «Αύρα» on the device

Status: accepted, 2026-11-20, owner-requested («Λέξη ενεργοποίησης «Αύρα», για
να ξεκινάει χωρίς άγγιγμα», 2026-11-19).

The wake word runs on the ESP32-S3 itself: microWakeWord's streaming model,
trained by the owner in Google Colab with `tools/wakeword/aura_avra_microwakeword.ipynb`
(results in `tools/wakeword/README.md`). Nothing leaves the device until «Αύρα»
is heard; then the question goes to the cloud assistant exactly as after a tap
(ADR 0009).

## Decision

- **Engine:** `firmware/components/aura_wakeword` (`Esp32WakeWord`). TensorFlow
  Lite Micro (`espressif/esp-tflite-micro` 1.4.3, Apache-2.0, with ESP-NN) runs
  the model; microWakeWord's own features (`esphome/esp-micro-speech-features`
  1.2.3, Apache-2.0, TensorFlow's microfrontend) are computed with the settings
  the training used (pymicro-features: 30 ms windows every 10 ms, 40 channels,
  125-7500 Hz, noise reduction, PCAN, log scale). Both are pinned in the
  component's `idf_component.yml`.
- **Counting words:** `aura::WakeWordDetector` (shared, unit-tested) does what
  microWakeWord's final test did, so its table of cutoffs holds on the device:
  probability = output / 255, the mean of the last five outputs over the cutoff,
  75 means of cooldown at the start and after each word; input features are
  scaled by 0.0390625 and quantized with the model's own scale and zero point,
  cut toward zero. Sensitivity: Low 0.99, Normal 0.98, High 0.75.
  - High was 0.96 until 2026-11-21. The owner's tries from a distance came to
    0.58–0.85, and the owner chose a High that wakes for them, knowing it wakes
    more often by mistake.
- **Microphone:** the companion opens it as for a question (the voice's gain)
  while app_main allows it: no turn under way, nothing listening or speaking, and
  no sound playing (the radio, an answer, an alarm). Notes, voice messages and
  the barge-in monitor take it first. After «Αύρα» the question goes on from the
  open microphone, with no ES7210 settling, as after a barge-in.
- **Opt-in:** off by default (`wake-word.txt`, Ήχος > Λέξη αφύπνισης): an open
  microphone, and its battery. The row exists only with a model built in and
  loaded (`CompanionCapabilities::wake_word`).
- **Memory:** the model is built into the firmware (62 KB); the arena (48 KB),
  the features' tables, a one-second buffer and the task's stack are in PSRAM;
  internal RAM is left to the panel's transfers.
- **The model file:** `firmware/components/aura_wakeword/model/avra.tflite`, with
  its manifest `avra.json` beside it. The build embeds it when it exists and
  builds without a wake word otherwise.
  - The model is in the repository by the owner's choice (2026-11-21), so CI
    builds the wake word too.
  - Its training audio allows personal, non-commercial use only, so the model,
    and firmware built with it, are for non-commercial use.

## Privacy

What the microphone hears while listening for «Αύρα» is processed in RAM, a
second at most waiting, and never stored, logged or sent. The log carries numbers
only (inferences, the highest window mean, the slowest inference, drops). After
«Αύρα» the question follows ADR 0009.

## Not decided here

Listening over the radio or an answer (with the ES7210's speaker reference, as
barge-in does), light sleep while listening, and a second model trained with the
owner's own recordings.

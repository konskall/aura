# «Αύρα» wake word (microWakeWord)

`aura_avra_microwakeword.ipynb` trains the «Αύρα» wake word for AURA's ESP32-S3
with [microWakeWord](https://github.com/kahrendt/microWakeWord) (Apache-2.0). It
is microWakeWord's own `basic_training_notebook.ipynb` with AURA's changes:

- the target word «Αύρα» ([ˈavra]) from six English spellings, each with the
  904-speaker LibriTTS voice of piper-sample-generator;
- optionally, real Greek from Azure's Greek and multilingual neural voices, when
  the Colab secrets `AZURE_SPEECH_KEY` and `AZURE_SPEECH_REGION` are set (about
  four characters a sample, within Azure's free tier);
- 20,000 training steps instead of 10,000;
- a last cell that downloads `avra.tflite` and `avra.json` (ESPHome's model
  manifest form, probability cutoff 0.97, sliding window 5).

## Running it

1. Open the notebook in Google Colab; Runtime → Change runtime type → T4 GPU.
2. Optional: add the two Azure secrets (🔑 on the left) with notebook access.
3. Runtime → Run all; after the first cell, Restart session and Run all again.
4. About 1-2 hours on a T4 and ~6 GB of downloads (Colab's disk). Keep the two
   downloaded files for the firmware.

## Before AURA listens for it

The model is not in the firmware yet. The detector still has to be built: the
microphone's 16 kHz stream through the micro_speech features into the streaming
model, on the device, with the false accepts per hour and the battery cost
measured. `docs/TASKS.md` keeps that task open, and the Settings row for the
wake word stays hidden until a model is on the device (`capabilities.wake_word`).

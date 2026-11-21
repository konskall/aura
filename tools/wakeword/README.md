# «Αύρα» wake word (microWakeWord)

`aura_avra_microwakeword.ipynb` trains the «Αύρα» wake word for AURA's ESP32-S3
with [microWakeWord](https://github.com/kahrendt/microWakeWord) (Apache-2.0). It
is microWakeWord's own `basic_training_notebook.ipynb` with AURA's changes:

- the target word «Αύρα» ([ˈavra]) from six English spellings, each with the
  904-speaker LibriTTS voice of piper-sample-generator;
- optionally, real Greek from Azure's Greek and multilingual neural voices, when
  the Colab secrets `AZURE_SPEECH_KEY` and `AZURE_SPEECH_REGION` are set (about
  four characters a sample, within Azure's free tier);
- 10,000 training steps as upstream, validated every 1,000 instead of 500: on
  free Colab (2026-11-20) the runtime may last under 3 hours;
- the validation's ambient set (dinner_party_eval, 4 recordings, 9.7 hours)
  cut to its first 4 hours by a cell before the training: microWakeWord makes
  it 116,000 float32 windows (3.8 GB) and copies them two or three times, more
  than free Colab's 12.7 GB RAM held, so the training was cut (^C) at its first
  validation three times. The full set moves to `negative_datasets_aside/`. The
  training cell runs alone after a session restart (it refuses with less than
  8 GB free);
- a last cell that downloads `avra.tflite` and `avra.json` (ESPHome's model
  manifest form, probability cutoff 0.97, sliding window 5);
- fixed for Colab's Python 3.13 (2026-11-20; microWakeWord's own notebook still
  fails there): piper-sample-generator 3.x runs as a package, pinned to commit
  2971426, with its phonemes from piper-tts 1.3.0 (piper-phonemize-cross has no
  3.13 build); datasets 4 decodes audio through torchcodec and gives no clip
  path, so the augmentation files are named from their own paths; the clips are
  trimmed of silence (`remove_silence=True`), which piper-sample-generator 2.x
  did itself; AudioSet's background noise comes from four parquet shards of
  the balanced set (its `.tar` files give 404 on Hugging Face).

## Running it

1. Open the notebook in Google Colab; Runtime → Change runtime type → T4 GPU.
2. Optional: add the two Azure secrets (🔑 on the left) with notebook access;
   the value is only what follows `=` in `.env`.
3. Runtime → Run all. After the install, the second cell stops and asks for a
   restart (microWakeWord's editable install is seen only after one): Runtime →
   Restart session, then Runtime → Run all again; the install is skipped.
4. About 1-2 hours on a T4 and ~6 GB of downloads (Colab's disk). Keep the two
   downloaded files for the firmware.

## The first model (2026-11-20)

Trained by the owner on free Colab (T4): 9,000 piper samples of six spellings,
150 Azure Greek and multilingual samples, AudioSet and FMA noise, 10,000 steps.
`avra.tflite` is 62,304 bytes: streaming and quantized, input `[1, 3, 40]` int8
(three 10 ms feature windows, one inference every 30 ms), output `[1, 1]` uint8;
its operators (CONV_2D, DEPTHWISE_CONV_2D, FULLY_CONNECTED, CONCATENATION,
SPLIT_V, STRIDED_SLICE, RESHAPE, LOGISTIC, QUANTIZE and the variable ops
CALL_ONCE, VAR_HANDLE, READ_VARIABLE, ASSIGN_VARIABLE) are the ones ESPHome's
v2 models use with TensorFlow Lite Micro.

Validation (nonstreaming, 4 hours of dinner-party audio): best at step 6,000,
95.0 % of the validation samples caught with no false accept at cutoff 0.94.
microWakeWord's final test of the quantized streaming model (5.3 hours of
another dinner party, the synthetic test samples):

| Cutoff | Missed (frr) | False accepts an hour |
|---|---|---|
| 0.99 | 10.3 % | 0.56 |
| 0.98 | 8.7 % | 0.75 |
| 0.97 | 7.6 % | 1.13 |
| 0.96 | 7.2 % | 1.50 |
| 0.95 | 6.8 % | 1.88 |

The test samples are synthetic voices: how it hears the owner is measured on the
device. The training audio allows personal, non-commercial use only, so whether
the model goes into the public repository is the owner's decision.

## On the device (ADR 0011)

`firmware/components/aura_wakeword` runs it: the model file goes to
`firmware/components/aura_wakeword/model/avra.tflite` (the build embeds it when
it is there and builds without a wake word otherwise). The features use the
training's settings, and `aura::WakeWordDetector` counts words as the final test
above did, so its cutoffs hold. Ήχος > Λέξη αφύπνισης offers Low (0.99), Normal
(0.98) and High (0.75; 0.96 until 2026-11-21). It is off until the owner turns it
on. False accepts in the owner's home and the battery's cost are measured on the
device.

The device's log says how it hears, in numbers only:

- `wake word: heard (window peak …)` for each «Αύρα».
- `wake word: almost (window peak …, wakes over …)` for a try that rose over
  0.5 and fell back without waking.
- Once a minute while it listens: the inferences, the minute's highest mean,
  the slowest inference and any samples dropped.

The first tries on the board (2026-11-20) woke 7 times at 0.98–1.00 from close
by, but from a metre the owner had to shout. The microphone listened at 24 dB
then, and a voice a metre away came in about 20 dB under the training's typical
level. Listening is at 33 dB since. The `almost` lines show how near the tries
from a distance come, which tells whether the gain or the cutoffs need to move.

At 33 dB (2026-11-21):

- The tries from a distance still woke nothing. Their `almost` means were 0.58,
  0.76, 0.78, 0.84 and 0.85.
- The microphones were not the cause. The quiet room measured an rms of about
  16 per tenth of a second, and the owner's questions 400–900. Both microphones
  heard alike, and 15 quiet minutes peaked at 0.04.
- A model trained on synthetic voices alone is sure of the owner's voice close
  by, and less sure of it across a room.

The owner chose two steps:

1. High at 0.75, which would have woken for four of those five tries. The test
   above measured cutoffs down to 0.95 only (about 2 false wakes an hour on the
   dinner party), so High's false wakes are counted in the owner's home.
2. A second training with the owner's own «Αύρα», recorded on the device at
   1–3 m.

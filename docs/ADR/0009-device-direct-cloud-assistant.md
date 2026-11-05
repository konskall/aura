# ADR 0009: The device's assistant calls Groq and Azure directly

Status: accepted, 2026-11-05 (the owner's choice). It replaces the device part
of ADR 0003 (an authenticated gateway with a device-scoped credential) and the
device note in ADR 0005. The PC bridge is unchanged.

## Decision

The ESP32 runs the spoken assistant without the PC and without a hosted
gateway:

- **Groq** recognises speech with `whisper-large-v3-turbo` and answers with `openai/gpt-oss-120b`. The answer is streamed, with temperature 0.2 and at most 1,200 tokens.
- **Azure Speech** speaks with the `el-GR` Athina and Nestoras neural voices, as 24 kHz mono MP3 that the shared `minimp3` path decodes to PCM.

The requests, limits and error codes are the PC bridge's (`bridge/src/groq-runner.mjs`, `bridge/src/speech.mjs`). The prompt is built by `shared/core/src/turn_prompt.cpp`, and `tests/interop/run_turn_prompt_parity.mjs` keeps it identical to the bridge's `turn-prompt.mjs`.

The keys reach the device once:

1. The owner opens **File transfer** on the device.
2. The owner runs `scripts/send-device-keys.ps1 -Address <shown address> -Code <shown code>`. It reads `GROQ_API_KEY`, `AZURE_SPEECH_KEY` and `AZURE_SPEECH_REGION` from the ignored `.env`.
3. The script signs in with the six-digit code and posts the keys to `POST /api/keys`, inside that code-protected session.
4. The device checks their format (`cloud_keys_valid`) and stores them in NVS under `aura_cloud`. That NVS is encrypted on every build:
   - development boards use the HMAC scheme with the key in eFuse block 5;
   - production uses flash encryption's `nvs_keys`.
5. The device restarts the assistant with the new keys.

The keys are never logged, shown, echoed in a response, or written to a file or the microSD card. They travel only in request headers, and copies are wiped after use.

## Rationale

The owner wants the assistant on the device itself: the PC need not be on, and there is no server to host. Groq already serves the simulator's speech recognition and its Groq answers, so one key covers both. Azure is already the simulator's voice.

## Consequences

- **The provider keys live on the device.**
  - On a development board (no secure boot), someone holding the board can flash their own firmware. That firmware can use the HMAC peripheral and read the keys.
  - The production profile closes that path with secure boot and release-mode flash encryption.
  - Use keys made only for the device, with spending limits.
  - If a device is lost, revoke both keys at Groq and at Azure.
- **The question's audio and text, the device facts the model needs and the answer go to Groq and Azure.**
  - The device facts are the time, battery, weather and timers. Notes and phone notifications are included only when the question asks for them.
  - Nothing else leaves the device.
- **The keys cross the local network once.** The transfer server is plain HTTP, so it relies on a trusted network, as it already does for files.
- **An action the model proposes is not carried out on the device yet.** The answer says that nothing was done. Confirming actions on the device is the next step.
- **ADR 0003's gateway remains possible later.** For example, it would allow revocable device credentials without provider keys on the device. Moving to it changes only the platform side of `IHttpsClient` and the key store.

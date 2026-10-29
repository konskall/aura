import { spawn } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { existsSync } from 'node:fs';
import { mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

// Local speech for the simulator: whisper.cpp recognises, the installed
// Windows (OneCore) voices speak. Audio and text stay on this PC, live only in
// short-lived files under bridge/runtime/speech, and are never logged.

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, '..', '..');

export const SPEECH_LANGUAGES = Object.freeze({
  el: 'el-GR', en: 'en-US', de: 'de-DE', fr: 'fr-FR', it: 'it-IT', es: 'es-ES',
});
export const speechLimits = Object.freeze({
  MAX_WAV_BYTES: 1_100_000,         // 34 s of 16 kHz mono PCM plus header: a 30 s note fits
  MIN_WAV_SAMPLES: 4800,            // 0.3 s
  MAX_TTS_TEXT_BYTES: 2000,
  MAX_TTS_WAV_BYTES: 8 * 1024 * 1024,
});

export const NEURAL_VOICE_PROFILES = Object.freeze({
  athina: Object.freeze({
    el: 'el-GR-AthinaNeural', en: 'en-US-AvaMultilingualNeural', de: 'de-DE-KatjaNeural',
    fr: 'fr-FR-DeniseNeural', it: 'it-IT-ElsaNeural', es: 'es-ES-ElviraNeural',
  }),
  nestoras: Object.freeze({
    el: 'el-GR-NestorasNeural', en: 'en-US-AndrewMultilingualNeural', de: 'de-DE-ConradNeural',
    fr: 'fr-FR-HenriNeural', it: 'it-IT-DiegoNeural', es: 'es-ES-AlvaroNeural',
  }),
});

const bytes = (value) => Buffer.byteLength(value, 'utf8');

// large-v3-turbo gets Greek right where small does not ("αύριο", not "άυρω"),
// so it is used whenever setup-voice.ps1 installed it.
export const WHISPER_MODELS = Object.freeze(['ggml-large-v3-turbo-q5_0.bin', 'ggml-small-q5_1.bin']);
const GROQ_TRANSCRIPTION_ENDPOINT = 'https://api.groq.com/openai/v1/audio/transcriptions';
const GROQ_TRANSCRIPTION_MODEL = 'whisper-large-v3-turbo';
const AZURE_TTS_PATH = '/cognitiveservices/v1';
const AZURE_TTS_OUTPUT_FORMAT = 'audio-24khz-48kbitrate-mono-mp3';

function azureSpeechEndpoint({ region = '', endpoint = '' } = {}) {
  const trimmedEndpoint = String(endpoint).trim();
  if (!trimmedEndpoint) {
    const trimmedRegion = String(region).trim().toLowerCase();
    if (!/^[a-z0-9-]{2,40}$/.test(trimmedRegion)) return null;
    return `https://${trimmedRegion}.tts.speech.microsoft.com${AZURE_TTS_PATH}`;
  }
  try {
    const url = new URL(trimmedEndpoint);
    const hostAllowed = url.hostname.endsWith('.tts.speech.microsoft.com') ||
      url.hostname.endsWith('.cognitiveservices.azure.com');
    if (url.protocol !== 'https:' || url.port || url.username || url.password || url.search || url.hash || !hostAllowed) return null;
    const pathName = url.pathname.replace(/\/+$/, '');
    if (pathName !== '' && pathName !== AZURE_TTS_PATH) return null;
    url.pathname = AZURE_TTS_PATH;
    return url.toString();
  } catch {
    return null;
  }
}

function xmlEscape(value) {
  return value.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;').replaceAll("'", '&apos;');
}

function validMp3(audio) {
  if (!Buffer.isBuffer(audio) || audio.length < 128 || audio.length > speechLimits.MAX_TTS_WAV_BYTES) return false;
  const hasId3 = audio.toString('ascii', 0, 3) === 'ID3';
  const hasFrameSync = audio[0] === 0xFF && (audio[1] & 0xE0) === 0xE0;
  return hasId3 || hasFrameSync;
}

async function boundedAudio(response) {
  const advertised = Number(response.headers?.get?.('content-length') ?? 0);
  if (advertised > speechLimits.MAX_TTS_WAV_BYTES) return null;
  if (!response.body || typeof response.body[Symbol.asyncIterator] !== 'function') {
    const value = Buffer.from(await response.arrayBuffer());
    return value.length <= speechLimits.MAX_TTS_WAV_BYTES ? value : null;
  }
  const chunks = [];
  let total = 0;
  for await (const chunk of response.body) {
    const value = Buffer.from(chunk);
    total += value.length;
    if (total > speechLimits.MAX_TTS_WAV_BYTES) {
      await response.body.cancel?.().catch(() => {});
      return null;
    }
    chunks.push(value);
  }
  return Buffer.concat(chunks, total);
}

// whisper's encoder cost grows with its audio context (1500 = 30 s). A
// context sized to the clip plus a few seconds of margin keeps the full
// accuracy at a fraction of the time; under ~9 s of context turbo starts to
// mishear, so that is the floor, and the whole window (1500) covers the
// longest recording, a 30 s dictated note.
export function audioContextFor(sampleCount) {
  const seconds = Math.max(0, sampleCount) / 16000;
  const needed = Math.ceil(((seconds + 3.5) * 50) / 64) * 64;
  return Math.min(1500, Math.max(448, needed));
}

function boundedEnvironment(source = process.env) {
  const names = ['PATH', 'Path', 'PATHEXT', 'SystemRoot', 'WINDIR', 'TEMP', 'TMP', 'USERPROFILE', 'LOCALAPPDATA',
    'APPDATA', 'PSModulePath'];
  const result = {};
  for (const name of names) if (source[name]) result[name] = source[name];
  return result;
}

// The recorder sends RIFF/WAVE, PCM 16-bit, mono, 16 kHz; nothing else is passed on.
export function inspectWav(buffer) {
  if (!Buffer.isBuffer(buffer) || buffer.length < 44 || buffer.length > speechLimits.MAX_WAV_BYTES ||
      buffer.toString('ascii', 0, 4) !== 'RIFF' || buffer.toString('ascii', 8, 12) !== 'WAVE') {
    return { ok: false, code: 'INVALID_AUDIO' };
  }
  let offset = 12;
  let format = null;
  while (offset + 8 <= buffer.length) {
    const id = buffer.toString('ascii', offset, offset + 4);
    const size = buffer.readUInt32LE(offset + 4);
    const body = offset + 8;
    if (size > buffer.length - body) return { ok: false, code: 'INVALID_AUDIO' };
    if (id === 'fmt ' && size >= 16) {
      format = {
        audioFormat: buffer.readUInt16LE(body), channels: buffer.readUInt16LE(body + 2),
        rate: buffer.readUInt32LE(body + 4), bits: buffer.readUInt16LE(body + 14),
      };
    } else if (id === 'data') {
      if (!format || format.audioFormat !== 1 || format.channels !== 1 || format.rate !== 16000 || format.bits !== 16) {
        return { ok: false, code: 'UNSUPPORTED_AUDIO' };
      }
      if (size / 2 < speechLimits.MIN_WAV_SAMPLES) return { ok: false, code: 'AUDIO_TOO_SHORT' };
      return { ok: true, samples: size / 2 };
    }
    offset = body + size + (size & 1);
  }
  return { ok: false, code: 'INVALID_AUDIO' };
}

// whisper prints the transcript, sometimes with markers for silence or music.
export function cleanTranscript(stdout) {
  return String(stdout)
    .replace(/\[[^\]]{0,40}\]|\([^)]{0,40}\)|\*[^*]{0,40}\*/g, ' ')
    .replace(/[\u0000-\u001F\u007F]/g, ' ')
    .replace(/\s+/g, ' ')
    .trim()
    .slice(0, 600);
}

function killTree(child) {
  if (!child?.pid || child.exitCode !== null) return;
  if (process.platform === 'win32') {
    spawn('taskkill', ['/pid', String(child.pid), '/t', '/f'], { shell: false, windowsHide: true, stdio: 'ignore' })
      .unref();
  } else {
    child.kill('SIGTERM');
  }
}

function run(spawnImpl, command, args, {
  timeoutMs, signal, binary = false, maxStdoutBytes = 65536, env = boundedEnvironment(),
}) {
  return new Promise((resolve) => {
    let child;
    try {
      child = spawnImpl(command, args, {
        env, shell: false, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'],
      });
    } catch {
      resolve({ code: -1, stdout: '', missing: true });
      return;
    }
    const chunks = [];
    let stdoutBytes = 0;
    let settled = false;
    const finish = (value) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      signal?.removeEventListener('abort', abort);
      resolve(value);
    };
    const abort = () => { killTree(child); finish({ code: -1, stdout: '', aborted: true }); };
    const timer = setTimeout(() => { killTree(child); finish({ code: -1, stdout: '', timedOut: true }); }, timeoutMs);
    signal?.addEventListener('abort', abort, { once: true });
    child.once('error', (error) => finish({ code: -1, stdout: '', missing: error?.code === 'ENOENT' }));
    child.stdout.on('data', (chunk) => {
      if (settled) return;
      const data = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk);
      if (stdoutBytes + data.length > maxStdoutBytes) {
        killTree(child);
        finish({ code: -1, stdout: binary ? Buffer.alloc(0) : '', overflow: true });
        return;
      }
      chunks.push(data);
      stdoutBytes += data.length;
    });
    child.stderr.on('data', () => {});
    child.once('close', (code) => {
      const stdout = Buffer.concat(chunks, stdoutBytes);
      finish({ code, stdout: binary ? stdout : stdout.toString('utf8') });
    });
    if (signal?.aborted) abort();
  });
}

export function createSpeechEngines(options = {}) {
  const spawnImpl = options.spawnImpl ?? spawn;
  const exists = options.exists ?? existsSync;
  const whisper = options.whisperPath ?? process.env.AURA_WHISPER_CLI ??
    path.join(repoRoot, '.tools', 'whisper', 'bin', 'whisper-cli.exe');
  const models = path.join(repoRoot, '.tools', 'whisper', 'models');
  const model = options.modelPath ?? process.env.AURA_WHISPER_MODEL ??
    WHISPER_MODELS.map((name) => path.join(models, name)).find((candidate) => exists(candidate)) ??
    path.join(models, WHISPER_MODELS.at(-1));
  const powershell = options.powershell ?? 'powershell.exe';
  const python = options.python ?? process.env.AURA_PYTHON ?? 'python';
  const scripts = options.scriptsDir ?? path.join(here, '..', 'scripts');
  const workdir = options.workdir ?? path.join(repoRoot, 'bridge', 'runtime', 'speech');
  const sttProvider = options.sttProvider ?? process.env.AURA_STT_PROVIDER ?? 'local';
  const ttsProvider = options.ttsProvider ?? process.env.AURA_TTS_PROVIDER ?? 'windows';
  const neuralTool = options.neuralTool ?? path.join(scripts, 'neural_tts.py');
  const neuralPythonPath = options.neuralPythonPath ?? process.env.AURA_EDGE_TTS_PYTHONPATH ??
    path.join(repoRoot, '.tools', 'edge-tts');
  const groqApiKey = options.groqApiKey ?? process.env.GROQ_API_KEY ?? '';
  const azureSpeechKey = options.azureSpeechKey ?? process.env.AZURE_SPEECH_KEY ?? '';
  const azureEndpoint = azureSpeechEndpoint({
    region: options.azureSpeechRegion ?? process.env.AZURE_SPEECH_REGION,
    endpoint: options.azureSpeechEndpoint ?? process.env.AZURE_SPEECH_ENDPOINT,
  });
  const fetchImpl = options.fetchImpl ?? fetch;
  // Physical cores matter more than hyper-threads for the encoder.
  const threads = String(Math.max(1, Math.min(4, os.cpus().length)));
  const psArgs = (script, extra = []) =>
    ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', path.join(scripts, script), ...extra];
  let voices = null;
  let voicesAt = 0;

  const localSttAvailable = () => exists(whisper) && exists(model);
  const sttAvailable = () => sttProvider === 'groq' ? Boolean(groqApiKey) : localSttAvailable();
  const neuralTtsAvailable = () => exists(neuralTool) &&
    exists(path.join(neuralPythonPath, 'edge_tts', '__init__.py'));

  async function ttsTags() {
    if (voices && Date.now() - voicesAt < 60000) return voices;
    const result = await run(spawnImpl, powershell, psArgs('voices.ps1'), { timeoutMs: 15000 });
    voices = new Set(result.code === 0 ?
      result.stdout.split(/\r?\n/).map((line) => line.trim()).filter((line) => /^[a-z]{2}-[A-Z]{2}$/.test(line)) : []);
    voicesAt = Date.now();
    return voices;
  }

  async function health() {
    const neural = ttsProvider === 'edge-neural';
    const azure = ttsProvider === 'azure';
    const tags = neural || azure ? null : await ttsTags();
    const available = neural ? neuralTtsAvailable() : azure ? Boolean(azureSpeechKey && azureEndpoint) : false;
    const result = { stt: sttAvailable(), stt_provider: sttProvider,
      tts_provider: azure ? 'azure' : neural ? 'edge-neural' : 'windows',
      tts_neural: neural || azure };
    for (const [code, tag] of Object.entries(SPEECH_LANGUAGES)) {
      result[`tts_${code}`] = neural || azure ? available : tags.has(tag);
    }
    return result;
  }

  async function transcribe({ wav, lang, signal }) {
    if (lang !== 'auto' && !Object.hasOwn(SPEECH_LANGUAGES, lang)) return { ok: false, code: 'INVALID_LANGUAGE' };
    const check = inspectWav(wav);
    if (!check.ok) return check;
    if (!sttAvailable()) return { ok: false, code: 'STT_UNAVAILABLE' };
    if (sttProvider === 'groq') {
      const controller = new AbortController();
      const abort = () => controller.abort();
      signal?.addEventListener('abort', abort, { once: true });
      const timer = setTimeout(abort, 60000);
      try {
        const form = new FormData();
        form.append('file', new Blob([wav], { type: 'audio/wav' }), 'aura.wav');
        form.append('model', GROQ_TRANSCRIPTION_MODEL);
        form.append('response_format', 'json');
        form.append('temperature', '0');
        if (lang !== 'auto') form.append('language', lang);
        const response = await fetchImpl(GROQ_TRANSCRIPTION_ENDPOINT, {
          method: 'POST', headers: { authorization: `Bearer ${groqApiKey}` }, body: form, signal: controller.signal,
        });
        if (!response.ok) {
          if (response.status === 401 || response.status === 403) return { ok: false, code: 'PROVIDER_AUTH_FAILED' };
          if (response.status === 429) return { ok: false, code: 'RATE_LIMIT' };
          return { ok: false, code: response.status >= 500 ? 'STT_UNAVAILABLE' : 'STT_FAILED' };
        }
        const payload = await response.json().catch(() => null);
        const text = cleanTranscript(payload?.text ?? '');
        return text ? { ok: true, text } : { ok: false, code: 'NO_SPEECH' };
      } catch (error) {
        if (signal?.aborted) return { ok: false, code: 'CANCELLED' };
        return { ok: false, code: error?.name === 'AbortError' ? 'TIMEOUT' : 'STT_UNAVAILABLE' };
      } finally {
        clearTimeout(timer);
        signal?.removeEventListener('abort', abort);
      }
    }
    await mkdir(workdir, { recursive: true });
    const file = path.join(workdir, `stt-${randomBytes(8).toString('hex')}.wav`);
    try {
      await writeFile(file, wav);
      // Greedy decoding and an audio context sized to the clip keep a short
      // question at a few seconds on a desktop CPU.
      const result = await run(spawnImpl, whisper, ['-m', model, '-f', file, '-l', lang, '-t', threads, '-bs', '1',
        '-bo', '1', '-ac', String(audioContextFor(check.samples)), '-nt', '-np'], { timeoutMs: 60000, signal });
      if (result.aborted) return { ok: false, code: 'CANCELLED' };
      if (result.timedOut) return { ok: false, code: 'TIMEOUT' };
      if (result.missing) return { ok: false, code: 'STT_UNAVAILABLE' };
      if (result.code !== 0) return { ok: false, code: 'STT_FAILED' };
      const text = cleanTranscript(result.stdout);
      return text ? { ok: true, text } : { ok: false, code: 'NO_SPEECH' };
    } finally {
      await rm(file, { force: true });
    }
  }

  async function synthesize({ text, lang, voice = 'nestoras', signal }) {
    if (!Object.hasOwn(SPEECH_LANGUAGES, lang)) return { ok: false, code: 'INVALID_LANGUAGE' };
    if (typeof text !== 'string') return { ok: false, code: 'INVALID_REQUEST' };
    const clean = text.replace(/[\u0000-\u001F\u007F]/g, ' ').replace(/\s+/g, ' ').trim();
    if (!clean) return { ok: false, code: 'INVALID_REQUEST' };
    if (bytes(clean) > speechLimits.MAX_TTS_TEXT_BYTES) return { ok: false, code: 'TEXT_TOO_LARGE' };
    if (!Object.hasOwn(NEURAL_VOICE_PROFILES, voice)) return { ok: false, code: 'INVALID_VOICE' };
    if (ttsProvider === 'azure') {
      if (!azureSpeechKey || !azureEndpoint) return { ok: false, code: 'TTS_UNAVAILABLE' };
      const providerVoice = NEURAL_VOICE_PROFILES[voice][lang];
      const locale = SPEECH_LANGUAGES[lang];
      const ssml = `<speak version="1.0" xml:lang="${locale}"><voice name="${providerVoice}">${xmlEscape(clean)}</voice></speak>`;
      const controller = new AbortController();
      const abort = () => controller.abort();
      signal?.addEventListener('abort', abort, { once: true });
      const timer = setTimeout(abort, 45000);
      if (signal?.aborted) abort();
      try {
        const response = await fetchImpl(azureEndpoint, {
          method: 'POST',
          headers: {
            'Ocp-Apim-Subscription-Key': azureSpeechKey,
            'Content-Type': 'application/ssml+xml',
            'X-Microsoft-OutputFormat': AZURE_TTS_OUTPUT_FORMAT,
            'User-Agent': 'AURA-Simulator',
          },
          body: ssml,
          signal: controller.signal,
        });
        if (!response.ok) {
          if (response.status === 401 || response.status === 403) return { ok: false, code: 'PROVIDER_AUTH_FAILED' };
          if (response.status === 429) return { ok: false, code: 'RATE_LIMIT' };
          return { ok: false, code: response.status >= 500 ? 'TTS_UNAVAILABLE' : 'TTS_FAILED' };
        }
        const audio = await boundedAudio(response);
        if (!validMp3(audio)) return { ok: false, code: 'TTS_FAILED' };
        return { ok: true, audio, contentType: 'audio/mpeg', providerVoice };
      } catch (error) {
        if (signal?.aborted) return { ok: false, code: 'CANCELLED' };
        return { ok: false, code: error?.name === 'AbortError' ? 'TIMEOUT' : 'TTS_UNAVAILABLE' };
      } finally {
        clearTimeout(timer);
        signal?.removeEventListener('abort', abort);
      }
    }
    if (ttsProvider === 'edge-neural') {
      if (!neuralTtsAvailable()) return { ok: false, code: 'TTS_UNAVAILABLE' };
      const environment = { ...boundedEnvironment(), PYTHONPATH: neuralPythonPath };
      const result = await run(spawnImpl, python, [neuralTool,
        '--text-base64', Buffer.from(clean, 'utf8').toString('base64'),
        '--voice', NEURAL_VOICE_PROFILES[voice][lang],
      ], { timeoutMs: 45000, signal, binary: true, maxStdoutBytes: speechLimits.MAX_TTS_WAV_BYTES,
        env: environment });
      if (result.aborted) return { ok: false, code: 'CANCELLED' };
      if (result.timedOut) return { ok: false, code: 'TIMEOUT' };
      if (result.missing) return { ok: false, code: 'TTS_UNAVAILABLE' };
      if (result.overflow || result.code !== 0) return { ok: false, code: 'TTS_FAILED' };
      const audio = result.stdout;
      if (!validMp3(audio)) return { ok: false, code: 'TTS_FAILED' };
      return { ok: true, audio, contentType: 'audio/mpeg', providerVoice: NEURAL_VOICE_PROFILES[voice][lang] };
    }
    const tag = SPEECH_LANGUAGES[lang];
    if (!(await ttsTags()).has(tag)) return { ok: false, code: 'TTS_UNAVAILABLE' };
    await mkdir(workdir, { recursive: true });
    const file = path.join(workdir, `tts-${randomBytes(8).toString('hex')}.wav`);
    try {
      // Base64 keeps arbitrary text out of command-line quoting.
      const result = await run(spawnImpl, powershell, psArgs('speak.ps1', [
        '-TextBase64', Buffer.from(clean, 'utf8').toString('base64'), '-Language', tag, '-OutFile', file,
      ]), { timeoutMs: 30000, signal });
      if (result.aborted) return { ok: false, code: 'CANCELLED' };
      if (result.timedOut) return { ok: false, code: 'TIMEOUT' };
      if (result.code !== 0) return { ok: false, code: 'TTS_FAILED' };
      const wav = await readFile(file).catch(() => null);
      if (!wav || wav.length < 44 || wav.length > speechLimits.MAX_TTS_WAV_BYTES ||
          wav.toString('ascii', 0, 4) !== 'RIFF') {
        return { ok: false, code: 'TTS_FAILED' };
      }
      return { ok: true, wav, audio: wav, contentType: 'audio/wav' };
    } finally {
      await rm(file, { force: true });
    }
  }

  return { health, transcribe, synthesize, sttAvailable };
}

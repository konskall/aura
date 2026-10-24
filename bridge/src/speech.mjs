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
  MAX_WAV_BYTES: 1_100_000,         // 15 s of 16 kHz mono PCM plus header
  MIN_WAV_SAMPLES: 4800,            // 0.3 s
  MAX_TTS_TEXT_BYTES: 2000,
  MAX_TTS_WAV_BYTES: 8 * 1024 * 1024,
});

const bytes = (value) => Buffer.byteLength(value, 'utf8');

// large-v3-turbo gets Greek right where small does not ("αύριο", not "άυρω"),
// so it is used whenever setup-voice.ps1 installed it.
export const WHISPER_MODELS = Object.freeze(['ggml-large-v3-turbo-q5_0.bin', 'ggml-small-q5_1.bin']);

// whisper's encoder cost grows with its audio context (1500 = 30 s). A
// context sized to the clip plus a few seconds of margin keeps the full
// accuracy at a fraction of the time; under ~9 s of context turbo starts to
// mishear, so that is the floor, and 768 (15.4 s) covers the longest recording.
export function audioContextFor(sampleCount) {
  const seconds = Math.max(0, sampleCount) / 16000;
  const needed = Math.ceil(((seconds + 3.5) * 50) / 64) * 64;
  return Math.min(768, Math.max(448, needed));
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

function run(spawnImpl, command, args, { timeoutMs, signal }) {
  return new Promise((resolve) => {
    let child;
    try {
      child = spawnImpl(command, args, {
        env: boundedEnvironment(), shell: false, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'],
      });
    } catch {
      resolve({ code: -1, stdout: '', missing: true });
      return;
    }
    let stdout = Buffer.alloc(0);
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
    child.stdout.on('data', (chunk) => { if (stdout.length < 65536) stdout = Buffer.concat([stdout, chunk]); });
    child.stderr.on('data', () => {});
    child.once('close', (code) => finish({ code, stdout: stdout.toString('utf8') }));
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
  const scripts = options.scriptsDir ?? path.join(here, '..', 'scripts');
  const workdir = options.workdir ?? path.join(repoRoot, 'bridge', 'runtime', 'speech');
  // Physical cores matter more than hyper-threads for the encoder.
  const threads = String(Math.max(1, Math.min(4, os.cpus().length)));
  const psArgs = (script, extra = []) =>
    ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', path.join(scripts, script), ...extra];
  let voices = null;
  let voicesAt = 0;

  const sttAvailable = () => exists(whisper) && exists(model);

  async function ttsTags() {
    if (voices && Date.now() - voicesAt < 60000) return voices;
    const result = await run(spawnImpl, powershell, psArgs('voices.ps1'), { timeoutMs: 15000 });
    voices = new Set(result.code === 0 ?
      result.stdout.split(/\r?\n/).map((line) => line.trim()).filter((line) => /^[a-z]{2}-[A-Z]{2}$/.test(line)) : []);
    voicesAt = Date.now();
    return voices;
  }

  async function health() {
    const tags = await ttsTags();
    const result = { stt: sttAvailable() };
    for (const [code, tag] of Object.entries(SPEECH_LANGUAGES)) result[`tts_${code}`] = tags.has(tag);
    return result;
  }

  async function transcribe({ wav, lang, signal }) {
    if (lang !== 'auto' && !Object.hasOwn(SPEECH_LANGUAGES, lang)) return { ok: false, code: 'INVALID_LANGUAGE' };
    const check = inspectWav(wav);
    if (!check.ok) return check;
    if (!sttAvailable()) return { ok: false, code: 'STT_UNAVAILABLE' };
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

  async function synthesize({ text, lang, signal }) {
    if (!Object.hasOwn(SPEECH_LANGUAGES, lang)) return { ok: false, code: 'INVALID_LANGUAGE' };
    if (typeof text !== 'string') return { ok: false, code: 'INVALID_REQUEST' };
    const clean = text.replace(/[\u0000-\u001F\u007F]/g, ' ').replace(/\s+/g, ' ').trim();
    if (!clean) return { ok: false, code: 'INVALID_REQUEST' };
    if (bytes(clean) > speechLimits.MAX_TTS_TEXT_BYTES) return { ok: false, code: 'TEXT_TOO_LARGE' };
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
      return { ok: true, wav };
    } finally {
      await rm(file, { force: true });
    }
  }

  return { health, transcribe, synthesize, sttAvailable };
}

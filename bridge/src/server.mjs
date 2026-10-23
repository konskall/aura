import http from 'node:http';
import { fileURLToPath } from 'node:url';
import { createCodexRunner } from './codex-runner.mjs';
import { createSpeechEngines, SPEECH_LANGUAGES, speechLimits } from './speech.mjs';
import { buildInstructions, validateTurn } from './turn-prompt.mjs';

const host = '127.0.0.1';
const port = Number.parseInt(process.env.AURA_BRIDGE_PORT ?? '4778', 10);
const token = process.env.AURA_BRIDGE_TOKEN ?? '';

function isLoopback(address) {
  return address === '127.0.0.1' || address === '::1' || address === '::ffff:127.0.0.1';
}

function allowedOrigin(origin) {
  if (!origin) return true;
  return new Set([
    'http://127.0.0.1', 'http://localhost',
    `http://127.0.0.1:${process.env.AURA_CONTROLS_PORT ?? '4777'}`,
    `http://localhost:${process.env.AURA_CONTROLS_PORT ?? '4777'}`,
  ]).has(origin);
}

function send(response, status, payload) {
  const body = JSON.stringify(payload);
  response.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(body),
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff',
  });
  response.end(body);
}

async function readBody(request, maxBytes) {
  const chunks = [];
  let total = 0;
  for await (const chunk of request) {
    total += chunk.length;
    if (total > maxBytes) throw Object.assign(new Error('too large'), { code: 'BODY_TOO_LARGE' });
    chunks.push(chunk);
  }
  return Buffer.concat(chunks);
}

async function readJson(request, maxBytes = 16384) {
  const body = await readBody(request, maxBytes);
  try { return JSON.parse(body.toString('utf8')); }
  catch { throw Object.assign(new Error('bad json'), { code: 'INVALID_JSON' }); }
}

const failureStatus = (code) => (code === 'CANCELLED' ? 499 : 503);
const clientAudioErrors = new Set(['INVALID_AUDIO', 'UNSUPPORTED_AUDIO', 'AUDIO_TOO_SHORT', 'INVALID_LANGUAGE']);
const clientTextErrors = new Set(['INVALID_LANGUAGE', 'INVALID_REQUEST', 'TEXT_TOO_LARGE']);

export function createBridgeServer(options = {}) {
  const serviceRunner = options.runner ?? createCodexRunner();
  const speech = options.speech ?? createSpeechEngines();
  const serviceToken = options.token ?? token;
  const active = new Map();
  let speechJob = null;
  let healthCache = { value: null, at: 0 };

  // One speech job at a time. A client that disconnects before the reply
  // aborts its job; the request's own 'close' (body fully read) must not.
  async function withSpeechJob(response, work) {
    if (speechJob) {
      send(response, 409, { ok: false, code: 'BUSY' });
      return;
    }
    const job = new AbortController();
    speechJob = job;
    response.once('close', () => { if (!response.writableEnded) job.abort(); });
    try {
      await work(job.signal);
    } finally {
      speechJob = null;
    }
  }

  const server = http.createServer(async (request, response) => {
    if (!isLoopback(request.socket.remoteAddress) || !allowedOrigin(request.headers.origin)) {
      send(response, 403, { ok: false, code: 'FORBIDDEN' });
      return;
    }
    const url = new URL(request.url ?? '/', 'http://127.0.0.1');
    if (request.method === 'GET' && url.pathname === '/health') {
      if (!healthCache.value || Date.now() - healthCache.at > 10000) {
        const [cli, speechHealth] = await Promise.all([serviceRunner.health(), speech.health()]);
        healthCache = { value: { cli, speech: speechHealth }, at: Date.now() };
      }
      send(response, 200, { ok: true, service: 'aura-codex-bridge', ...healthCache.value });
      return;
    }
    if (!serviceToken || request.headers.authorization !== `Bearer ${serviceToken}`) {
      send(response, 401, { ok: false, code: 'UNAUTHORIZED' });
      return;
    }

    try {
      if (request.method === 'POST' && url.pathname === '/v1/assistant/turn') {
        const body = await readJson(request, 24576);
        if (!body || typeof body.id !== 'string' || !/^[A-Za-z0-9._-]{1,80}$/.test(body.id)) {
          send(response, 400, { ok: false, code: 'INVALID_REQUEST' });
          return;
        }
        const turn = validateTurn(body);
        if (!turn.ok) {
          send(response, turn.code === 'PROMPT_TOO_LARGE' ? 413 : 400, { ok: false, code: turn.code });
          return;
        }
        if (active.size > 0 || active.has(body.id)) {
          send(response, 409, { ok: false, code: 'BUSY' });
          return;
        }
        const controller = new AbortController();
        active.set(body.id, controller);
        const started = Date.now();
        // Sizes and counts only: no prompt, history or device facts are logged.
        console.log(JSON.stringify({ event: 'turn_started', id: body.id, mode: turn.value.mode,
          promptBytes: Buffer.byteLength(turn.value.prompt), historyTurns: turn.value.history?.length ?? 0,
          contextBytes: Buffer.byteLength(turn.value.context ?? '') }));
        const result = await serviceRunner.runTurn({ instructions: buildInstructions(turn.value),
          signal: controller.signal });
        active.delete(body.id);
        console.log(JSON.stringify({ event: 'turn_finished', id: body.id, ok: result.ok, code: result.code ?? null,
          durationMs: Date.now() - started }));
        send(response, result.ok ? 200 : failureStatus(result.code), result);
        return;
      }
      if (request.method === 'POST' && url.pathname === '/v1/assistant/cancel') {
        const body = await readJson(request, 1024);
        const controller = body && typeof body.id === 'string' ? active.get(body.id) : null;
        if (!controller) {
          send(response, 404, { ok: false, code: 'TURN_NOT_FOUND' });
          return;
        }
        controller.abort();
        active.delete(body.id);
        send(response, 200, { ok: true, code: 'CANCELLED' });
        return;
      }
      if (request.method === 'POST' && url.pathname === '/v1/speech/transcribe') {
        const lang = url.searchParams.get('lang') ?? 'el';
        if (lang !== 'auto' && !Object.hasOwn(SPEECH_LANGUAGES, lang)) {
          send(response, 400, { ok: false, code: 'INVALID_LANGUAGE' });
          return;
        }
        if (!String(request.headers['content-type'] ?? '').startsWith('audio/wav')) {
          send(response, 415, { ok: false, code: 'UNSUPPORTED_MEDIA_TYPE' });
          return;
        }
        await withSpeechJob(response, async (signal) => {
          const wav = await readBody(request, speechLimits.MAX_WAV_BYTES);
          const result = await speech.transcribe({ wav, lang, signal });
          // The transcript itself is never logged.
          console.log(JSON.stringify({ event: 'transcribe_finished', ok: result.ok, code: result.code ?? null,
            audioBytes: wav.length }));
          send(response, result.ok ? 200 : (clientAudioErrors.has(result.code) ? 400 : failureStatus(result.code)),
            result);
        });
        return;
      }
      if (request.method === 'POST' && url.pathname === '/v1/speech/synthesize') {
        const body = await readJson(request, 8192);
        if (!body || typeof body.text !== 'string' || typeof body.lang !== 'string') {
          send(response, 400, { ok: false, code: 'INVALID_REQUEST' });
          return;
        }
        await withSpeechJob(response, async (signal) => {
          const result = await speech.synthesize({ text: body.text, lang: body.lang, signal });
          console.log(JSON.stringify({ event: 'synthesize_finished', ok: result.ok, code: result.code ?? null,
            textBytes: Buffer.byteLength(body.text) }));
          if (!result.ok) {
            send(response, clientTextErrors.has(result.code) ? 400 : failureStatus(result.code), result);
            return;
          }
          response.writeHead(200, {
            'Content-Type': 'audio/wav',
            'Content-Length': result.wav.length,
            'Cache-Control': 'no-store',
            'X-Content-Type-Options': 'nosniff',
          });
          response.end(result.wav);
        });
        return;
      }
      send(response, 404, { ok: false, code: 'NOT_FOUND' });
    } catch (error) {
      send(response, error.code === 'BODY_TOO_LARGE' ? 413 : 400,
        { ok: false, code: error.code ?? 'INVALID_REQUEST' });
    }
  });
  server.cancelAll = () => {
    for (const controller of active.values()) controller.abort();
    active.clear();
    speechJob?.abort();
  };
  return server;
}

const isMain = process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1];
if (isMain) {
  if (!token) {
    console.error('AURA_BRIDGE_TOKEN is required.');
    process.exit(2);
  }
  const server = createBridgeServer();
  server.listen(port, host, () => {
    console.log(JSON.stringify({ event: 'bridge_ready', host, port }));
  });
  const shutdown = () => {
    server.cancelAll();
    server.close(() => process.exit(0));
    setTimeout(() => process.exit(0), 1500).unref();
  };
  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);
}

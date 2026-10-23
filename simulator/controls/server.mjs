import http from 'node:http';
import { appendFile, mkdir, readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const host = '127.0.0.1';
const port = Number.parseInt(process.env.AURA_CONTROLS_PORT ?? '4777', 10);
const token = process.env.AURA_BRIDGE_TOKEN ?? '';
const eventPath = process.env.AURA_CONTROL_EVENTS ?? path.resolve(here, '..', 'runtime', 'control-events.log');
const allowedTypes = new Set([
  'wake_detected', 'begin_recording', 'end_recording', 'prompt', 'partial', 'final',
  'provider_unavailable', 'error', 'notification', 'wifi', 'wifi_fail_next', 'bluetooth', 'battery',
  'battery_unknown', 'charging',
  'provider', 'screen', 'cancel', 'speaker',
  'reset', 'ui_view', 'review_long_answer', 'review_speaking',
  'audio_test', 'audio_wav', 'audio_pause', 'audio_resume', 'audio_stop', 'audio_mute', 'audio_volume',
  'audio_complete', 'audio_fail', 'input_amplitude',
  'weather_city', 'weather_refresh',
  'aoraton_incoming', 'aoraton_unread', 'aoraton_disconnect', 'aoraton_reconnect',
  'aoraton_invalid', 'aoraton_pairing_failure', 'aoraton_reset', 'aoraton_prepare',
  'aoraton_confirm', 'aoraton_sound',
  // v13 companion (DEMO stand-ins for hardware and phone input)
  'timer_start', 'alarm_soon', 'reminder_soon', 'briefing', 'imu', 'display_sleep',
  'phone_notification', 'phone_call', 'phone_call_end', 'phone_clear', 'phone_pair',
  'update_available', 'translate_text', 'note_text',
]);

export function validateEvent(value) {
  if (!value || typeof value !== 'object' || !allowedTypes.has(value.type)) return null;
  const payload = value.payload == null ? '' : String(value.payload);
  if (Buffer.byteLength(payload, 'utf8') > 8000) return null;
  return { type: value.type, payload };
}

async function readJson(request) {
  const chunks = [];
  let size = 0;
  for await (const chunk of request) {
    size += chunk.length;
    if (size > 12000) throw new Error('BODY_TOO_LARGE');
    chunks.push(chunk);
  }
  return JSON.parse(Buffer.concat(chunks).toString('utf8'));
}

function json(response, status, value) {
  const body = JSON.stringify(value);
  response.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(body),
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff',
  });
  response.end(body);
}

export function createControlServer() {
  return http.createServer(async (request, response) => {
    if (request.socket.remoteAddress !== '127.0.0.1' && request.socket.remoteAddress !== '::ffff:127.0.0.1' &&
        request.socket.remoteAddress !== '::1') {
      json(response, 403, { ok: false });
      return;
    }
    if (request.method === 'GET' && request.url === '/health') {
      json(response, 200, { ok: true, service: 'aura-simulator-controls' });
      return;
    }
    if (request.method === 'GET' && (request.url === '/' || request.url.startsWith('/?'))) {
      const body = await readFile(path.join(here, 'public', 'index.html'));
      response.writeHead(200, {
        'Content-Type': 'text/html; charset=utf-8',
        'Content-Length': body.length,
        'Cache-Control': 'no-store',
        'Content-Security-Policy': "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'",
        'X-Frame-Options': 'DENY',
      });
      response.end(body);
      return;
    }
    if (request.method === 'POST' && request.url === '/api/event') {
      if (!token || request.headers.authorization !== `Bearer ${token}`) {
        json(response, 401, { ok: false, code: 'UNAUTHORIZED' });
        return;
      }
      try {
        const event = validateEvent(await readJson(request));
        if (!event) {
          json(response, 400, { ok: false, code: 'INVALID_EVENT' });
          return;
        }
        await mkdir(path.dirname(eventPath), { recursive: true });
        const line = `${Date.now()}\t${event.type}\t${encodeURIComponent(event.payload)}\n`;
        await appendFile(eventPath, line, 'utf8');
        json(response, 202, { ok: true });
      } catch {
        json(response, 400, { ok: false, code: 'INVALID_REQUEST' });
      }
      return;
    }
    json(response, 404, { ok: false, code: 'NOT_FOUND' });
  });
}

const isMain = process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1];
if (isMain) {
  if (!token) {
    console.error('AURA_BRIDGE_TOKEN is required.');
    process.exit(2);
  }
  const server = createControlServer();
  server.listen(port, host, () => {
    console.log(JSON.stringify({ event: 'controls_ready', host, port, eventPath }));
  });
  const shutdown = () => server.close(() => process.exit(0));
  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);
}

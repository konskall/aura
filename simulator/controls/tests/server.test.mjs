import assert from 'node:assert/strict';
import { test } from 'node:test';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { supportedEventTypes, validateEvent } from '../server.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));

test('accepts known bounded events', () => {
  assert.deepEqual(validateEvent({ type: 'prompt', payload: 'Τι καιρό κάνει;' }),
    { type: 'prompt', payload: 'Τι καιρό κάνει;' });
  assert.deepEqual(validateEvent({ type: 'battery', payload: 42 }),
    { type: 'battery', payload: '42' });
  assert.deepEqual(validateEvent({ type: 'battery_unknown' }),
    { type: 'battery_unknown', payload: '' });
  assert.deepEqual(validateEvent({ type: 'bluetooth', payload: 1 }),
    { type: 'bluetooth', payload: '1' });
  assert.deepEqual(validateEvent({ type: 'aoraton_incoming', payload: 'Ασφαλές DEMO' }),
    { type: 'aoraton_incoming', payload: 'Ασφαλές DEMO' });
  assert.deepEqual(validateEvent({ type: 'audio_volume', payload: 35 }),
    { type: 'audio_volume', payload: '35' });
  assert.deepEqual(validateEvent({ type: 'input_amplitude', payload: 72 }),
    { type: 'input_amplitude', payload: '72' });
  assert.deepEqual(validateEvent({ type: 'audio_fail', payload: 'DEMO_AUDIO_FAILURE' }),
    { type: 'audio_fail', payload: 'DEMO_AUDIO_FAILURE' });
  assert.deepEqual(validateEvent({ type: 'voice_preview', payload: 'nestoras' }),
    { type: 'voice_preview', payload: 'nestoras' });
  assert.deepEqual(validateEvent({ type: 'ui_view', payload: 'launcher' }),
    { type: 'ui_view', payload: 'launcher' });
  assert.deepEqual(validateEvent({ type: 'weather_city', payload: 'Σπάρτη' }),
    { type: 'weather_city', payload: 'Σπάρτη' });
});

test('accepts the v13 companion stand-ins', () => {
  for (const type of ['timer_start', 'alarm_soon', 'reminder_soon', 'briefing', 'imu', 'display_sleep',
    'phone_notification', 'phone_call', 'phone_call_end', 'phone_clear', 'phone_pair', 'update_available',
    'translate_text', 'note_text']) {
    assert.equal(validateEvent({ type, payload: 'DEMO' })?.type, type);
  }
  assert.deepEqual(validateEvent({ type: 'imu', payload: 'face_down' }), { type: 'imu', payload: 'face_down' });
});

test('accepts current language, radio, and fuel controls', () => {
  for (const type of ['language', 'radio_play', 'radio_stop', 'radio_search', 'fuel_refresh']) {
    assert.equal(validateEvent({ type, payload: 'DEMO' })?.type, type);
    assert.ok(supportedEventTypes.includes(type));
  }
});

test('control page only emits supported events and routes to handled views', async () => {
  const html = await readFile(path.resolve(here, '..', 'public', 'index.html'), 'utf8');
  const main = await readFile(path.resolve(here, '..', '..', 'app', 'src', 'main.cpp'), 'utf8');
  const declaredEvents = [...html.matchAll(/data-event="([a-z_]+)"/g)].map((match) => match[1]);
  const scriptedEvents = [...html.matchAll(/send\('([a-z_]+)'/g)].map((match) => match[1]);
  for (const type of new Set([...declaredEvents, ...scriptedEvents])) {
    assert.ok(supportedEventTypes.includes(type), `unsupported event in page: ${type}`);
  }

  const viewButtons = [...html.matchAll(/data-event="ui_view" data-value="([a-z-]+)"/g)]
    .map((match) => match[1]);
  for (const view of viewButtons) {
    assert.ok(main.includes(`event.payload == "${view}"`), `unhandled UI route: ${view}`);
  }

  const ids = [...html.matchAll(/\sid="([^"]+)"/g)].map((match) => match[1]);
  assert.equal(new Set(ids).size, ids.length, 'HTML ids must be unique');
  assert.match(html, /<option value="groq">Groq API<\/option>/, 'the active Groq backend is visible in controls');
  assert.match(main, /event\.payload == "codex" \|\| event\.payload == "groq"/,
    'Codex and Groq both select the live loopback gateway');
});

test('rejects unknown and oversized events', () => {
  assert.equal(validateEvent({ type: 'shell', payload: 'dir' }), null);
  assert.equal(validateEvent({ type: 'prompt', payload: 'x'.repeat(9000) }), null);
  assert.equal(validateEvent(null), null);
});

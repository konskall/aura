import assert from 'node:assert/strict';
import { test } from 'node:test';
import { validateEvent } from '../server.mjs';

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

test('rejects unknown and oversized events', () => {
  assert.equal(validateEvent({ type: 'shell', payload: 'dir' }), null);
  assert.equal(validateEvent({ type: 'prompt', payload: 'x'.repeat(9000) }), null);
  assert.equal(validateEvent(null), null);
});

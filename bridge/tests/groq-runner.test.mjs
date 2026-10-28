import assert from 'node:assert/strict';
import { test } from 'node:test';
import { ReadableStream } from 'node:stream/web';
import { createGroqRunner, groqDefaults } from '../src/groq-runner.mjs';

function sseResponse(parts, status = 200) {
  const encoder = new TextEncoder();
  return {
    ok: status >= 200 && status < 300,
    status,
    body: new ReadableStream({
      start(controller) {
        for (const part of parts) controller.enqueue(encoder.encode(part));
        controller.close();
      },
    }),
  };
}

test('Groq runner streams Greek deltas and uses the current configured model', async () => {
  const calls = [];
  const deltas = [];
  const runner = createGroqRunner({
    apiKey: 'unit-test-secret',
    fetchImpl: async (url, options) => {
      calls.push({ url, options });
      return sseResponse([
        'data: {"choices":[{"delta":{"content":"Καλη"}}]}\n\n',
        'data: {"choices":[{"delta":{"content":"μέρα σου."}}]}\n\n',
        'data: [DONE]\n\n',
      ]);
    },
  });
  const result = await runner.runTurnStream({ instructions: 'Απάντησε σύντομα.', onDelta: (value) => deltas.push(value) });
  assert.deepEqual(result, { ok: true, text: 'Καλημέρα σου.' });
  assert.deepEqual(deltas, ['Καλη', 'μέρα σου.']);
  assert.equal(calls[0].url, groqDefaults.endpoint);
  assert.equal(calls[0].options.headers.authorization, 'Bearer unit-test-secret');
  assert.equal(JSON.parse(calls[0].options.body).model, groqDefaults.model);
});

test('Groq runner fails closed without a secret and maps quota failures', async () => {
  const missing = createGroqRunner({ apiKey: '', fetchImpl: async () => { throw new Error('must not run'); } });
  assert.equal((await missing.runTurn({ instructions: 'hello' })).code, 'PROVIDER_NOT_CONFIGURED');
  const limited = createGroqRunner({ apiKey: 'x', fetchImpl: async () => sseResponse([], 429) });
  assert.equal((await limited.runTurn({ instructions: 'hello' })).code, 'RATE_LIMIT');
});

test('Groq runner sanitizes streamed controls before publishing deltas', async () => {
  const deltas = [];
  const payload = `data: ${JSON.stringify({ choices: [{ delta: { content: 'safe\u0001 text' } }] })}\n\n`;
  const runner = createGroqRunner({
    apiKey: 'unit-test-secret',
    fetchImpl: async () => sseResponse([payload, 'data: [DONE]\n\n']),
  });
  const result = await runner.runTurnStream({ instructions: 'hello', onDelta: (value) => deltas.push(value) });
  assert.deepEqual(deltas, ['safe text']);
  assert.deepEqual(result, { ok: true, text: 'safe text' });
});

test('Groq runner cancels the response body after an early bounded failure', async () => {
  const encoder = new TextEncoder();
  let cancelled = false;
  const oversized = JSON.stringify({ choices: [{ delta: { content: 'x'.repeat(12001) } }] });
  const runner = createGroqRunner({
    apiKey: 'unit-test-secret',
    fetchImpl: async () => ({
      ok: true,
      status: 200,
      body: new ReadableStream({
        start(controller) { controller.enqueue(encoder.encode(`data: ${oversized}\n\n`)); },
        cancel() { cancelled = true; },
      }),
    }),
  });
  const result = await runner.runTurnStream({ instructions: 'hello' });
  assert.deepEqual(result, { ok: false, code: 'RESPONSE_TOO_LARGE' });
  assert.equal(cancelled, true);
});

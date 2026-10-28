import assert from 'node:assert/strict';
import { test } from 'node:test';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createCodexRunner, sanitizeDisplayText } from '../src/codex-runner.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const fake = path.join(here, 'fake-codex.mjs');

function runner(options = {}) {
  return createCodexRunner({
    command: process.execPath,
    prefixArgs: [fake],
    workdir: path.join(here, '.runtime'),
    ...options,
  });
}

test('fake subprocess returns UTF-8 Greek text', async () => {
  const result = await runner().runTurn({ prompt: 'Πες μου κάτι' });
  assert.equal(result.ok, true);
  assert.match(result.text, /Δοκιμαστική/);
});

test('an explicitly marked turn enables the Codex web-search tool', async () => {
  const result = await runner().runTurn({ prompt: 'Ψάξε για πίτσα στη Σπάρτη', webSearch: true });
  assert.equal(result.ok, true);
  assert.match(result.text, /web search/);
});

test('prompt limit is enforced before spawn', async () => {
  const result = await runner().runTurn({ prompt: 'α'.repeat(3000) });
  assert.deepEqual(result, { ok: false, code: 'PROMPT_TOO_LARGE' });
});

test('timeout terminates a slow child', async () => {
  const result = await runner({ timeoutMs: 150 }).runTurn({ prompt: '[timeout]' });
  assert.deepEqual(result, { ok: false, code: 'TIMEOUT' });
});

test('abort reports cancellation', async () => {
  const controller = new AbortController();
  const promise = runner({ timeoutMs: 5000 }).runTurn({ prompt: '[timeout]', signal: controller.signal });
  setTimeout(() => controller.abort(), 75);
  assert.deepEqual(await promise, { ok: false, code: 'CANCELLED' });
});

test('oversized and failed output are normalized', async () => {
  assert.equal((await runner().runTurn({ prompt: '[large]' })).code, 'RESPONSE_TOO_LARGE');
  assert.equal((await runner().runTurn({ prompt: '[error]' })).code, 'RATE_LIMIT');
  assert.equal(sanitizeDisplayText('a\u0000b\r\nc'), 'ab\nc');
});

test('health probes executable and login status', async () => {
  assert.deepEqual(await runner().health(), { installed: true, loggedIn: true });
});

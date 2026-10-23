import assert from 'node:assert/strict';
import { test } from 'node:test';
import { createBridgeServer } from '../src/server.mjs';

const token = 'test-token';

function fakeSpeech(overrides = {}) {
  return {
    health: async () => ({ stt: true, tts_el: true, tts_en: false }),
    transcribe: async () => ({ ok: true, text: 'Βάλε χρονόμετρο' }),
    synthesize: async () => ({ ok: true, wav: Buffer.from('RIFF....WAVEfmt fake') }),
    ...overrides,
  };
}

async function withServer(runner, callback, speech = fakeSpeech()) {
  const server = createBridgeServer({ runner, token, speech });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const { port } = server.address();
  try { await callback(`http://127.0.0.1:${port}`, server); }
  finally {
    server.cancelAll();
    await new Promise((resolve) => server.close(resolve));
  }
}

function headers() {
  return { authorization: `Bearer ${token}`, 'content-type': 'application/json' };
}

test('health and authorization boundary', async () => {
  const runner = { health: async () => ({ installed: true, loggedIn: true }) };
  await withServer(runner, async (base) => {
    assert.equal((await fetch(`${base}/health`)).status, 200);
    assert.equal((await fetch(`${base}/v1/assistant/turn`, {
      method: 'POST', headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ id: 'one', prompt: 'hello' }),
    })).status, 401);
  });
});

test('one active turn, cancellation, and recovery', async () => {
  const runner = {
    health: async () => ({ installed: true, loggedIn: true }),
    runTurn: ({ signal }) => new Promise((resolve) => {
      const timer = setTimeout(() => resolve({ ok: true, text: 'late' }), 2000);
      signal.addEventListener('abort', () => {
        clearTimeout(timer);
        resolve({ ok: false, code: 'CANCELLED' });
      }, { once: true });
    }),
  };
  await withServer(runner, async (base) => {
    const first = fetch(`${base}/v1/assistant/turn`, {
      method: 'POST', headers: headers(), body: JSON.stringify({ id: 'first', prompt: 'wait' }),
    });
    await new Promise((resolve) => setTimeout(resolve, 40));
    const concurrent = await fetch(`${base}/v1/assistant/turn`, {
      method: 'POST', headers: headers(), body: JSON.stringify({ id: 'second', prompt: 'busy' }),
    });
    assert.equal(concurrent.status, 409);
    const cancelled = await fetch(`${base}/v1/assistant/cancel`, {
      method: 'POST', headers: headers(), body: JSON.stringify({ id: 'first' }),
    });
    assert.equal(cancelled.status, 200);
    assert.equal((await first).status, 499);

    runner.runTurn = async () => ({ ok: true, text: 'recovered' });
    const recovered = await fetch(`${base}/v1/assistant/turn`, {
      method: 'POST', headers: headers(), body: JSON.stringify({ id: 'third', prompt: 'again' }),
    });
    assert.equal(recovered.status, 200);
    assert.equal((await recovered.json()).text, 'recovered');
  });
});


test('turns send device facts, history and translation as built instructions', async () => {
  const seen = [];
  const runner = {
    health: async () => ({ installed: true, loggedIn: true }),
    runTurn: async ({ instructions }) => { seen.push(instructions); return { ok: true, text: 'ok' }; },
  };
  await withServer(runner, async (base) => {
    const health = await (await fetch(`${base}/health`)).json();
    assert.deepEqual(health.speech, { stt: true, tts_el: true, tts_en: false });
    const chat = await fetch(`${base}/v1/assistant/turn`, {
      method: 'POST', headers: headers(), body: JSON.stringify({
        id: 'chat', prompt: 'Και αύριο;', mode: 'chat', context: 'weather for Αθήνα: now 22 C',
        history: [{ q: 'Τι καιρό κάνει;', a: 'Λιακάδα.' }],
      }),
    });
    assert.equal(chat.status, 200);
    assert.match(seen[0], /weather for Αθήνα/);
    assert.match(seen[0], /AURA: Λιακάδα\./);
    const translate = await fetch(`${base}/v1/assistant/turn`, {
      method: 'POST', headers: headers(),
      body: JSON.stringify({ id: 'tr', prompt: 'Ευχαριστώ', mode: 'translate', source: 'el', target: 'en' }),
    });
    assert.equal(translate.status, 200);
    assert.match(seen[1], /from Greek to English/);
    const invalid = await fetch(`${base}/v1/assistant/turn`, {
      method: 'POST', headers: headers(),
      body: JSON.stringify({ id: 'bad', prompt: 'x', history: [{ q: 'a' }] }),
    });
    assert.equal(invalid.status, 400);
    assert.equal((await invalid.json()).code, 'INVALID_HISTORY');
  });
});

test('speech endpoints: media type, busy guard and binary WAV replies', async () => {
  let release;
  const slow = new Promise((resolve) => { release = resolve; });
  const speech = fakeSpeech({
    // Like whisper: an aborted job reports CANCELLED, so a spurious abort fails the test.
    transcribe: async ({ wav, lang, signal }) => {
      await slow;
      await new Promise((resolve) => setTimeout(resolve, 30));
      return signal.aborted ? { ok: false, code: 'CANCELLED' } : { ok: true, text: `${lang}:${wav.length}` };
    },
  });
  const runner = { health: async () => ({ installed: true, loggedIn: true }) };
  await withServer(runner, async (base) => {
    const unauthorised = await fetch(`${base}/v1/speech/transcribe?lang=el`, {
      method: 'POST', headers: { 'content-type': 'audio/wav' }, body: Buffer.alloc(64),
    });
    assert.equal(unauthorised.status, 401);
    const wrongType = await fetch(`${base}/v1/speech/transcribe?lang=el`, {
      method: 'POST', headers: headers(), body: Buffer.alloc(64),
    });
    assert.equal(wrongType.status, 415);
    const badLanguage = await fetch(`${base}/v1/speech/transcribe?lang=xx`, {
      method: 'POST', headers: { ...headers(), 'content-type': 'audio/wav' }, body: Buffer.alloc(64),
    });
    assert.equal(badLanguage.status, 400);
    const first = fetch(`${base}/v1/speech/transcribe?lang=el`, {
      method: 'POST', headers: { ...headers(), 'content-type': 'audio/wav' }, body: Buffer.alloc(64),
    });
    await new Promise((resolve) => setTimeout(resolve, 40));
    const busy = await fetch(`${base}/v1/speech/synthesize`, {
      method: 'POST', headers: headers(), body: JSON.stringify({ text: 'Γεια', lang: 'el' }),
    });
    assert.equal(busy.status, 409);
    release();
    assert.deepEqual(await (await first).json(), { ok: true, text: 'el:64' });
    const spoken = await fetch(`${base}/v1/speech/synthesize`, {
      method: 'POST', headers: headers(), body: JSON.stringify({ text: 'Γεια', lang: 'el' }),
    });
    assert.equal(spoken.status, 200);
    assert.equal(spoken.headers.get('content-type'), 'audio/wav');
    assert.equal(Buffer.from(await spoken.arrayBuffer()).toString('ascii', 0, 4), 'RIFF');
    const tooLarge = await fetch(`${base}/v1/speech/transcribe?lang=el`, {
      method: 'POST', headers: { ...headers(), 'content-type': 'audio/wav' }, body: Buffer.alloc(1_200_000),
    });
    assert.equal(tooLarge.status, 413);
  }, speech);
});

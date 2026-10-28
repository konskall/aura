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

test('fuel endpoint is authenticated and fails closed when its cache is invalid', async () => {
  const runner = { health: async () => ({ installed: true, loggedIn: true }) };
  const snapshot = {
    schema: 1, country: 'GR', period_date: '2026-10-19',
    source: 'European Commission Weekly Oil Bulletin',
    source_url: 'https://energy.ec.europa.eu/data-and-analysis/weekly-oil-bulletin_en',
    unleaded95_millieur_per_litre: 2191, diesel_millieur_per_litre: 2126,
    lpg_millieur_per_litre: null,
  };
  const server = createBridgeServer({ runner, token, speech: fakeSpeech(), fuelReader: async () => snapshot });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const base = `http://127.0.0.1:${server.address().port}`;
  try {
    assert.equal((await fetch(`${base}/v1/fuel/prices`)).status, 401);
    const response = await fetch(`${base}/v1/fuel/prices`, { headers: headers() });
    assert.equal(response.status, 200);
    assert.deepEqual(await response.json(), { ok: true, ...snapshot });
  } finally {
    server.cancelAll();
    await new Promise((resolve) => server.close(resolve));
  }

  const missing = createBridgeServer({ runner, token, speech: fakeSpeech(), fuelReader: async () => null });
  await new Promise((resolve) => missing.listen(0, '127.0.0.1', resolve));
  try {
    const response = await fetch(`http://127.0.0.1:${missing.address().port}/v1/fuel/prices`, { headers: headers() });
    assert.equal(response.status, 503);
    assert.equal((await response.json()).code, 'FUEL_DATA_UNAVAILABLE');
  } finally {
    missing.cancelAll();
    await new Promise((resolve) => missing.close(resolve));
  }
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

test('web search is passed only to a runner with a real search capability', async () => {
  const seen = [];
  const runner = {
    supportsWebSearch: true,
    health: async () => ({ installed: true, loggedIn: true }),
    runTurn: async (request) => { seen.push(request); return { ok: true, text: 'results' }; },
  };
  await withServer(runner, async (base) => {
    const response = await fetch(`${base}/v1/assistant/turn`, {
      method: 'POST', headers: headers(),
      body: JSON.stringify({ id: 'web-one', prompt: 'Ψάξε για πίτσα στη Σπάρτη', web_search: true }),
    });
    assert.equal(response.status, 200);
    assert.equal(seen[0].webSearch, true);
    assert.match(seen[0].instructions, /live web-search tool/);
  });

  // Without a search tool the question is still answered: the model says it
  // cannot search right now and never claims live results.
  const fallback = [];
  const unavailable = {
    supportsWebSearch: false,
    health: async () => ({ provider: 'test' }),
    runTurn: async (request) => { fallback.push(request); return { ok: true, text: 'Δεν μπορώ να ψάξω τώρα.' }; },
  };
  await withServer(unavailable, async (base) => {
    const response = await fetch(`${base}/v1/assistant/turn`, {
      method: 'POST', headers: headers(),
      body: JSON.stringify({ id: 'web-two', prompt: 'search the web', web_search: true }),
    });
    assert.equal(response.status, 200);
    assert.equal(fallback[0].webSearch, false);
    assert.match(fallback[0].instructions, /cannot search the web right now/);
    assert.doesNotMatch(fallback[0].instructions, /live web-search tool/);
  });
});

test('a runner that throws frees the turn slot', async () => {
  let calls = 0;
  const runner = {
    health: async () => ({ provider: 'test' }),
    runTurnStream: async ({ onDelta }) => {
      calls += 1;
      if (calls === 1) {
        onDelta('Μισή ');
        throw new Error('provider crashed');
      }
      return { ok: true, text: 'Εντάξει.' };
    },
  };
  await withServer(runner, async (base) => {
    const first = await fetch(`${base}/v1/assistant/stream`, {
      method: 'POST', headers: headers(), body: JSON.stringify({ id: 'crash-one', prompt: 'hello' }),
      signal: AbortSignal.timeout(3000),
    });
    assert.match(await first.text(), /"type":"error"/);
    const second = await fetch(`${base}/v1/assistant/stream`, {
      method: 'POST', headers: headers(), body: JSON.stringify({ id: 'crash-two', prompt: 'hello' }),
      signal: AbortSignal.timeout(3000),
    });
    assert.equal(second.status, 200);
    assert.match(await second.text(), /"type":"done"/);
  });
});

test('stream endpoint emits bounded NDJSON deltas and an authoritative final reply', async () => {
  const runner = {
    health: async () => ({ provider: 'test', configured: true }),
    runTurnStream: async ({ onDelta }) => {
      onDelta('Καλησπέρα. ');
      onDelta('Τι κάνεις;');
      return { ok: true, text: 'Καλησπέρα. Τι κάνεις;' };
    },
  };
  await withServer(runner, async (base) => {
    const response = await fetch(`${base}/v1/assistant/stream`, {
      method: 'POST', headers: headers(), body: JSON.stringify({ id: 'stream-one', prompt: 'hello' }),
    });
    assert.equal(response.status, 200);
    assert.match(response.headers.get('content-type'), /application\/x-ndjson/);
    const events = (await response.text()).trim().split('\n').map((line) => JSON.parse(line));
    assert.deepEqual(events, [
      { type: 'delta', text: 'Καλησπέρα. ' },
      { type: 'delta', text: 'Τι κάνεις;' },
      { type: 'done', text: 'Καλησπέρα. Τι κάνεις;' },
    ]);
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

test('speech synthesis forwards the selected profile and serves neural MP3', async () => {
  let received;
  const speech = fakeSpeech({
    synthesize: async (request) => {
      received = request;
      return { ok: true, audio: Buffer.from('ID3neural-audio'), contentType: 'audio/mpeg' };
    },
  });
  const runner = { health: async () => ({ installed: true, loggedIn: true }) };
  await withServer(runner, async (base) => {
    const response = await fetch(`${base}/v1/speech/synthesize`, {
      method: 'POST', headers: headers(),
      body: JSON.stringify({ text: 'Καλημέρα', lang: 'el', voice: 'nestoras' }),
    });
    assert.equal(response.status, 200);
    assert.equal(response.headers.get('content-type'), 'audio/mpeg');
    assert.equal(Buffer.from(await response.arrayBuffer()).toString('ascii', 0, 3), 'ID3');
    assert.equal(received.text, 'Καλημέρα');
    assert.equal(received.lang, 'el');
    assert.equal(received.voice, 'nestoras');
    assert.ok(received.signal instanceof AbortSignal);
  }, speech);
});

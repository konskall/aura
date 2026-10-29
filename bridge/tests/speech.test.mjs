import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { existsSync } from 'node:fs';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { PassThrough } from 'node:stream';
import { test } from 'node:test';
import { audioContextFor, cleanTranscript, createSpeechEngines, inspectWav, NEURAL_VOICE_PROFILES,
  WHISPER_MODELS } from '../src/speech.mjs';

function wav({ rate = 16000, channels = 1, bits = 16, samples = 8000 } = {}) {
  const data = samples * channels * (bits / 8);
  const buffer = Buffer.alloc(44 + data);
  buffer.write('RIFF', 0, 'ascii');
  buffer.writeUInt32LE(36 + data, 4);
  buffer.write('WAVEfmt ', 8, 'ascii');
  buffer.writeUInt32LE(16, 16);
  buffer.writeUInt16LE(1, 20);
  buffer.writeUInt16LE(channels, 22);
  buffer.writeUInt32LE(rate, 24);
  buffer.writeUInt32LE(rate * channels * (bits / 8), 28);
  buffer.writeUInt16LE(channels * (bits / 8), 32);
  buffer.writeUInt16LE(bits, 34);
  buffer.write('data', 36, 'ascii');
  buffer.writeUInt32LE(data, 40);
  return buffer;
}

// A child process stand-in: records the call, then runs `behaviour`.
function fakeSpawn(calls, behaviour) {
  return (command, args, options) => {
    calls.push({ command, args, options });
    const child = new EventEmitter();
    child.pid = 4242;
    child.exitCode = null;
    child.stdout = new PassThrough();
    child.stderr = new PassThrough();
    setImmediate(async () => {
      const { code = 0, stdout = '' } = (await behaviour(command, args)) ?? {};
      child.stdout.end(stdout);
      child.stderr.end();
      child.exitCode = code;
      setImmediate(() => child.emit('close', code));
    });
    return child;
  };
}

test('only 16 kHz mono 16-bit PCM WAV reaches the recogniser', () => {
  assert.equal(inspectWav(wav()).ok, true);
  assert.equal(inspectWav(wav({ rate: 44100 })).code, 'UNSUPPORTED_AUDIO');
  assert.equal(inspectWav(wav({ channels: 2 })).code, 'UNSUPPORTED_AUDIO');
  assert.equal(inspectWav(wav({ samples: 100 })).code, 'AUDIO_TOO_SHORT');
  assert.equal(inspectWav(Buffer.from('not audio at all, really not audio at all.....')).code, 'INVALID_AUDIO');
});

test('the audio context follows the clip length within safe bounds', () => {
  assert.equal(audioContextFor(4800), 448);
  assert.equal(audioContextFor(16000 * 5), 448);
  assert.equal(audioContextFor(16000 * 8), 576);
  assert.equal(audioContextFor(16000 * 15), 960);
  // A dictated note runs to 30 s: whisper's whole window.
  assert.equal(audioContextFor(16000 * 30), 1500);
  assert.equal(audioContextFor(16000 * 60), 1500);
});

test('the turbo model is preferred and small is the fallback', async () => {
  const workdir = await mkdtemp(path.join(os.tmpdir(), 'aura-speech-'));
  try {
    for (const [installed, expected] of [[WHISPER_MODELS, WHISPER_MODELS[0]], [[WHISPER_MODELS[1]], WHISPER_MODELS[1]]]) {
      const calls = [];
      const speech = createSpeechEngines({
        whisperPath: 'whisper-cli.exe', workdir,
        exists: (candidate) => candidate === 'whisper-cli.exe' || installed.some((name) => candidate.endsWith(name)),
        spawnImpl: fakeSpawn(calls, async () => ({ stdout: ' Γεια.\n' })),
      });
      await speech.transcribe({ wav: wav(), lang: 'el' });
      assert.ok(calls[0].args[1].endsWith(expected), `uses ${expected}`);
    }
  } finally {
    await rm(workdir, { recursive: true, force: true });
  }
});

test('transcripts drop whisper markers and control characters', () => {
  assert.equal(cleanTranscript(' [BLANK_AUDIO]\n Γεια σου,\u0007 τι κάνεις; (μουσική) \n'), 'Γεια σου, τι κάνεις;');
});

test('recognition runs whisper with an argument array and removes the audio file', async () => {
  const calls = [];
  const workdir = await mkdtemp(path.join(os.tmpdir(), 'aura-speech-'));
  try {
    const speech = createSpeechEngines({
      whisperPath: 'whisper-cli.exe', modelPath: 'model.bin', workdir, exists: () => true,
      spawnImpl: fakeSpawn(calls, async () => ({ stdout: ' Βάλε χρονόμετρο δέκα λεπτά.\n' })),
    });
    const result = await speech.transcribe({ wav: wav(), lang: 'el' });
    assert.deepEqual(result, { ok: true, text: 'Βάλε χρονόμετρο δέκα λεπτά.' });
    assert.equal(calls[0].command, 'whisper-cli.exe');
    assert.equal(calls[0].options.shell, false);
    assert.deepEqual(calls[0].args.slice(0, 2), ['-m', 'model.bin']);
    assert.equal(calls[0].args[calls[0].args.indexOf('-l') + 1], 'el');
    assert.equal(calls[0].args[calls[0].args.indexOf('-ac') + 1], '448', 'a short clip gets the smallest safe context');
    const audio = calls[0].args[calls[0].args.indexOf('-f') + 1];
    assert.equal(existsSync(audio), false, 'the temporary WAV is removed');
    assert.equal((await speech.transcribe({ wav: wav(), lang: 'xx' })).code, 'INVALID_LANGUAGE');
    const missing = createSpeechEngines({ workdir, exists: () => false, spawnImpl: fakeSpawn([], async () => ({})) });
    assert.equal((await missing.transcribe({ wav: wav(), lang: 'el' })).code, 'STT_UNAVAILABLE');
  } finally {
    await rm(workdir, { recursive: true, force: true });
  }
});

test('synthesis passes the text as Base64 and returns the voice WAV', async () => {
  const calls = [];
  const workdir = await mkdtemp(path.join(os.tmpdir(), 'aura-speech-'));
  try {
    const speech = createSpeechEngines({
      workdir, exists: () => true, powershell: 'pwsh-fake',
      spawnImpl: fakeSpawn(calls, async (command, args) => {
        if (args.some((arg) => arg.endsWith('voices.ps1'))) return { stdout: 'el-GR\r\nen-US\r\n' };
        await writeFile(args[args.indexOf('-OutFile') + 1], wav({ samples: 1600 }));
        return {};
      }),
    });
    const health = await speech.health();
    assert.equal(health.stt_provider, 'local');
    assert.equal(health.tts_el, true);
    assert.equal(health.tts_en, true);
    assert.equal(health.tts_de, false);
    const result = await speech.synthesize({ text: 'Καλημέρα "AURA"; rm -rf', lang: 'el' });
    assert.equal(result.ok, true);
    assert.equal(result.wav.toString('ascii', 0, 4), 'RIFF');
    const speak = calls.find((call) => call.args.some((arg) => arg.endsWith('speak.ps1')));
    const encoded = speak.args[speak.args.indexOf('-TextBase64') + 1];
    assert.equal(Buffer.from(encoded, 'base64').toString('utf8'), 'Καλημέρα "AURA"; rm -rf');
    assert.equal(speak.args[speak.args.indexOf('-Language') + 1], 'el-GR');
    const english = await speech.synthesize({ text: 'Good morning AURA', lang: 'en' });
    assert.equal(english.ok, true);
    const englishSpeak = calls.filter((call) => call.args.some((arg) => arg.endsWith('speak.ps1'))).at(-1);
    assert.equal(englishSpeak.args[englishSpeak.args.indexOf('-Language') + 1], 'en-US');
    assert.equal((await speech.synthesize({ text: 'Hallo', lang: 'de' })).code, 'TTS_UNAVAILABLE');
    assert.equal((await speech.synthesize({ text: 'x'.repeat(2001), lang: 'el' })).code, 'TEXT_TOO_LARGE');
  } finally {
    await rm(workdir, { recursive: true, force: true });
  }
});

test('neural synthesis uses only allowlisted voices and returns bounded MP3', async () => {
  const calls = [];
  const mp3 = Buffer.concat([Buffer.from('ID3', 'ascii'), Buffer.alloc(512, 0x41)]);
  const speech = createSpeechEngines({
    ttsProvider: 'edge-neural', python: 'python-fake', neuralTool: 'neural_tts.py',
    neuralPythonPath: 'edge-package',
    exists: (candidate) => candidate === 'neural_tts.py' || candidate.endsWith('edge_tts\\__init__.py') ||
      candidate.endsWith('edge_tts/__init__.py'),
    spawnImpl: fakeSpawn(calls, async () => ({ stdout: mp3 })),
  });
  const health = await speech.health();
  assert.equal(health.stt_provider, 'local');
  assert.equal(health.tts_provider, 'edge-neural');
  assert.equal(health.tts_neural, true);
  assert.equal(health.tts_el, true);
  assert.equal(health.tts_en, true);

  const result = await speech.synthesize({ text: 'Καλημέρα AURA', lang: 'el', voice: 'nestoras' });
  assert.equal(result.ok, true);
  assert.equal(result.contentType, 'audio/mpeg');
  assert.deepEqual(result.audio, mp3);
  assert.equal(result.providerVoice, NEURAL_VOICE_PROFILES.nestoras.el);
  assert.equal(calls[0].command, 'python-fake');
  assert.equal(calls[0].options.shell, false);
  assert.equal(calls[0].options.env.PYTHONPATH, 'edge-package');
  assert.equal(Object.hasOwn(calls[0].options.env, 'GROQ_API_KEY'), false);
  assert.equal(calls[0].args[calls[0].args.indexOf('--voice') + 1], 'el-GR-NestorasNeural');
  const encoded = calls[0].args[calls[0].args.indexOf('--text-base64') + 1];
  assert.equal(Buffer.from(encoded, 'base64').toString('utf8'), 'Καλημέρα AURA');

  const beforeInvalid = calls.length;
  assert.equal((await speech.synthesize({ text: 'Γεια', lang: 'el', voice: 'unknown' })).code, 'INVALID_VOICE');
  assert.equal(calls.length, beforeInvalid, 'invalid profiles never reach the helper process');
});

test('Azure Speech uses the regional REST endpoint, escaped SSML and an allowlisted voice', async () => {
  const calls = [];
  const mp3 = Buffer.concat([Buffer.from('ID3', 'ascii'), Buffer.alloc(512, 0x42)]);
  const speech = createSpeechEngines({
    ttsProvider: 'azure', azureSpeechKey: 'test-only-azure-key', azureSpeechRegion: 'WestEurope',
    fetchImpl: async (url, options) => {
      calls.push({ url, options });
      return new Response(mp3, { status: 200, headers: { 'content-type': 'audio/mpeg' } });
    },
    exists: () => false,
  });
  const health = await speech.health();
  assert.equal(health.stt_provider, 'local');
  assert.equal(health.tts_provider, 'azure');
  assert.equal(health.tts_neural, true);
  assert.equal(health.tts_el, true);
  assert.equal(health.tts_en, true);

  const result = await speech.synthesize({ text: 'AURA <test> & "safe"', lang: 'el', voice: 'athina' });
  assert.equal(result.ok, true);
  assert.deepEqual(result.audio, mp3);
  assert.equal(result.providerVoice, NEURAL_VOICE_PROFILES.athina.el);
  assert.equal(calls.length, 1);
  assert.equal(calls[0].url, 'https://westeurope.tts.speech.microsoft.com/cognitiveservices/v1');
  assert.equal(calls[0].url.includes('test-only-azure-key'), false);
  assert.equal(calls[0].options.headers['Ocp-Apim-Subscription-Key'], 'test-only-azure-key');
  assert.equal(calls[0].options.headers['X-Microsoft-OutputFormat'], 'audio-24khz-48kbitrate-mono-mp3');
  assert.match(calls[0].options.body, /name="el-GR-AthinaNeural"/);
  assert.match(calls[0].options.body, /AURA &lt;test&gt; &amp; &quot;safe&quot;/);
  assert.equal(calls[0].options.body.includes('<test>'), false);
});

test('Azure Speech rejects non-Speech endpoints and maps provider authentication failures', async () => {
  let calls = 0;
  const rejected = createSpeechEngines({
    ttsProvider: 'azure', azureSpeechKey: 'test-only-azure-key',
    azureSpeechEndpoint: 'https://example.services.ai.azure.com/api/projects/Aura',
    fetchImpl: async () => { calls += 1; return new Response(null, { status: 200 }); },
    exists: () => false,
  });
  assert.equal((await rejected.health()).tts_el, false);
  assert.equal((await rejected.synthesize({ text: 'hello', lang: 'el' })).code, 'TTS_UNAVAILABLE');
  assert.equal(calls, 0, 'a Foundry/project endpoint is never contacted as Speech');

  const authFailure = createSpeechEngines({
    ttsProvider: 'azure', azureSpeechKey: 'test-only-azure-key',
    azureSpeechEndpoint: 'https://aura-test.cognitiveservices.azure.com',
    fetchImpl: async () => new Response(null, { status: 401 }),
    exists: () => false,
  });
  assert.equal((await authFailure.synthesize({ text: 'hello', lang: 'en' })).code, 'PROVIDER_AUTH_FAILED');
});

test('optional Groq STT uploads one bounded WAV with no secret in the URL', async () => {
  const calls = [];
  const engine = createSpeechEngines({
    sttProvider: 'groq', groqApiKey: 'test-only-key',
    fetchImpl: async (url, options) => {
      calls.push({ url, options });
      return { ok: true, status: 200, json: async () => ({ text: 'Καλημέρα AURA' }) };
    },
    // Avoid invoking Windows voice discovery in this test.
    spawnImpl: () => { throw new Error('not used'); },
    exists: () => false,
  });
  const audio = wav({ samples: 8000 });
  const result = await engine.transcribe({ wav: audio, lang: 'el' });
  assert.deepEqual(result, { ok: true, text: 'Καλημέρα AURA' });
  assert.equal(calls.length, 1);
  assert.equal(calls[0].url, 'https://api.groq.com/openai/v1/audio/transcriptions');
  assert.equal(calls[0].options.headers.authorization, 'Bearer test-only-key');
  assert.equal(calls[0].url.includes('test-only-key'), false);
  assert.equal(calls[0].options.body.get('model'), 'whisper-large-v3-turbo');
  assert.equal(calls[0].options.body.get('language'), 'el');
});

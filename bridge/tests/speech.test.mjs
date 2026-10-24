import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { existsSync } from 'node:fs';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { PassThrough } from 'node:stream';
import { test } from 'node:test';
import { audioContextFor, cleanTranscript, createSpeechEngines, inspectWav, WHISPER_MODELS } from '../src/speech.mjs';

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
  assert.equal(audioContextFor(16000 * 15), 768);
  assert.equal(audioContextFor(16000 * 60), 768);
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
    assert.equal(health.tts_el, true);
    assert.equal(health.tts_de, false);
    const result = await speech.synthesize({ text: 'Καλημέρα "AURA"; rm -rf', lang: 'el' });
    assert.equal(result.ok, true);
    assert.equal(result.wav.toString('ascii', 0, 4), 'RIFF');
    const speak = calls.find((call) => call.args.some((arg) => arg.endsWith('speak.ps1')));
    const encoded = speak.args[speak.args.indexOf('-TextBase64') + 1];
    assert.equal(Buffer.from(encoded, 'base64').toString('utf8'), 'Καλημέρα "AURA"; rm -rf');
    assert.equal(speak.args[speak.args.indexOf('-Language') + 1], 'el-GR');
    assert.equal((await speech.synthesize({ text: 'Hallo', lang: 'de' })).code, 'TTS_UNAVAILABLE');
    assert.equal((await speech.synthesize({ text: 'x'.repeat(2001), lang: 'el' })).code, 'TEXT_TOO_LARGE');
  } finally {
    await rm(workdir, { recursive: true, force: true });
  }
});

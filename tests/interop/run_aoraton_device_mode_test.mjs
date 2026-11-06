// node tests/interop/run_aoraton_device_mode_test.mjs <path to aura_aoraton_device_mode_host.exe>
// The simulator signs in the way the device does (ADR 0010), end to end: the
// real PC helper relays Google's code over the transfer session, the device
// mode's own account worker redeems it with its PKCE verifier at the local fake
// Supabase, and after a restart the stored settings and session sign it in
// again without the PC. Nothing secret reaches either program's output.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { createServer } from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createInterface } from 'node:readline';
import { signInDevice } from '../../scripts/aoraton-device-sign-in.mjs';
import { ANON_KEY, AUTH_CODE, startFakeSupabase } from './aoraton_fake_supabase.mjs';

const exe = process.argv[2];
assert.ok(exe && existsSync(exe), 'the device mode host is required');

function freePort() {
  return new Promise((resolve, reject) => {
    const server = createServer();
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => {
      const { port } = server.address();
      server.close(() => resolve(port));
    });
  });
}

function startDevice(env) {
  const child = spawn(exe, [], { env, stdio: ['ignore', 'pipe', 'pipe'] });
  const lines = [];
  let output = '';
  child.stderr.on('data', (chunk) => { output += chunk; });
  const ready = new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('the device mode did not start')), 20000);
    createInterface({ input: child.stdout }).on('line', (line) => {
      lines.push(line);
      output += `${line}\n`;
      const match = /^READY (http:\/\/127\.0\.0\.1:\d+) (\d{6})$/.exec(line);
      if (match) {
        clearTimeout(timer);
        resolve({ address: match[1], pin: match[2] });
      }
    });
    child.once('exit', (code) => reject(new Error(`the device mode exited (${code})`)));
  });
  return { child, lines, ready, output: () => output };
}

async function waitFor(lines, test, ms = 20000) {
  for (let waited = 0; waited < ms && !lines.some(test); waited += 50) await new Promise((r) => setTimeout(r, 50));
  return lines.some(test);
}

const fake = await startFakeSupabase();
const folder = await mkdtemp(join(tmpdir(), 'aura-device-mode-'));
const env = {
  ...process.env,
  AURA_AORATON_SIGN_IN: 'device',
  AURA_AORATON_ALLOW_LOOPBACK: '1',
  AURA_AORATON_DEVICE_STORE: join(folder, 'device.dpapi'),
  AURA_AORATON_DEVICE_SETTINGS: join(folder, 'device-settings.txt'),
  AURA_AORATON_CALLBACK_PORT: String(await freePort()),
};
const config = { url: `http://127.0.0.1:${fake.port}`, anon_key: ANON_KEY, inco_enabled: false };
const outputs = [];
try {
  // First boot: no settings yet. The PC helper sends them and relays the sign-in.
  const first = startDevice(env);
  try {
    const { address, pin } = await first.ready;
    assert.ok(first.lines.includes('MODE device'), 'AURA_AORATON_SIGN_IN=device selects the device mode');
    assert.ok(!first.lines.includes('CONFIGURED'), 'without settings the account does not start');
    const log = [];
    const opened = [];
    const result = await signInDevice({
      address, code: pin, config, allowLoopbackProject: true, log: (line) => log.push(line),
      // The stand-in browser: the fake's authorize page redirects to the helper's
      // loopback callback with the code.
      openBrowser: async (url) => {
        opened.push(url);
        const response = await fetch(url);
        assert.equal(response.status, 200, 'the callback page answers');
      },
    });
    assert.equal(result.signedIn, true, `signed in (${result.error ?? 'no error'})`);
    assert.equal(opened.length, 1, 'one browser page');
    assert.ok(await waitFor(first.lines, (line) => line === 'CONFIGURED'), 'the settings started the account');
    assert.ok(await waitFor(first.lines, (line) => line === 'SIGNED_IN'), 'the device mode is signed in');
    assert.ok(await waitFor(first.lines, (line) => line === 'ROOMS 2'), 'and lists the account rooms');
    assert.ok(fake.state.challenge, 'the fake checked the device mode PKCE verifier');
    outputs.push(log.join('\n'), first.output());
  } finally {
    first.child.kill();
  }
  const settings = await readFile(env.AURA_AORATON_DEVICE_SETTINGS, 'utf8');
  assert.ok(settings.includes(config.url), 'the settings are stored, as in the device NVS');
  assert.ok(existsSync(env.AURA_AORATON_DEVICE_STORE), 'the session is in its own vault');

  // Second boot: the stored settings and session sign it in without the PC.
  const second = startDevice(env);
  try {
    await second.ready;
    assert.ok(await waitFor(second.lines, (line) => line === 'SIGNED_IN'), 'signed in again after a restart');
    outputs.push(second.output());
  } finally {
    second.child.kill();
  }
  const printed = outputs.join('\n');
  for (const secret of [AUTH_CODE, 'at-', 'rt-', ANON_KEY]) {
    assert.ok(!printed.includes(secret), 'no code, token or key in the output');
  }
  console.log('aoraton device mode interop passed');
} finally {
  await fake.close();
  await rm(folder, { recursive: true, force: true });
}

// node tests/interop/run_aoraton_device_mode_test.mjs <path to aura_aoraton_device_mode_host.exe>
// The simulator signs in the way the device does (ADR 0010), end to end: the
// real PC helper relays Google's code over the transfer session, the device
// mode's own account worker redeems it with its PKCE verifier at the local fake
// Supabase, and after a restart the stored settings and session sign it in
// again without the PC. Then the phone's way, with no PC at all: the project's
// values built in, the device's QR, Google's page, aoraton.com's relay and back
// to the device.
// Nothing secret reaches either program's output.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { createServer } from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createInterface } from 'node:readline';
import { signInDevice } from '../../scripts/aoraton-device-sign-in.mjs';
import { ACCESS_TOKEN_PREFIX, ANON_KEY, AUTH_CODE, REFRESH_TOKEN_PREFIX, SITE_URL, acceptedRedirect,
  startFakeSupabase } from './aoraton_fake_supabase.mjs';

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

function startDevice(env, args = []) {
  const child = spawn(exe, args, { env, stdio: ['ignore', 'pipe', 'pipe'] });
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
  // The phone's way (no PC): the project's values are built in (the board's
  // from config/aoraton.local.json; here the environment, as dev-simulator.ps1
  // sets it), the owner taps sign-in, and a stand-in phone follows the QR.
  const phoneFolder = await mkdtemp(join(tmpdir(), 'aura-device-phone-'));
  const phoneEnv = {
    ...env,
    AURA_AORATON_DEVICE_STORE: join(phoneFolder, 'device.dpapi'),
    AURA_AORATON_DEVICE_SETTINGS: join(phoneFolder, 'device-settings.txt'),
    AURA_AORATON_URL: config.url,
    AURA_AORATON_ANON_KEY: ANON_KEY,
  };
  const phone = startDevice(phoneEnv, ['--phone']);
  try {
    const { address } = await phone.ready;
    assert.ok(await waitFor(phone.lines, (line) => line === 'CONFIGURED'),
      'the built-in values start the account without the PC');
    assert.ok(await waitFor(phone.lines, (line) => line.startsWith('PHONE ')), 'the device shows the phone QR');
    const qr = phone.lines.find((line) => line.startsWith('PHONE ')).slice(6);
    assert.match(qr, /^http:\/\/127\.0\.0\.1:\d+\/aoraton\/phone\?s=[A-Za-z0-9_-]{22}$/, 'the QR');
    assert.ok(qr.startsWith(`${address}/aoraton/phone?s=`), 'at the transfer server');
    // The phone opens the QR: a short wait, then Google's page (the fake's).
    let google = '';
    for (let tries = 0; tries < 100 && !google; tries += 1) {
      const page = await fetch(qr, { redirect: 'manual' });
      assert.equal(page.status, 200, 'the QR page answers');
      const match = /<meta http-equiv="refresh" content="0;url=([^"]+)">/.exec(await page.text());
      if (match) google = match[1].replaceAll('&amp;', '&');
      else await new Promise((resolve) => setTimeout(resolve, 100));
    }
    assert.ok(google.startsWith(`${config.url}/auth/v1/authorize?`), 'the phone goes on to Google');
    const authorize = await fetch(google, { redirect: 'manual' });
    assert.equal(authorize.status, 302, 'Google sends the phone back');
    // Supabase returns a phone to no IP address but 127.0.0.1 (as the fake does
    // now), so it returns to aoraton.com's relay page, which knows the device by
    // its address.
    assert.equal(acceptedRedirect('http://192.168.2.5:8788/aoraton/callback?s=x'), SITE_URL,
      'a return address on the local network is refused');
    assert.equal(acceptedRedirect('http://127.0.0.1:4779/aoraton/callback'), 'http://127.0.0.1:4779/aoraton/callback',
      'loopback is not');
    const back = new URL(authorize.headers.get('location'));
    assert.equal(`${back.origin}${back.pathname}`, 'https://aoraton.com/aura-phone/', "to aoraton.com's relay");
    assert.equal(`http://${back.searchParams.get('d')}`, address, 'which knows the device by its address');
    assert.ok(back.searchParams.get('code'), 'with the code');
    // The relay (aoraton's public/aura-phone/relay.js, tested there) hands the
    // device its secret and the code over the local network.
    const forward = new URLSearchParams({ s: back.searchParams.get('s'), code: back.searchParams.get('code') });
    const done = await fetch(`${address}/aoraton/callback?${forward}`, { redirect: 'manual' });
    assert.equal(done.status, 200, 'the device takes the code');
    assert.ok(!(await done.text()).includes(AUTH_CODE), 'and does not show it');
    assert.ok(await waitFor(phone.lines, (line) => line === 'SIGNED_IN'), 'the phone signed the device in');
    assert.ok(await waitFor(phone.lines, (line) => line === 'ROOMS 2'), 'and it lists the account rooms');
    assert.ok(!existsSync(phoneEnv.AURA_AORATON_DEVICE_SETTINGS), 'no PC settings were needed');
    const again = await fetch(qr, { redirect: 'manual' });
    assert.equal(again.status, 404, 'the QR is spent');
    outputs.push(phone.output());
  } finally {
    phone.child.kill();
    await rm(phoneFolder, { recursive: true, force: true });
  }

  const printed = outputs.join('\n');
  const leaked = (text) => [AUTH_CODE, ACCESS_TOKEN_PREFIX, REFRESH_TOKEN_PREFIX, ANON_KEY]
    .filter((secret) => text.includes(secret));
  // The output holds random base64url text, the QR's secret and the PKCE
  // challenge in the helper's link, which can spell a token's prefix by chance
  // (CI failed so at 0cb39f2). Such text is no token.
  assert.deepEqual(leaked('PHONE http://127.0.0.1:8788/aoraton/phone?s=Q9at-Wx7rt-K2mZp0LqY4b'), [],
    'random base64url text is no token');
  assert.deepEqual(leaked(`Bearer ${fake.state.access}`), [ACCESS_TOKEN_PREFIX], 'a real token is one');
  assert.deepEqual(leaked(printed), [], 'no code, token or key in the output');
  console.log('aoraton device mode interop passed');
} finally {
  await fake.close();
  await rm(folder, { recursive: true, force: true });
}

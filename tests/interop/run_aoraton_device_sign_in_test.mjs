// node tests/interop/run_aoraton_device_sign_in_test.mjs <path to aura_aoraton_link_host.exe>
// The PC helper (scripts/aoraton-device-sign-in.mjs) signs a device in through
// the real transfer protocol: the simulator's socket server with the shared
// AoratonDeviceLink stands in for the device. The device's authorize URL
// opens in a stand-in browser, the code comes back on 127.0.0.1 and reaches
// the device exactly once, an invite follows, and nothing secret is printed.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { createServer } from 'node:net';
import { createInterface } from 'node:readline';
import { signInDevice } from '../../scripts/aoraton-device-sign-in.mjs';

const exe = process.argv[2];
assert.ok(exe && existsSync(exe), 'the device stand-in executable is required');
const CODE = 'DEMO-code-123456';
const INVITE = 'https://aoraton.com/#room=DEMO&key=v2.demo-room&k=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA';
const ANON = `eyJhbGciOiJIUzI1NiJ9.${'a'.repeat(120)}.DEMO_signature-0`;
const CONFIG = { url: 'https://demo-project.supabase.co', anon_key: ANON, inco_enabled: false };

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

function startDevice(callbackPort) {
  const child = spawn(exe, [CODE, INVITE, String(callbackPort)], { stdio: ['ignore', 'pipe', 'inherit'] });
  const lines = [];
  const ready = new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('the device stand-in did not start')), 15000);
    createInterface({ input: child.stdout }).on('line', (line) => {
      lines.push(line);
      const match = /^READY (http:\/\/127\.0\.0\.1:\d+) (\d{6})$/.exec(line);
      if (match) {
        clearTimeout(timer);
        resolve({ address: match[1], pin: match[2] });
      }
    });
    child.once('exit', (code) => reject(new Error(`the device stand-in exited (${code})`)));
  });
  return { child, lines, ready };
}

async function waitFor(lines, wanted) {
  for (let i = 0; i < 100 && !lines.includes(wanted); i += 1) await new Promise((r) => setTimeout(r, 50));
  return lines.includes(wanted);
}

// The stand-in browser: Google would show its page and then redirect to the
// device's redirect_to with ?code=… (or ?error=… when the owner declines).
function browser(answer, opened) {
  return async (url) => {
    const authorize = new URL(url);
    assert.equal(authorize.origin, CONFIG.url, 'only the configured project opens in the browser');
    assert.equal(authorize.pathname, '/auth/v1/authorize');
    opened.push(url);
    const redirect = new URL(authorize.searchParams.get('redirect_to'));
    const response = await fetch(`${redirect.href}?${answer}`);
    assert.equal(response.status, 200, 'the callback page answers');
  };
}

async function scenario(name, run) {
  const callbackPort = await freePort();
  const device = startDevice(callbackPort);
  try {
    const { address, pin } = await device.ready;
    await run({ address, pin, lines: device.lines, callbackPort });
  } finally {
    device.child.kill();
  }
  console.log(`ok - ${name}`);
}

await scenario('a wrong code is refused before anything is sent', async ({ address, pin }) => {
  const wrong = pin === '000000' ? '111111' : '000000';
  await assert.rejects(signInDevice({ address, code: wrong, config: CONFIG, openBrowser: async () => {}, log: () => {} }),
    /code/);
});

await scenario('the device signs in and takes an invite', async ({ address, pin, lines }) => {
  const log = [];
  const opened = [];
  const result = await signInDevice({ address, code: pin, config: CONFIG, invite: INVITE,
    openBrowser: browser(`code=${CODE}`, opened), log: (line) => log.push(line) });
  assert.equal(result.signedIn, true, 'signed in');
  assert.equal(result.invited, true, 'invite delivered');
  assert.equal(opened.length, 1, 'one browser page');
  assert.ok(await waitFor(lines, 'CONFIG OK'), 'the device received a valid configuration');
  assert.ok(await waitFor(lines, 'CODE_OK'), 'the device received exactly the code');
  assert.ok(await waitFor(lines, 'INVITE_OK'), 'the device received the invite');
  const printed = log.join('\n');
  for (const secret of [CODE, ANON, pin, 'v2.demo-room']) {
    assert.ok(!printed.includes(secret), 'nothing secret is printed');
  }
});

await scenario('a declined Google sign-in reaches the device as a refusal', async ({ address, pin, lines }) => {
  const result = await signInDevice({ address, code: pin, config: CONFIG,
    openBrowser: browser('error=access_denied&error_description=DEMO', []), log: () => {} });
  assert.equal(result.signedIn, false);
  assert.equal(result.error, 'refused');
  assert.ok(await waitFor(lines, 'CANCELLED'), 'the device saw the refusal');
});

await scenario('an already signed-in device only takes the invite', async ({ address, pin, lines }) => {
  await signInDevice({ address, code: pin, config: CONFIG, openBrowser: browser(`code=${CODE}`, []), log: () => {} });
  const opened = [];
  const again = await signInDevice({ address, code: pin, config: CONFIG, invite: INVITE,
    openBrowser: browser(`code=${CODE}`, opened), log: () => {} });
  assert.equal(again.signedIn, true);
  assert.equal(opened.length, 0, 'no second browser page');
  assert.ok(await waitFor(lines, 'INVITE_OK'), 'the invite still arrives');
});

console.log('aoraton device sign-in interop passed');

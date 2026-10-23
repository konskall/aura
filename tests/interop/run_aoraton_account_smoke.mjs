// node tests/interop/run_aoraton_account_smoke.mjs <path to aura_aoraton_account_smoke.exe>
// Starts the loopback fake Supabase, runs the native smoke against it and checks
// the result, the bytes that reached the "server" and that nothing secret leaked
// into the program's output.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { ALPHA, ANON_KEY, startFakeSupabase } from './aoraton_fake_supabase.mjs';

const exe = process.argv[2];
assert.ok(exe && existsSync(exe), 'smoke executable path is required');
const fake = await startFakeSupabase();
const report = join(tmpdir(), `aura-aoraton-smoke-${process.pid}.json`);
const store = join(tmpdir(), `aura-aoraton-smoke-${process.pid}.dpapi`);
const callbackPort = 47000 + (process.pid % 900);

let output = '';
const exitCode = await new Promise((resolve) => {
  const child = spawn(exe, [], {
    shell: false,
    env: {
      ...process.env,
      AURA_AORATON_URL: `http://127.0.0.1:${fake.port}`,
      AURA_AORATON_ANON_KEY: ANON_KEY,
      AURA_AORATON_ALLOW_LOOPBACK: '1',
      AURA_AORATON_CALLBACK_PORT: String(callbackPort),
      AURA_AORATON_BROWSER: 'fetch',
      AURA_AORATON_STORE: store,
      AURA_AORATON_SMOKE_REPORT: report,
      AURA_AORATON_SMOKE_PASSPHRASE_B64: Buffer.from(fake.passphrase, 'utf8').toString('base64url'),
    },
  });
  child.stdout.on('data', (chunk) => { output += chunk; });
  child.stderr.on('data', (chunk) => { output += chunk; });
  const timer = setTimeout(() => child.kill(), 120000);
  child.on('close', (code) => { clearTimeout(timer); resolve(code); });
});

try {
  const result = JSON.parse(await readFile(report, 'utf8'));
  assert.equal(exitCode, 0, `smoke exited ${exitCode}: ${JSON.stringify(result)}`);
  assert.deepEqual(
    { signed_in: result.signed_in, rooms: result.rooms, unlocked: result.unlocked, sent: result.sent,
      incoming_notified: result.incoming_notified, store_written: result.store_written, signed_out: result.signed_out },
    { signed_in: true, rooms: 2, unlocked: true, sent: 'Acknowledged', incoming_notified: 1, store_written: true,
      signed_out: true });
  assert.ok(result.history >= 1, 'history fetched');
  assert.equal(fake.state.lastSentPlaintext, 'DEMO από το AURA', 'the server received v2 ciphertext AURA encrypted');
  assert.equal(fake.state.rows.filter((row) => row.room_key === ALPHA && row.uid === 'user-1').length, 1,
    'exactly one send');
  assert.equal(fake.state.logouts, 1, 'sign-out told the server');
  assert.ok(!existsSync(store), 'sign-out erased the DPAPI store');
  for (const secret of ['at-', 'rt-', 'code-1', fake.passphrase, 'DEMO από το AURA']) {
    assert.ok(!output.includes(secret), 'no token, code, passphrase or message text in the program output');
  }
  console.log(`aoraton account smoke passed (${fake.state.log.length} requests to the loopback fake)`);
} finally {
  await rm(report, { force: true });
  await rm(store, { force: true });
  await fake.close();
}

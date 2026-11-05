// node tests/interop/run_update_signing_test.mjs <path to aura_update_signature_tests.exe>
// The owner's tooling and the device agree: a manifest that
// scripts/update-signing.mjs signs passes the native check (the simulator's
// BCrypt verifier, which runs the same manifest parser as the firmware), and a
// changed manifest or another key's signature does not.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { existsSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { keygen, signManifest } from '../../scripts/update-signing.mjs';

const exe = process.argv[2];
assert.ok(exe && existsSync(exe), 'verifier executable path is required');
const directory = mkdtempSync(join(tmpdir(), 'aura-update-signing-'));
try {
  const key = join(directory, 'owner.pem');
  const publicKey = keygen(key);
  assert.match(publicKey, /^04[0-9a-f]{128}$/, 'the public key is an uncompressed P-256 point');
  assert.throws(() => keygen(key), 'an existing key is never replaced');
  const image = join(directory, 'aura.bin');
  writeFileSync(image, Buffer.from('AURA firmware image for the signing test'));
  const manifest = signManifest({ image, version: '13.1.0', url: 'https://updates.example/aura.bin', key,
    notes: 'Test' });

  const check = (body, keyHex) => {
    const file = join(directory, 'manifest.json');
    writeFileSync(file, JSON.stringify(body));
    const result = spawnSync(exe, [file, keyHex], { encoding: 'utf8', shell: false });
    return result.stdout.trim();
  };
  assert.equal(check(manifest, publicKey), 'OK', 'the device accepts the owner\'s signed manifest');
  assert.equal(check({ ...manifest, version: '13.2.0' }, publicKey), 'MANIFEST_BAD_SIGNATURE',
    'a changed version is refused');
  assert.equal(check({ ...manifest, sha256: manifest.sha256.replace(/^./, (c) => (c === '0' ? '1' : '0')) },
    publicKey), 'MANIFEST_BAD_SIGNATURE', 'a changed image hash is refused');
  const otherKey = keygen(join(directory, 'other.pem'));
  assert.equal(check(manifest, otherKey), 'MANIFEST_BAD_SIGNATURE', 'another key does not verify');
  const { signature, ...unsigned } = manifest;
  assert.ok(signature);
  assert.equal(check(unsigned, publicKey), 'MANIFEST_UNSIGNED', 'an unsigned manifest is refused');
  assert.equal(check(manifest, ''), 'UPDATE_KEY_MISSING', 'a build without a key installs nothing');
  console.log('update signing interop: PASS');
} finally {
  rmSync(directory, { recursive: true, force: true });
}

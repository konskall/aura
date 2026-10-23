// Synthetic aoraton key-vault vectors, produced with WebCrypto exactly as the
// browser's utils/vault.ts seals them: PBKDF2-SHA256 → AES-256-GCM (no AAD),
// wire `v2:<b64url iv>:<b64url ct+tag>`. Nothing here is a real key or room.
//   node tests/interop/make_aoraton_vault_vectors.mjs           writes the fixture
//   node tests/interop/make_aoraton_vault_vectors.mjs --check   verifies it
import assert from 'node:assert/strict';
import { readFile, writeFile } from 'node:fs/promises';
import { webcrypto } from 'node:crypto';

const fixtureUrl = new URL('../fixtures/aoraton_vault_vectors.json', import.meta.url);
const encoder = new TextEncoder();
const b64url = (bytes) => Buffer.from(bytes).toString('base64url');
const filled = (value, size) => new Uint8Array(size).fill(value);

async function vaultKey(passphrase, salt, iterations) {
  const material = await webcrypto.subtle.importKey('raw', encoder.encode(passphrase), 'PBKDF2', false, ['deriveKey']);
  return webcrypto.subtle.deriveKey({ name: 'PBKDF2', hash: 'SHA-256', salt, iterations }, material,
    { name: 'AES-GCM', length: 256 }, false, ['encrypt', 'decrypt']);
}

async function seal(key, ivByte, plaintext) {
  const iv = filled(ivByte, 12);
  const ct = new Uint8Array(await webcrypto.subtle.encrypt({ name: 'AES-GCM', iv }, key, encoder.encode(plaintext)));
  return `v2:${b64url(iv)}:${b64url(ct)}`;
}

const passphrase = 'AURA synthetic vault ✓';
const salt = Uint8Array.from({ length: 16 }, (_, index) => index);
const iterations = 1000;
const key = await vaultKey(passphrase, salt, iterations);
const otherKey = await vaultKey('a different synthetic passphrase', salt, iterations);
const rooms = [
  { room_key: 'v2.QUFBQUFBQUFBQUFBQUFBQUFB', rk: filled(0x11, 32), name: 'DEMO Alpha' },
  { room_key: 'demo_room_1234', rk: filled(0x22, 32), name: 'DEMO Beta' },
];

const fixture = {
  note: 'Synthetic vectors only (tests/interop/make_aoraton_vault_vectors.mjs).',
  passphrase,
  kdf_salt: b64url(salt),
  kdf_iters: iterations,
  check_blob: await seal(key, 1, 'inco-vault-check-v1'),
  rows: [
    ...(await Promise.all(rooms.map(async (room, index) => ({
      room_key: room.room_key,
      blob: await seal(key, 2 + index, JSON.stringify({ v: 1, rk: b64url(room.rk), name: room.name, pin: '' })),
    })))),
    {
      room_key: 'v2.WldaWldaWldaWldaWldaWlda',
      blob: await seal(otherKey, 9, JSON.stringify({ v: 1, rk: b64url(filled(0x33, 32)), name: 'DEMO other', pin: '' })),
    },
  ],
  expected: rooms.map((room) => ({ room_key: room.room_key, rk_hex: Buffer.from(room.rk).toString('hex') })),
};

const text = `${JSON.stringify(fixture, null, 2)}\n`;
if (process.argv.includes('--check')) {
  // A Windows checkout (core.autocrlf) may store the fixture with CRLF.
  const stored = (await readFile(fixtureUrl, 'utf8')).replace(/\r\n/g, '\n');
  assert.equal(stored, text, 'vault fixture matches WebCrypto output');
  console.log('aoraton vault vectors verified');
} else {
  await writeFile(fixtureUrl, text, 'utf8');
  console.log('aoraton vault vectors written');
}

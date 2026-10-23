import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { webcrypto } from 'node:crypto';

const fixtureUrl = new URL('../fixtures/aoraton_e2ee_v2_vectors.json', import.meta.url);
const fixture = JSON.parse(await readFile(fixtureUrl, 'utf8'));
const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true });

const fromHex = (hex) => Uint8Array.from(hex.match(/../g).map((byte) => Number.parseInt(byte, 16)));
const fromBase64Url = (value) => new Uint8Array(Buffer.from(value, 'base64url'));
const toBase64Url = (value) => Buffer.from(value).toString('base64url');

const plaintext = String.fromCodePoint(...fixture.plaintext_codepoints.map((value) => Number.parseInt(value, 16)));
const roomKey = fromHex(fixture.room_key_hex);
const iv = fromBase64Url(fixture.iv_base64url);
const imported = await webcrypto.subtle.importKey('raw', roomKey, 'HKDF', false, ['deriveKey', 'deriveBits']);

async function deriveAes(info) {
  return webcrypto.subtle.deriveKey(
    { name: 'HKDF', hash: 'SHA-256', salt: new Uint8Array(32), info: encoder.encode(info) },
    imported,
    { name: 'AES-GCM', length: 256 },
    false,
    ['encrypt', 'decrypt'],
  );
}

async function deriveBits(info) {
  return new Uint8Array(await webcrypto.subtle.deriveBits(
    { name: 'HKDF', hash: 'SHA-256', salt: new Uint8Array(32), info: encoder.encode(info) },
    imported,
    256,
  ));
}

const aes = await deriveAes('inco/e2ee/v2/msg');
const encrypted = new Uint8Array(await webcrypto.subtle.encrypt(
  { name: 'AES-GCM', iv }, aes, encoder.encode(plaintext),
));
const wire = `v2:${toBase64Url(iv)}:${toBase64Url(encrypted)}`;
assert.equal(wire, fixture.wire, 'WebCrypto encryption must match the CNG known-answer vector');

const [version, encodedIv, encodedCiphertext] = fixture.wire.split(':');
assert.equal(version, 'v2');
const decrypted = await webcrypto.subtle.decrypt(
  { name: 'AES-GCM', iv: fromBase64Url(encodedIv) },
  aes,
  fromBase64Url(encodedCiphertext),
);
assert.equal(decoder.decode(decrypted), plaintext, 'WebCrypto must decrypt the device vector');
assert.equal(Buffer.from(encoder.encode(plaintext)).toString('hex'), fixture.plaintext_utf8_hex);
assert.equal(toBase64Url(await deriveBits('inco/e2ee/v2/auth')), fixture.room_key_verifier);
assert.equal(toBase64Url(await deriveBits('inco/e2ee/v2/safety')), fixture.safety_material);

const pinInput = `inco/e2ee/v2/pin|${fixture.pin_room_input.toLowerCase().trim()}|${fixture.pin_input.trim()}`;
const pinDigest = new Uint8Array(await webcrypto.subtle.digest('SHA-256', encoder.encode(pinInput)));
assert.equal(toBase64Url(pinDigest), fixture.pin_verifier);

const tampered = fromBase64Url(encodedCiphertext);
tampered[tampered.length - 1] ^= 0x01;
await assert.rejects(
  webcrypto.subtle.decrypt({ name: 'AES-GCM', iv: fromBase64Url(encodedIv) }, aes, tampered),
  /operation|decrypt/i,
  'WebCrypto must reject a corrupted authentication tag',
);

const randomIvOne = webcrypto.getRandomValues(new Uint8Array(12));
const randomIvTwo = webcrypto.getRandomValues(new Uint8Array(12));
assert.notDeepEqual(randomIvOne, randomIvTwo, 'browser CSPRNG must issue fresh IVs');

console.log('aoraton_webcrypto_oracle: PASS');

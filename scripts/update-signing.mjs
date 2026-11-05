#!/usr/bin/env node
// The owner's update signing key and signed update manifests.
//
//   node scripts/update-signing.mjs keygen <private key file outside the repository>
//     Makes a P-256 key pair. It saves the private key (PEM) and prints the
//     CONFIG_AURA_OTA_PUBLIC_KEY line for firmware/sdkconfig.defaults.
//   node scripts/update-signing.mjs sign --image <firmware .bin> --version <x.y.z>
//       --url <https://...> --key <private key file> --out <manifest.json> [--notes <text>]
//     Writes the manifest the device fetches, signed over the version, the
//     size and the SHA-256 (AURA-OTA-1).
//
// The private key never enters the repository. Keep a copy offline: without
// it no further update can be signed for devices built with its public key.
import { createHash, createPrivateKey, generateKeyPairSync, sign } from 'node:crypto';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, relative, resolve, isAbsolute } from 'node:path';
import { fileURLToPath } from 'node:url';

const repository = resolve(dirname(fileURLToPath(import.meta.url)), '..');

export function signingMessage(version, size, sha256) {
  return `AURA-OTA-1\n${version}\n${size}\n${sha256.toLowerCase()}\n`;
}

function insideRepository(path) {
  const fromRepository = relative(repository, resolve(path));
  return fromRepository === '' || (!fromRepository.startsWith('..') && !isAbsolute(fromRepository));
}

function fail(message) {
  throw new Error(message);
}

export function publicKeyHex(publicKey) {
  const jwk = publicKey.export({ format: 'jwk' });
  return Buffer.concat([Buffer.from([4]), Buffer.from(jwk.x, 'base64url'), Buffer.from(jwk.y, 'base64url')])
    .toString('hex');
}

export function keygen(path) {
  if (!path) fail('give the private key file: keygen <file outside the repository>');
  if (insideRepository(path)) fail('the private key must be stored outside the repository');
  if (existsSync(path)) fail(`${path} exists; refusing to replace a signing key`);
  const { privateKey, publicKey } = generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
  writeFileSync(path, privateKey.export({ format: 'pem', type: 'pkcs8' }), { mode: 0o600, flag: 'wx' });
  return publicKeyHex(publicKey);
}

export function signManifest({ image, version, url, key, notes = '' }) {
  if (!/^\d+\.\d+\.\d+$/.test(version ?? '')) fail('--version must look like 13.1.0');
  if (!(url ?? '').startsWith('https://')) fail('--url must be an https:// address');
  if (!key || insideRepository(key)) fail('--key must be the private key file outside the repository');
  const bytes = readFileSync(image);
  const sha256 = createHash('sha256').update(bytes).digest('hex');
  const privateKey = createPrivateKey(readFileSync(key));
  const signature = sign('sha256', Buffer.from(signingMessage(version, bytes.length, sha256)),
    { key: privateKey, dsaEncoding: 'ieee-p1363' }).toString('hex');
  return { version, url, size: bytes.length, sha256, notes, signature };
}

function options(argumentsList) {
  const values = {};
  for (let index = 0; index < argumentsList.length; index += 2) {
    const name = argumentsList[index];
    if (!name.startsWith('--') || index + 1 >= argumentsList.length) fail(`unexpected argument ${name}`);
    values[name.slice(2)] = argumentsList[index + 1];
  }
  return values;
}

function main() {
  const [command, ...rest] = process.argv.slice(2);
  if (command === 'keygen') {
    const hex = keygen(rest[0]);
    console.log('Private key saved. Keep an offline copy; it never goes into the repository.');
    console.log('Add this line to firmware/sdkconfig.defaults:');
    console.log(`CONFIG_AURA_OTA_PUBLIC_KEY="${hex}"`);
  } else if (command === 'sign') {
    const values = options(rest);
    if (!values.image || !values.out) fail('sign needs --image and --out');
    const manifest = signManifest(values);
    writeFileSync(values.out, `${JSON.stringify(manifest)}\n`);
    console.log(`Signed ${manifest.version}: ${manifest.size} bytes, SHA-256 ${manifest.sha256}`);
  } else {
    fail('commands: keygen <file> | sign --image <bin> --version <x.y.z> --url <https://...> --key <file> --out <json>');
  }
}

if (resolve(process.argv[1] ?? '') === fileURLToPath(import.meta.url)) {
  try {
    main();
  } catch (error) {
    console.error(`update-signing: ${error.message}`);
    process.exit(2);
  }
}

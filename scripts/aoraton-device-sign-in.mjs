#!/usr/bin/env node
// Signs the AURA device in to the owner's aoraton.com account (ADR 0010).
//
//   node scripts/aoraton-device-sign-in.mjs --address http://<device>:<port> --code <six digits> [--invite <link>]
//
// Open Settings → Μεταφορά αρχείων on the device first: it shows the address
// and the code. The device creates the PKCE verifier and keeps the session and
// the room keys. This PC only sends the project's public values from
// config/aoraton.local.json, opens Google's page in its browser and hands the
// one-time code back over the code-protected session. Nothing secret is
// printed. Afterwards unlock the room keys on the device itself (Messages →
// account → Room keys); an invite link can come from here with --invite.
import { spawn } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const ADDRESS_RE = /^http:\/\/(\d{1,3}\.){3}\d{1,3}:\d{1,5}$/;
const CODE_RE = /^\d{6}$/;
const CALLBACK_PATH = '/aoraton/callback';
const DEFAULT_TIMEOUTS = { request: 15000, start: 20000, browser: 300000, finish: 60000 };

export class DeviceSignInError extends Error {}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

async function post(address, path, body, token, timeoutMs) {
  const headers = { 'Content-Type': 'application/json' };
  if (token) headers.Authorization = `Bearer ${token}`;
  const response = await fetch(`${address}${path}`, {
    method: 'POST', headers, body: JSON.stringify(body), signal: AbortSignal.timeout(timeoutMs),
  });
  let json = null;
  try {
    json = await response.json();
  } catch {
    json = null;
  }
  return { status: response.status, json };
}

// The device builds the authorize URL from its own settings; only the
// configured project, and a return address on this PC's loopback, are opened.
function callbackPort(authorizeUrl, projectUrl) {
  let authorize;
  let redirect;
  try {
    authorize = new URL(authorizeUrl);
    redirect = new URL(authorize.searchParams.get('redirect_to') ?? '');
  } catch {
    throw new DeviceSignInError('The device sent an unexpected sign-in address.');
  }
  if (authorize.origin !== new URL(projectUrl).origin || authorize.pathname !== '/auth/v1/authorize' ||
      redirect.protocol !== 'http:' || redirect.hostname !== '127.0.0.1' || redirect.pathname !== CALLBACK_PATH ||
      !redirect.port) {
    throw new DeviceSignInError('The device sent an unexpected sign-in address.');
  }
  return Number(redirect.port);
}

// One-shot listener for Google's redirect back to 127.0.0.1.
function listenForCallback(port, timeoutMs) {
  let settle;
  const result = new Promise((resolve) => { settle = resolve; });
  let done = false;
  const server = createServer((request, response) => {
    const url = new URL(request.url ?? '/', `http://127.0.0.1:${port}`);
    if (request.method !== 'GET' || url.pathname !== CALLBACK_PATH) {
      response.writeHead(404).end();
      return;
    }
    const code = url.searchParams.get('code') ?? '';
    response.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' });
    response.end('<!doctype html><meta charset="utf-8"><title>AURA</title>' +
      '<p>AURA: η σύνδεση συνεχίζει στη συσκευή. Μπορείς να κλείσεις αυτή την καρτέλα.</p>' +
      '<p>AURA: the sign-in continues on the device. You can close this tab.</p>');
    finish(code ? { code, refused: false } : { code: '', refused: true });
  });
  const timer = setTimeout(() => finish(null), timeoutMs);
  function finish(value) {
    if (done) return;
    done = true;
    clearTimeout(timer);
    server.close();
    settle(value);
  }
  const ready = new Promise((resolve, reject) => {
    server.once('error', () => {
      clearTimeout(timer);
      done = true;
      reject(new DeviceSignInError(`Port ${port} on this PC is busy (the simulator's own Aoraton sign-in?). ` +
        'Close it and try again.'));
    });
    server.listen(port, '127.0.0.1', resolve);
  });
  return { ready, result };
}

export function openInBrowser(url) {
  const [command, args] = process.platform === 'win32' ? ['rundll32', ['url.dll,FileProtocolHandler', url]]
    : process.platform === 'darwin' ? ['open', [url]] : ['xdg-open', [url]];
  const child = spawn(command, args, { stdio: 'ignore', detached: true, shell: false });
  child.on('error', () => {});
  child.unref();
}

export async function signInDevice({ address, code, config, invite, openBrowser = openInBrowser,
  log = (line) => console.log(line), timeouts = {} }) {
  const limits = { ...DEFAULT_TIMEOUTS, ...timeouts };
  if (!ADDRESS_RE.test(address ?? '')) {
    throw new DeviceSignInError('Give the address the device shows, for example http://192.168.1.23:8788');
  }
  if (!CODE_RE.test(code ?? '')) throw new DeviceSignInError('The code is the six digits the device shows.');
  if (typeof config?.url !== 'string' || !config.url.startsWith('https://') || typeof config.anon_key !== 'string') {
    throw new DeviceSignInError('config/aoraton.local.json needs the project url and anon_key (scripts/setup-aoraton.ps1).');
  }
  let session;
  try {
    session = await post(address, '/api/session', { pin: code }, null, limits.request);
  } catch {
    throw new DeviceSignInError('The device cannot be reached. Open File transfer on the device and check its address.');
  }
  if (session.status !== 200 || typeof session.json?.token !== 'string') {
    throw new DeviceSignInError('The device refused the code. Check the six digits it shows.');
  }
  const token = session.json.token;
  const call = (path, body) => post(address, path, body, token, limits.request);
  const status = async () => (await call('/api/aoraton/status', {})).json ?? {};

  const configured = await call('/api/aoraton/config',
    { url: config.url, anon_key: config.anon_key, inco_enabled: config.inco_enabled === true });
  if (configured.status !== 200) {
    throw new DeviceSignInError('The device did not accept the project settings in config/aoraton.local.json.');
  }

  let state = await status();
  let signedIn = state.state === 'signed_in';
  let error;
  if (signedIn) {
    log('The device is already signed in to aoraton.com.');
  } else {
    if ((await call('/api/aoraton/sign-in', {})).status !== 200) {
      throw new DeviceSignInError('The device could not start the sign-in.');
    }
    const startBy = Date.now() + limits.start;
    while (state.state !== 'waiting') {
      if (state.state === 'failed' || Date.now() > startBy) {
        throw new DeviceSignInError('The device did not start the sign-in. Try again.');
      }
      await sleep(200);
      state = await status();
    }
    const listener = listenForCallback(callbackPort(state.authorize_url, config.url), limits.browser);
    await listener.ready;
    log('Google sign-in opens in this PC\'s browser. Finish it there.');
    log(`If no browser opens, visit: ${state.authorize_url}`);
    await openBrowser(state.authorize_url);
    const answer = await listener.result;
    if (!answer || answer.refused) {
      error = answer ? 'refused' : 'timeout';
      await call('/api/aoraton/code', { error: answer ? 'access_denied' : 'timeout' });
    } else {
      const delivered = await call('/api/aoraton/code', { code: answer.code });
      answer.code = '';
      if (delivered.status !== 200) {
        throw new DeviceSignInError('The device was no longer waiting for this sign-in. Start it again.');
      }
      const finishBy = Date.now() + limits.finish;
      do {
        await sleep(200);
        state = await status();
      } while (state.state !== 'signed_in' && state.state !== 'failed' && Date.now() < finishBy);
      signedIn = state.state === 'signed_in';
      if (!signedIn) error = typeof state.error === 'string' && state.error ? state.error.toLowerCase() : 'unconfirmed';
    }
  }

  let invited = false;
  if (invite && signedIn) {
    invited = (await call('/api/aoraton/invite', { invite })).status === 200;
    log(invited ? 'The device took the invite link.' : 'The device did not take the invite link: is it a whole aoraton invite?');
  }
  if (signedIn) {
    log('The device is signed in. Unlock its room keys on the device: Messages → account → Room keys.');
  } else {
    log(error === 'refused' ? 'The sign-in was declined in the browser.'
      : error === 'timeout' ? 'No answer came from the browser within five minutes.'
        : `The device did not complete the sign-in (${error}).`);
  }
  return error ? { signedIn, invited, error } : { signedIn, invited };
}

function parseArguments(argv) {
  const options = {};
  for (let index = 0; index < argv.length; index += 2) {
    const name = argv[index];
    const value = argv[index + 1];
    if (!['--address', '--code', '--invite'].includes(name) || value === undefined) {
      throw new DeviceSignInError('Usage: node scripts/aoraton-device-sign-in.mjs --address http://<device>:<port> ' +
        '--code <six digits> [--invite <link>]');
    }
    options[name.slice(2)] = value;
  }
  return options;
}

function readConfig() {
  const file = join(dirname(fileURLToPath(import.meta.url)), '..', 'config', 'aoraton.local.json');
  try {
    return JSON.parse(readFileSync(file, 'utf8'));
  } catch {
    throw new DeviceSignInError('config/aoraton.local.json is missing: run scripts\\setup-aoraton.ps1 once.');
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try {
    const options = parseArguments(process.argv.slice(2));
    const result = await signInDevice({ ...options, config: readConfig() });
    process.exitCode = result.signedIn ? 0 : 1;
  } catch (error) {
    console.error(error instanceof DeviceSignInError ? error.message : 'The sign-in stopped unexpectedly.');
    process.exitCode = 1;
  }
}

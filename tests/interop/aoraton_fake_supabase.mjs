// A loopback stand-in for the aoraton Supabase project, covering exactly the
// endpoints AURA's account worker uses. Synthetic data only; it never proxies
// anywhere and binds 127.0.0.1.
import http from 'node:http';
import { createHash, webcrypto } from 'node:crypto';
import { readFile } from 'node:fs/promises';

const encoder = new TextEncoder();
const b64url = (bytes) => Buffer.from(bytes).toString('base64url');
export const USER_ID = 'user-1';
export const ANON_KEY = 'anon-key-synthetic';
export const ALPHA = 'v2.QUFBQUFBQUFBQUFBQUFBQUFB';
export const BETA = 'demo_room_1234';
export const INCO_UID = '00000000-0000-0000-0000-000000000000';
export const INCO_ANSWER = 'DEMO inco: γεια σου';

async function messageKey(rkByte) {
  const raw = new Uint8Array(32).fill(rkByte);
  const imported = await webcrypto.subtle.importKey('raw', raw, 'HKDF', false, ['deriveKey']);
  return webcrypto.subtle.deriveKey(
    { name: 'HKDF', hash: 'SHA-256', salt: new Uint8Array(32), info: encoder.encode('inco/e2ee/v2/msg') },
    imported, { name: 'AES-GCM', length: 256 }, false, ['encrypt', 'decrypt']);
}

async function seal(rkByte, text) {
  const iv = webcrypto.getRandomValues(new Uint8Array(12));
  const key = await messageKey(rkByte);
  const ct = new Uint8Array(await webcrypto.subtle.encrypt({ name: 'AES-GCM', iv }, key, encoder.encode(text)));
  return `v2:${b64url(iv)}:${b64url(ct)}`;
}

export async function open(rkByte, wire) {
  const [, iv, ct] = wire.split(':');
  const key = await messageKey(rkByte);
  const plain = await webcrypto.subtle.decrypt({ name: 'AES-GCM', iv: Buffer.from(iv, 'base64url') }, key,
    Buffer.from(ct, 'base64url'));
  return new TextDecoder().decode(plain);
}

export async function startFakeSupabase() {
  const vault = JSON.parse(await readFile(new URL('../fixtures/aoraton_vault_vectors.json', import.meta.url), 'utf8'));
  const state = {
    generation: 0, access: '', refresh: '', challenge: '', redirect: '', seconds: 0, nextId: 0,
    rows: [], log: [], logouts: 0, incoRequests: [], incoInserts: [],
  };
  const rkFor = (roomKey) => (roomKey === ALPHA ? 0x11 : 0x22);
  const stamp = () => {
    const s = state.seconds++;
    return `2026-10-23T10:${String(Math.floor(s / 60)).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}.5+00:00`;
  };
  const addRow = async (roomKey, uid, username, text, extra = {}) => {
    const row = { id: `m-${++state.nextId}`, room_key: roomKey, uid, username, text: await seal(rkFor(roomKey), text),
      type: 'text', created_at: stamp(), client_msg_id: null, ...extra };
    state.rows.push(row);
    return row;
  };
  await addRow(ALPHA, 'u-maria', 'Maria', 'DEMO καλημέρα');

  const session = () => {
    state.generation += 1;
    state.access = `at-${state.generation}`;
    state.refresh = `rt-${state.generation}`;
    return { access_token: state.access, token_type: 'bearer', expires_in: 3600, refresh_token: state.refresh,
      user: { id: USER_ID, email: 'owner@example.com', user_metadata: { full_name: 'Demo Owner' } } };
  };
  const readBody = (request) => new Promise((resolve) => {
    let data = '';
    request.on('data', (chunk) => { data += chunk; });
    request.on('end', () => resolve(data));
  });
  const json = (response, status, value) => {
    const body = value === undefined ? '' : JSON.stringify(value);
    response.writeHead(status, { 'Content-Type': 'application/json' });
    response.end(body);
  };
  const rooms = [
    { room_key: ALPHA, room_name: 'DEMO Alpha', display_name: null, e2ee: true, expires_at: null, created_by: USER_ID,
      ai_enabled: true, ai_avatar_url: null },
    { room_key: BETA, room_name: 'DEMO Beta', display_name: null, e2ee: true, expires_at: null, created_by: 'u-other',
      ai_enabled: false, ai_avatar_url: null },
  ];

  const server = http.createServer(async (request, response) => {
    const url = new URL(request.url, 'http://127.0.0.1');
    const body = await readBody(request);
    state.log.push(`${request.method} ${url.pathname}`);
    if (request.method === 'GET' && url.pathname === '/auth/v1/authorize') {
      if (url.searchParams.get('provider') !== 'google' || url.searchParams.get('code_challenge_method') !== 's256') {
        return json(response, 400, { error: 'bad authorize' });
      }
      state.challenge = url.searchParams.get('code_challenge');
      state.redirect = url.searchParams.get('redirect_to');
      response.writeHead(302, { Location: `${state.redirect}?code=code-1` });
      return response.end();
    }
    if (request.headers.apikey !== ANON_KEY) return json(response, 401, { message: 'no apikey' });
    if (request.method === 'POST' && url.pathname === '/auth/v1/token') {
      const payload = JSON.parse(body || '{}');
      if (url.searchParams.get('grant_type') === 'pkce') {
        const expected = createHash('sha256').update(payload.code_verifier ?? '').digest('base64url');
        if (payload.auth_code !== 'code-1' || expected !== state.challenge) return json(response, 400, { error: 'invalid_grant' });
        return json(response, 200, session());
      }
      if (url.searchParams.get('grant_type') === 'refresh_token') {
        if (payload.refresh_token !== state.refresh) return json(response, 400, { error: 'invalid_grant' });
        return json(response, 200, session());
      }
    }
    if (request.method === 'POST' && url.pathname === '/auth/v1/logout') {
      state.logouts += 1;
      return json(response, 204);
    }
    if (request.headers.authorization !== `Bearer ${state.access}`) return json(response, 401, { message: 'JWT expired' });
    if (request.method === 'GET' && url.pathname === '/rest/v1/rooms') {
      const createdBy = url.searchParams.get('created_by');
      if (createdBy) return json(response, 200, rooms.filter((room) => `eq.${room.created_by}` === createdBy));
      const keys = (url.searchParams.get('room_key') ?? '').replace(/^in\.\(|\)$/g, '').split(',')
        .map((key) => key.replace(/^"|"$/g, ''));
      return json(response, 200, rooms.filter((room) => keys.includes(room.room_key)));
    }
    if (request.method === 'GET' && url.pathname === '/rest/v1/subscribers') {
      return json(response, 200, [{ room_key: BETA }]);
    }
    if (request.method === 'POST' && url.pathname === '/rest/v1/rpc/room_overview') {
      const items = JSON.parse(body).p_items;
      return json(response, 200, items.map(({ room_key: key, since }) => {
        const rows = state.rows.filter((row) => row.room_key === key);
        const last = rows.at(-1);
        return { room_key: key, unread_count: rows.filter((row) => row.uid !== USER_ID && (!since || row.created_at > since)).length,
          last_message_at: last?.created_at ?? null, last_text: last?.text ?? null,
          last_username: last?.username ?? null, last_type: last?.type ?? null };
      }));
    }
    if (request.method === 'GET' && url.pathname === '/rest/v1/messages') {
      const key = (url.searchParams.get('room_key') ?? '').replace(/^eq\./, '');
      return json(response, 200, state.rows.filter((row) => row.room_key === key).slice(-50).reverse());
    }
    if (request.method === 'POST' && url.pathname === '/functions/v1/inco-ai') {
      state.incoRequests.push(JSON.parse(body));
      // A short think, so the client's longer inco wait is exercised.
      await new Promise((resolve) => setTimeout(resolve, 300));
      return json(response, 200, { text: `**${INCO_ANSWER}**`, sources: [] });
    }
    if (request.method === 'POST' && url.pathname === '/rest/v1/messages') {
      const row = JSON.parse(body);
      if (row.uid === INCO_UID) {
        // RLS: inco's uid only in a room that enables inco.
        const room = rooms.find((item) => item.room_key === row.room_key);
        if (!room?.ai_enabled || request.headers.prefer !== 'return=representation') {
          return json(response, 403, { message: 'row-level security' });
        }
        const stored = { ...row, id: `m-${++state.nextId}`, created_at: stamp() };
        state.rows.push(stored);
        state.incoInserts.push(stored);
        return json(response, 201, [{ id: stored.id, created_at: stored.created_at }]);
      }
      if (request.headers.prefer !== 'return=representation' || row.type !== 'text' || row.uid !== USER_ID) {
        return json(response, 400, { message: 'bad insert' });
      }
      const stored = { ...row, id: `m-${++state.nextId}`, created_at: stamp() };
      state.rows.push(stored);
      state.lastSentPlaintext = await open(rkFor(row.room_key), row.text);
      // A reply from someone else follows the first message AURA sends.
      if (!state.replied) {
        state.replied = true;
        await addRow(row.room_key, 'u-maria', 'Maria', 'DEMO απάντηση από τη Μαρία');
      }
      return json(response, 201, [{ id: stored.id, created_at: stored.created_at }]);
    }
    if (request.method === 'GET' && url.pathname === '/rest/v1/key_vault_meta') {
      return json(response, 200, [{ kdf_salt: vault.kdf_salt, kdf_iters: vault.kdf_iters, check_blob: vault.check_blob }]);
    }
    if (request.method === 'GET' && url.pathname === '/rest/v1/key_vault') {
      return json(response, 200, vault.rows.map(({ room_key: roomKey, blob }) => ({ room_key: roomKey, blob })));
    }
    return json(response, 404, { message: 'unexpected' });
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  return {
    port: server.address().port,
    state,
    passphrase: vault.passphrase,
    close: () => new Promise((resolve) => server.close(resolve)),
  };
}

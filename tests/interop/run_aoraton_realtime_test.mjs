// Public RFC6455 framing over a loopback fake. No external WebSocket package,
// Supabase project, real account, message content or credentials are used.
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { createHash } from 'node:crypto';
import { spawn } from 'node:child_process';

let current;
let connections = 0;
let heartbeat = false;
let heartbeatRequested = false;
let failure;
const sockets = new Set();
const seenTokens = [];
function frame(text, opcode = 1, final = true) {
  const body = Buffer.from(text);
  const head = Buffer.alloc(body.length < 126 ? 2 : body.length < 65536 ? 4 : 10);
  head[0] = (final ? 0x80 : 0) | opcode;
  if (head.length === 2) head[1] = body.length;
  else if (head.length === 4) { head[1] = 126; head.writeUInt16BE(body.length, 2); }
  else { head[1] = 127; head.writeBigUInt64BE(BigInt(body.length), 2); }
  return Buffer.concat([head, body]);
}
function send(socket, data) { socket.write(frame(JSON.stringify(data))); }
function change(socket, fragmented = false) {
  const data = JSON.stringify({ topic: 'realtime:messages:alpha', event: 'postgres_changes', payload: {
    data: { schema: 'public', table: 'messages', type: 'INSERT', record: { room_key: 'alpha', id: 'DEMO-id' } },
  } });
  if (fragmented) {
    socket.write(frame(data.slice(0, 40), 1, false));
    socket.write(frame(data.slice(40), 0, true));
  } else socket.write(frame(data));
}
const server = createServer((request, response) => {
  response.writeHead(200, { 'Content-Type': 'application/json' });
  response.end(JSON.stringify({ heartbeat, connections }));
  if (request.url === '/insert') change(current, true);
  if (request.url === '/drop') current.destroy();
  if (request.url === '/heartbeat') heartbeatRequested = true;
  if (request.url === '/oversize') current.write(frame('x'.repeat(70000)));
});
server.on('upgrade', (request, socket, head) => {
  try {
    const url = new URL(request.url, 'http://127.0.0.1');
    assert.equal(url.pathname, '/realtime/v1/websocket');
    assert.equal(url.searchParams.get('apikey'), 'DEMO-anon');
    assert.equal(url.searchParams.get('vsn'), '1.0.0');
    assert.ok(!request.url.includes('jwt'));
    const accept = createHash('sha1').update(request.headers['sec-websocket-key'] + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + accept + '\r\n\r\n');
    current = socket;
    const number = ++connections;
    sockets.add(socket);
    socket.on('close', () => sockets.delete(socket));
    socket.on('error', () => {});
    let pending = head;
    const receive = (chunk) => {
      try {
        pending = Buffer.concat([pending, chunk]);
        while (pending.length >= 2) {
          const opcode = pending[0] & 15;
          let size = pending[1] & 127;
          let offset = 2;
          if (size === 126) { if (pending.length < 4) return; size = pending.readUInt16BE(2); offset = 4; }
          if (size === 127) { if (pending.length < 10) return; size = Number(pending.readBigUInt64BE(2)); offset = 10; }
          assert.ok(size < 65536);
          assert.ok(pending[1] & 0x80, 'client WebSocket frames are masked');
          if (pending.length < offset + 4 + size) return;
          const mask = pending.subarray(offset, offset + 4); offset += 4;
          const bytes = Buffer.from(pending.subarray(offset, offset + size));
          for (let i = 0; i < bytes.length; ++i) bytes[i] ^= mask[i % 4];
          pending = pending.subarray(offset + size);
          if (opcode === 8) { socket.end(frame('', 8)); return; }
          if (opcode === 9) { socket.write(frame(bytes, 10)); continue; }
          assert.equal(opcode, 1);
          const message = JSON.parse(bytes.toString('utf8'));
          if (message.event === 'phx_join') {
            assert.equal(message.payload.access_token, number < 3 ? 'DEMO-jwt-1' : 'DEMO-jwt-2');
            seenTokens.push(message.payload.access_token);
            const sub = message.payload.config.postgres_changes[0];
            assert.equal(sub.schema, 'public'); assert.equal(sub.table, 'messages'); assert.equal(sub.event, 'INSERT');
            assert.ok(['room_key=eq.alpha', 'room_key=eq.beta'].includes(sub.filter));
            send(socket, { topic: message.topic, event: 'phx_reply', ref: message.ref,
              payload: { status: 'ok', response: { postgres_changes: [{ ...sub, id: number }] } } });
          } else if (message.event === 'heartbeat') {
            assert.equal(message.topic, 'phoenix');
            send(socket, { topic: 'phoenix', event: 'phx_reply', ref: message.ref, payload: { status: 'ok', response: {} } });
            if (heartbeatRequested) { heartbeat = true; change(socket); }
          } else assert.fail('unexpected client event');
        }
      } catch (error) { failure = error; socket.destroy(); }
    };
    socket.on('data', receive);
    if (head.length) { pending = Buffer.alloc(0); receive(head); }
  } catch (error) { failure = error; socket.destroy(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
let output = '';
try {
  const code = await new Promise(resolve => {
    const child = spawn(process.argv[2], [`http://127.0.0.1:${server.address().port}`], { shell: false });
    const timer = setTimeout(() => child.kill(), 45000);
    child.stdout.on('data', chunk => { output += chunk; });
    child.stderr.on('data', chunk => { output += chunk; });
    child.on('exit', code => { clearTimeout(timer); resolve(code); });
    child.on('error', error => { failure = error; clearTimeout(timer); resolve(-1); });
  });
  if (failure) throw failure;
  assert.equal(code, 0, output);
  assert.ok(heartbeat && seenTokens.includes('DEMO-jwt-1') && seenTokens.includes('DEMO-jwt-2'));
  assert.ok(!output.includes('DEMO-jwt') && !output.includes('DEMO-anon'), 'adapter never logs authentication data');
  process.stdout.write(output);
} finally {
  for (const socket of sockets) socket.destroy();
  await new Promise(resolve => server.close(resolve));
}

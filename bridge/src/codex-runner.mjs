import { spawn } from 'node:child_process';
import { mkdir } from 'node:fs/promises';
import path from 'node:path';
import { buildChatInstructions, MAX_INSTRUCTIONS_BYTES } from './turn-prompt.mjs';

const MAX_PROMPT_BYTES = 4000;
const MAX_OUTPUT_BYTES = 12000;

function boundedEnvironment(source = process.env, extra = {}) {
  const names = [
    'PATH', 'Path', 'PATHEXT', 'SystemRoot', 'WINDIR', 'TEMP', 'TMP', 'USERPROFILE',
    'LOCALAPPDATA', 'APPDATA', 'CODEX_HOME', 'HTTP_PROXY', 'HTTPS_PROXY', 'NO_PROXY',
  ];
  const result = {};
  for (const name of names) if (source[name]) result[name] = source[name];
  return { ...result, ...extra };
}

function killProcessTree(child) {
  if (!child?.pid || child.exitCode !== null) return;
  if (process.platform === 'win32') {
    const killer = spawn('taskkill', ['/pid', String(child.pid), '/t', '/f'], {
      shell: false,
      windowsHide: true,
      stdio: 'ignore',
    });
    killer.unref();
  } else {
    try { process.kill(-child.pid, 'SIGTERM'); } catch { child.kill('SIGTERM'); }
  }
}

export function sanitizeDisplayText(value) {
  return String(value)
    .replace(/\r\n/g, '\n')
    .replace(/[\u0000-\u0008\u000B\u000C\u000E-\u001F\u007F]/g, '')
    .trim();
}

export function classifyFailure(stderr, exitCode) {
  const value = String(stderr).toLowerCase();
  if (value.includes('not logged in') || value.includes('login')) return 'NOT_LOGGED_IN';
  if (value.includes('rate limit') || value.includes('too many requests')) return 'RATE_LIMIT';
  if (value.includes('not recognized') || value.includes('enoent')) return 'CLI_NOT_INSTALLED';
  return exitCode === 0 ? 'INVALID_RESPONSE' : 'CLI_FAILED';
}

export function createCodexRunner(options = {}) {
  const command = options.command ?? process.env.AURA_CODEX_PATH ?? 'codex';
  const prefixArgs = options.prefixArgs ?? [];
  const timeoutMs = Math.min(Math.max(options.timeoutMs ?? 45000, 100), 120000);
  const workdir = options.workdir ?? process.env.AURA_BRIDGE_WORKDIR ??
    path.resolve('runtime', 'assistant_sandbox');
  const spawnImpl = options.spawnImpl ?? spawn;
  const extraEnv = options.extraEnv ?? {};

  // `instructions` is the complete text built by turn-prompt.mjs; a bare
  // `prompt` gets the default chat instructions.
  async function runTurn({ prompt, instructions, signal }) {
    if (instructions === undefined) {
      if (typeof prompt !== 'string' || Buffer.byteLength(prompt, 'utf8') === 0) {
        return { ok: false, code: 'INVALID_REQUEST' };
      }
      if (Buffer.byteLength(prompt, 'utf8') > MAX_PROMPT_BYTES) {
        return { ok: false, code: 'PROMPT_TOO_LARGE' };
      }
      instructions = buildChatInstructions({ prompt });
    } else if (typeof instructions !== 'string' || Buffer.byteLength(instructions, 'utf8') === 0) {
      return { ok: false, code: 'INVALID_REQUEST' };
    } else if (Buffer.byteLength(instructions, 'utf8') > MAX_INSTRUCTIONS_BYTES) {
      return { ok: false, code: 'PROMPT_TOO_LARGE' };
    }
    await mkdir(workdir, { recursive: true });

    const args = [
      ...prefixArgs,
      'exec', '--sandbox', 'read-only', '--ephemeral', '--ignore-user-config', '--ignore-rules',
      '--skip-git-repo-check', '--color', 'never', '-C', workdir, '-',
    ];

    return new Promise((resolve) => {
      let child;
      try {
        child = spawnImpl(command, args, {
          cwd: workdir,
          env: boundedEnvironment(process.env, extraEnv),
          shell: false,
          windowsHide: true,
          detached: process.platform !== 'win32',
          stdio: ['pipe', 'pipe', 'pipe'],
        });
      } catch {
        resolve({ ok: false, code: 'CLI_NOT_INSTALLED' });
        return;
      }

      let stdout = Buffer.alloc(0);
      let stderr = Buffer.alloc(0);
      let settled = false;
      const finish = (result) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        signal?.removeEventListener('abort', abort);
        resolve(result);
      };
      const abort = () => {
        killProcessTree(child);
        finish({ ok: false, code: 'CANCELLED' });
      };
      const timer = setTimeout(() => {
        killProcessTree(child);
        finish({ ok: false, code: 'TIMEOUT' });
      }, timeoutMs);

      signal?.addEventListener('abort', abort, { once: true });
      child.once('error', (error) => {
        finish({ ok: false, code: error?.code === 'ENOENT' ? 'CLI_NOT_INSTALLED' : 'CLI_FAILED' });
      });
      child.stdout.on('data', (chunk) => {
        if (stdout.length <= MAX_OUTPUT_BYTES) stdout = Buffer.concat([stdout, chunk]);
        if (stdout.length > MAX_OUTPUT_BYTES) {
          killProcessTree(child);
          finish({ ok: false, code: 'RESPONSE_TOO_LARGE' });
        }
      });
      child.stderr.on('data', (chunk) => {
        if (stderr.length < 16384) stderr = Buffer.concat([stderr, chunk]).subarray(0, 16384);
      });
      child.once('close', (code) => {
        if (settled) return;
        const text = sanitizeDisplayText(stdout.toString('utf8'));
        if (code === 0 && text && Buffer.byteLength(text, 'utf8') <= MAX_OUTPUT_BYTES) {
          finish({ ok: true, text });
        } else {
          finish({ ok: false, code: classifyFailure(stderr.toString('utf8'), code) });
        }
      });
      child.stdin.end(instructions, 'utf8');
      if (signal?.aborted) abort();
    });
  }

  async function probe(args, probeTimeout = 4000) {
    return new Promise((resolve) => {
      let child;
      try {
        child = spawnImpl(command, [...prefixArgs, ...args], {
          env: boundedEnvironment(), shell: false, windowsHide: true,
          stdio: ['ignore', 'pipe', 'pipe'],
        });
      } catch { resolve(false); return; }
      const timer = setTimeout(() => { killProcessTree(child); resolve(false); }, probeTimeout);
      child.once('error', () => { clearTimeout(timer); resolve(false); });
      child.once('close', (code) => { clearTimeout(timer); resolve(code === 0); });
    });
  }

  return {
    runTurn,
    async health() {
      const installed = await probe(['--version']);
      const loggedIn = installed ? await probe(['login', 'status']) : false;
      return { installed, loggedIn };
    },
  };
}

export const limits = { MAX_PROMPT_BYTES, MAX_OUTPUT_BYTES };


import { MAX_INSTRUCTIONS_BYTES } from './turn-prompt.mjs';
import { sanitizeDisplayText } from './codex-runner.mjs';

const DEFAULT_ENDPOINT = 'https://api.groq.com/openai/v1/chat/completions';
const DEFAULT_MODEL = 'openai/gpt-oss-120b';
const MAX_OUTPUT_BYTES = 12000;

function sanitizeStreamDelta(value) {
  return String(value)
    .replace(/\r\n/g, '\n')
    .replace(/[\u0000-\u0008\u000B\u000C\u000E-\u001F\u007F]/g, '');
}

function failureForStatus(status) {
  if (status === 401 || status === 403) return 'PROVIDER_AUTH_FAILED';
  if (status === 429) return 'RATE_LIMIT';
  if (status >= 400 && status < 500) return 'PROVIDER_REQUEST_FAILED';
  return 'PROVIDER_UNAVAILABLE';
}

export async function consumeGroqSse(body, onDelta) {
  if (!body?.getReader) return { ok: false, code: 'INVALID_RESPONSE' };
  const reader = body.getReader();
  const decoder = new TextDecoder('utf-8', { fatal: true });
  let pending = '';
  let answer = '';
  const consumeLine = (line) => {
    const trimmed = line.trim();
    if (!trimmed || !trimmed.startsWith('data:')) return null;
    const data = trimmed.slice(5).trim();
    if (data === '[DONE]') return 'done';
    let event;
    try { event = JSON.parse(data); } catch { return 'invalid'; }
    if (event?.error) return 'provider-error';
    const rawDelta = event?.choices?.[0]?.delta?.content;
    if (typeof rawDelta !== 'string' || rawDelta.length === 0) return null;
    const delta = sanitizeStreamDelta(rawDelta);
    if (delta.length === 0) return null;
    answer += delta;
    if (Buffer.byteLength(answer, 'utf8') > MAX_OUTPUT_BYTES) return 'too-large';
    onDelta?.(delta);
    return null;
  };
  try {
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      pending += decoder.decode(value, { stream: true });
      let newline;
      while ((newline = pending.indexOf('\n')) !== -1) {
        const result = consumeLine(pending.slice(0, newline));
        pending = pending.slice(newline + 1);
        if (result === 'too-large') return { ok: false, code: 'RESPONSE_TOO_LARGE' };
        if (result === 'invalid') return { ok: false, code: 'INVALID_RESPONSE' };
        if (result === 'provider-error') return { ok: false, code: 'PROVIDER_ERROR' };
        if (result === 'done') return answer ? { ok: true, text: sanitizeDisplayText(answer) } :
          { ok: false, code: 'INVALID_RESPONSE' };
      }
    }
    pending += decoder.decode();
    const tail = consumeLine(pending);
    if (tail === 'too-large') return { ok: false, code: 'RESPONSE_TOO_LARGE' };
    if (tail === 'invalid') return { ok: false, code: 'INVALID_RESPONSE' };
    return answer ? { ok: true, text: sanitizeDisplayText(answer) } : { ok: false, code: 'INVALID_RESPONSE' };
  } catch (error) {
    return { ok: false, code: error?.name === 'AbortError' ? 'CANCELLED' : 'INVALID_RESPONSE' };
  } finally {
    try { await reader.cancel(); } catch {}
    reader.releaseLock();
  }
}

export function createGroqRunner(options = {}) {
  const apiKey = options.apiKey ?? process.env.GROQ_API_KEY ?? '';
  const fetchImpl = options.fetchImpl ?? fetch;
  const endpoint = options.endpoint ?? DEFAULT_ENDPOINT;
  const configuredModel = options.model ?? process.env.AURA_GROQ_MODEL ?? DEFAULT_MODEL;
  const model = /^[A-Za-z0-9._/-]{1,100}$/.test(configuredModel) ? configuredModel : DEFAULT_MODEL;
  const timeoutMs = Math.min(Math.max(options.timeoutMs ?? 45000, 1000), 120000);

  async function runTurnStream({ instructions, signal, onDelta }) {
    if (!apiKey) return { ok: false, code: 'PROVIDER_NOT_CONFIGURED' };
    if (typeof instructions !== 'string' || Buffer.byteLength(instructions, 'utf8') === 0) {
      return { ok: false, code: 'INVALID_REQUEST' };
    }
    if (Buffer.byteLength(instructions, 'utf8') > MAX_INSTRUCTIONS_BYTES) {
      return { ok: false, code: 'PROMPT_TOO_LARGE' };
    }
    const controller = new AbortController();
    const abort = () => controller.abort();
    signal?.addEventListener('abort', abort, { once: true });
    const timer = setTimeout(abort, timeoutMs);
    try {
      const response = await fetchImpl(endpoint, {
        method: 'POST',
        headers: { authorization: `Bearer ${apiKey}`, 'content-type': 'application/json' },
        body: JSON.stringify({
          model,
          messages: [{ role: 'user', content: instructions }],
          stream: true,
          temperature: 0.2,
          max_completion_tokens: 1200,
        }),
        signal: controller.signal,
      });
      if (!response.ok) return { ok: false, code: failureForStatus(response.status) };
      return await consumeGroqSse(response.body, onDelta);
    } catch (error) {
      if (signal?.aborted) return { ok: false, code: 'CANCELLED' };
      if (error?.name === 'AbortError') return { ok: false, code: 'TIMEOUT' };
      return { ok: false, code: 'PROVIDER_UNAVAILABLE' };
    } finally {
      clearTimeout(timer);
      signal?.removeEventListener('abort', abort);
    }
  }

  return {
    supportsWebSearch: false,
    runTurnStream,
    runTurn: ({ instructions, signal }) => runTurnStream({ instructions, signal }),
    async health() { return { provider: 'groq', configured: Boolean(apiKey), model }; },
  };
}

export const groqDefaults = Object.freeze({ endpoint: DEFAULT_ENDPOINT, model: DEFAULT_MODEL });

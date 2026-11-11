// node tests/interop/run_turn_prompt_parity.mjs <path to aura_cloud_assistant_tests.exe>
// The device builds the same prompt as the PC bridge: for each turn, the
// bridge's validateTurn + buildInstructions (bridge/src/turn-prompt.mjs) and the
// device's build_turn_instructions (shared/core/src/turn_prompt.cpp) must agree
// byte for byte. Groq searches with its browser tool (groq-runner.mjs), so a
// web-search turn gets the search instructions on both.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { existsSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { buildInstructions, validateTurn } from '../../bridge/src/turn-prompt.mjs';

const exe = process.argv[2];
assert.ok(exe && existsSync(exe), 'test executable path is required');

const turns = [
  { prompt: 'Τι ώρα είναι;' },
  {
    prompt: ' What is\r\nthe weather?\u0001 ', language: 'en',
    context: 'Weather: DEMO sunny 22°C\nBattery: 80%',
    history: [{ q: 'Hi', a: 'Hello!' }, { q: 'Και αύριο;', a: 'Βροχή.\tΚρύο.' }],
  },
  { prompt: 'Βρες μου ένα εστιατόριο', web_search: true, context: 'Τοποθεσία: Αθήνα' },
];

const directory = mkdtempSync(join(tmpdir(), 'aura-turn-prompt-'));
try {
  for (const [index, turn] of turns.entries()) {
    const checked = validateTurn(turn);
    assert.ok(checked.ok, `turn ${index} is valid for the bridge`);
    const expected = buildInstructions(checked.value);
    const flat = { ...turn };
    delete flat.history;
    for (const [position, item] of (turn.history ?? []).entries()) {
      flat[`q${position}`] = item.q;
      flat[`a${position}`] = item.a;
    }
    const file = join(directory, `turn-${index}.json`);
    writeFileSync(file, JSON.stringify(flat));
    const result = spawnSync(exe, ['--prompt', file], { encoding: 'utf8', shell: false });
    assert.equal(result.status, 0, `turn ${index}: the device prints a prompt`);
    assert.equal(result.stdout, expected, `turn ${index}: the device's prompt is the bridge's`);
  }
  console.log(`turn prompt parity: ${turns.length} turns identical`);
} finally {
  rmSync(directory, { recursive: true, force: true });
}

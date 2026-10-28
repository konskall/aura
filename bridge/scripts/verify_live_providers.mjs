import { createGroqRunner } from '../src/groq-runner.mjs';
import { createSpeechEngines } from '../src/speech.mjs';

const groq = createGroqRunner();
const answer = await groq.runTurn({
  instructions: 'Reply with exactly: AURA provider ready',
});
if (!answer.ok) {
  console.error(`Groq live verification failed: ${answer.code}`);
  process.exit(1);
}

const speech = createSpeechEngines({ ttsProvider: 'azure' });
const voices = {};
for (const profile of ['athina', 'nestoras']) {
  const voice = await speech.synthesize({
    text: 'Η φωνή της AURA είναι έτοιμη.',
    lang: 'el',
    voice: profile,
  });
  if (!voice.ok) {
    console.error(`Azure Speech live verification failed for ${profile}: ${voice.code}`);
    process.exit(1);
  }
  voices[profile] = { voice: voice.providerVoice, audioBytes: voice.audio.length, contentType: voice.contentType };
}

console.log(JSON.stringify({
  ok: true,
  groq: { responseBytes: Buffer.byteLength(answer.text, 'utf8') },
  azureSpeech: voices,
}));

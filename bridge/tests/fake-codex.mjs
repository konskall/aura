const args = process.argv.slice(2);
if (args.includes('--version')) {
  console.log('codex-cli fake');
  process.exit(0);
}
if (args[0] === 'login' && args[1] === 'status') {
  console.log('Logged in');
  process.exit(0);
}

let input = '';
for await (const chunk of process.stdin) input += chunk;
if (input.includes('[timeout]')) await new Promise((resolve) => setTimeout(resolve, 5000));
if (input.includes('[error]')) {
  console.error('rate limit exceeded');
  process.exit(1);
}
if (input.includes('[large]')) {
  console.log('x'.repeat(13000));
  process.exit(0);
}
console.log('Δοκιμαστική ασφαλής απάντηση από fake Codex.');


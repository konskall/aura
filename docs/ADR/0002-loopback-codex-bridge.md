# ADR 0002: Loopback Codex CLI bridge

Status: accepted, 2026-10-17.

Use a dependency-light Node service for desktop development. It wraps the installed `codex exec` non-interactive command rather than exposing a shell. The production device never invokes this bridge.

The current Codex manual and local CLI help confirm stdin prompts, default/read-only sandboxing, `--ephemeral`, `--ignore-user-config`, `--ignore-rules`, output schema support, and JSONL output. A dedicated read-only invocation in an isolated directory is the narrowest practical local bridge. A future production provider requires a separately authorized service/API gateway.


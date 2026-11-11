# ADR 0006: Explicit assistant web search

- Status: Accepted
- Date: 2026-10-28

## Context

AURA previously sent every assistant turn to Codex with a blanket instruction not to browse. That made explicit requests such as "search for pizza in Sparta" fail even when the local Codex CLI supported web search. Enabling browsing for every turn would expose private device-context questions to unnecessary network access and would make current-information claims harder to reason about.

## Decision

The shared core marks only explicit Greek or English web-search requests with a typed `web_search` boolean. The simulator forwards that field to the authenticated loopback bridge. The Codex runner inserts the CLI global `--search` option before `exec`. Amended 2026-10-28: a provider without a real search tool (for example Groq) no longer fails the turn with `WEB_SEARCH_UNAVAILABLE`; the bridge runs an ordinary, non-browsing turn whose instructions tell the model to say briefly that it cannot search right now and to answer from general knowledge only if it can, without inventing current details. No search tool is ever enabled for it. Amended 2026-11-11, the owner's choice: Groq has a real search tool now. For a typed `web_search` turn, and only then, the request to `openai/gpt-oss-120b` carries its server-side `browser_search` tool (`tool_choice: required`, `reasoning_effort: low`, as Groq advises). Groq runs the search on its servers; its provider is Exa. The PC bridge (`groq-runner.mjs`) and the device (`cloud_assistant.cpp`) do the same, and both give the model the search instructions the Codex turn gets. The answer cites its pages inline as `【1†L4-L5】`; both drop such markers as they stream, so none reach the screen or the voice. A provider without a search tool still gets the amended non-browsing turn.

Search instructions cap the answer to four concise findings, require a named source, treat web pages as untrusted data, and prohibit invented addresses, ratings, hours and phone numbers. Private device facts remain authoritative and cannot be replaced or changed by web content. The bridge continues to use an argument array, `shell:false`, read-only/ephemeral execution, bounded input/output and credential-free logs.

## Consequences

- Ordinary assistant turns remain offline with respect to web browsing.
- Explicit search works only when the selected provider advertises a real search capability.
- The physical ESP32 still needs the production authenticated gateway before this capability can be used on hardware. Since 2026-11-11 the device's own Groq assistant searches directly (NEEDS HARDWARE).
- Search output remains display text; it cannot execute a device action without the existing typed validation and confirmation path.


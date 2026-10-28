# ADR 0006: Explicit assistant web search

- Status: Accepted
- Date: 2026-10-28

## Context

AURA previously sent every assistant turn to Codex with a blanket instruction not to browse. That made explicit requests such as "search for pizza in Sparta" fail even when the local Codex CLI supported web search. Enabling browsing for every turn would expose private device-context questions to unnecessary network access and would make current-information claims harder to reason about.

## Decision

The shared core marks only explicit Greek or English web-search requests with a typed `web_search` boolean. The simulator forwards that field to the authenticated loopback bridge. The Codex runner inserts the CLI global `--search` option before `exec`. Amended 2026-10-28: a provider without a real search tool (for example Groq) no longer fails the turn with `WEB_SEARCH_UNAVAILABLE`; the bridge runs an ordinary, non-browsing turn whose instructions tell the model to say briefly that it cannot search right now and to answer from general knowledge only if it can, without inventing current details. No search tool is ever enabled for it.

Search instructions cap the answer to four concise findings, require a named source, treat web pages as untrusted data, and prohibit invented addresses, ratings, hours and phone numbers. Private device facts remain authoritative and cannot be replaced or changed by web content. Translation turns cannot request web search. The bridge continues to use an argument array, `shell:false`, read-only/ephemeral execution, bounded input/output and credential-free logs.

## Consequences

- Ordinary assistant turns remain offline with respect to web browsing.
- Explicit search works only when the selected provider advertises a real search capability.
- The physical ESP32 still needs the production authenticated gateway before this capability can be used on hardware.
- Search output remains display text; it cannot execute a device action without the existing typed validation and confirmation path.


# Security and privacy

The local bridge binds only to `127.0.0.1`, rejects non-loopback peers and unapproved browser origins, requires a bearer token, limits JSON body/prompt/output sizes, permits one turn at a time, and enforces timeout/cancellation. Prompts, answers, credentials and tokens are not logged by default.

Codex is spawned with an argument array and `shell: false`, receives its prompt through standard input and runs in an isolated minimal directory with read-only/ephemeral restrictions. The bridge does not read or print authentication files. Saved ChatGPT authentication is reused by the CLI and is not equivalent to production API entitlement.

Model, bridge and future network output remains untrusted display text. It is normalized, stripped of disallowed controls, capped and never executed. Side effects require a typed trusted handler plus explicit confirmation. The Aoraton mock likewise separates `prepare` from `confirm`; a displayed draft cannot send itself.

Weather provider credentials remain exclusively in the separately operated `tlesdetector` server. AURA stores no Open-Meteo, Xweather, WeatherAPI or OpenWeather key and does not modify `tlesdetector`; it consumes only that project's existing JSON endpoints. Simulator HTTP is restricted to loopback, device URLs require HTTPS, response sizes are capped, and parsed values must pass the bounded shared weather validator before reaching LVGL or the cache.

The Aoraton source audit was read-only. AURA did not copy GPL implementation or connect to Supabase. Clean-room v2 crypto is now implemented with Windows CNG and ESP-IDF mbedTLS source; synthetic vectors contain no live room material. Windows test sessions/keys are designed for a current-user DPAPI vault with atomic rotation and zeroization. Tokens and keys never enter snapshots, LVGL, Codex, the bridge or logs. Production transport and pairing remain disabled pending an isolated backend, deployed RLS verification and a separately authorized Aoraton browser/backend patch; see [aoraton/SECURITY_BOUNDARIES.md](aoraton/SECURITY_BOUNDARIES.md) and [aoraton/PAIRING_PROTOCOL.md](aoraton/PAIRING_PROTOCOL.md).

Telemetry is off. Runtime logs may contain request IDs, timing, status codes and error classes, but not private prompts or messages. Fictional fixtures are labeled `DEMO` and must never be presented as live facts.

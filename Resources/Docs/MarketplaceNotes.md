# Fab Review Notes: Secure Editor Bridge

This document summarizes the plugin security posture and safe defaults for marketplace review.

Product framing for review:

- Secure AI control of Unreal Editor assets, Blueprints, animation references, skeleton data, and editor state through a typed localhost API.
- Editor-only, localhost-only, bearer-token-authenticated, policy-gated automation bridge.
- Optional advanced capabilities such as exec or Python are disabled by default and are not the core product surface.

## Security model

- Local-only intended usage: clients should call `http://127.0.0.1:<port>`.
- Non-loopback requests are rejected in request validation.
- Bearer token authentication is required when a token is configured.
- Optional environment override: `BAT_AUTH_TOKEN`.
- Forwarded proxy headers (`x-forwarded-for`) are rejected.
- Request body limits and rate limiting are enforced for all `/ai/*` endpoints.

## Secure defaults

Default behavior is intentionally conservative:

- `bServerEnabled=false`
- `bSafeModeEnabled=true`
- `bEnableExecRoute=false`
- `bAllowPythonExec=false`
- `bSaveTokenInProjectSettings=true`
- `MaxRequestBodyBytes=65536`
- `RateLimitPerSecond=10`
- `RateLimitBurst=20`

## Distribution model

- Plugin source is available on GitHub under the MIT License.
- The Fab listing provides a packaged, pre-built binary for convenience at $14.99.
- Both versions are feature-identical. Copies obtained through Fab are also
  subject to the license terms selected for the Fab listing.
- No feature gating, no time-limited trials, no telemetry.

## Dependencies and runtime scope

- Module type: `EditorNoCommandlet`, allowlisted for the Editor target only.
- Required built-in Unreal Engine plugins: `PCG`, `GeometryProcessing`, and `GeometryScripting`.
- BAT uses only Unreal Engine modules; it has no external SDK, third-party DLL, separate service, package-manager dependency, or required internet connection.
- The HTTP server runs in the Unreal Editor process, listens on loopback only, and is disabled by default.
- The plugin is not a runtime gameplay dependency and is not included in packaged games.

## Pricing

- Marketplace price: **$14.99** (USD)
- No subscriptions, no recurring fees.

## Safe vs unsafe mode

Safe mode (`bSafeModeEnabled=true`):

- Raw command execution is allowlist-based.
- Python execution is denied.
- Batch command requests are validated command-by-command.

Unsafe mode (`bSafeModeEnabled=false`):

- Broader command execution is allowed.
- Command separator/injection patterns are still blocked (for example newlines, `&&`, `;`, pipes).
- Python execution is still disabled unless `bAllowPythonExec=true`.

## Optional exec route policy

- `/ai/exec` is opt-in via `bEnableExecRoute=true`.
- When disabled, requests return:

```json
{ "ok": false, "requestId": "...", "data": {}, "warnings": [], "errors": [{ "code": "exec_route_disabled", "message": "...", "recoverable": false }] }
```

## Python policy

Python execution requires both:

- `bAllowPythonExec=true`
- Safe mode OFF (`bSafeModeEnabled=false`)

Error responses:

- `python_disabled`
- `python_requires_unsafe_mode`

## UI and explicit consent

The "Blueprint Automation Toolkit" editor panel exposes:

- Start/Stop server
- Safe Mode toggle
- Exec Route toggle
- Python toggle (only when safe mode is OFF)
- Rotate Token
- Copy Local Endpoint URL
- Last 20 request stats

Unsafe options require explicit confirmation dialogs.

Before first start, users are prompted:

- "This opens a local HTTP server on localhost only. Requires a token. Proceed?"

## Token handling

- If no token is configured, a strong token is generated.
- `bSaveTokenInProjectSettings=true` stores generated and rotated tokens in project settings.
- `bSaveTokenInProjectSettings=false` keeps generated and rotated tokens runtime-only for the current editor session.
- Rotate Token generates a new token and persists it only when project-setting persistence is enabled and runtime is not using ENV override.
- When `BAT_AUTH_TOKEN` is set, runtime uses ENV token and does not write it to disk.
- If project-setting persistence is enabled and a token is present in project config, that project-scoped token is a valid documented source for clients and agents.
- Otherwise, clients and agents should use an explicitly provided token or a secure source such as `BAT_AUTH_TOKEN`.
- Do not scrape per-user editor or user settings files for bearer tokens.
- Auth failure responses include `projectTokenConfigured` and `tokenSource` details so clients can explain where the active token is expected to come from.

## Example request

```http
POST /ai/exec HTTP/1.1
Host: 127.0.0.1:9876
Authorization: Bearer YOUR_TOKEN
Content-Type: application/json

{"command":"stat fps"}
```

## Logging and auditability

The plugin logs:

- server start/stop
- denied-request reasons (auth failure, non-loopback, safe-mode blocks, rate-limited, too-large)
- request stats in the UI panel (last 20 requests)

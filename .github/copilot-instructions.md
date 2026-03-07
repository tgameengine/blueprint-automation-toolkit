# Copilot Instructions: Blueprint Automation Toolkit Plugin

These instructions apply to work inside `Plugins/blueprint-automation-toolkit/`.

## Scope

- Restrict analysis and edits to `Plugins/blueprint-automation-toolkit/`.
- Do not edit files outside this plugin unless the user explicitly approves a scope exception.
- Keep docs and examples plugin-scoped.

## Architecture Rules

- Do not add a new HTTP route unless the user explicitly says: `ADD A NEW ROUTE`.
- Do not add bespoke, feature-specific request schemas for one-off behaviors.
- Prefer composing behavior from existing primitive endpoints and JSON plans.
- If a capability is missing, add at most one generic primitive endpoint to cover it.
- Keep route contracts generic and reusable; avoid corridor-specific, map-specific, or feature-specific payload shapes.

## Auth Token Handling

- If the plugin is configured to persist its token in project config, that project-scoped token is an allowed documented source.
- Otherwise, ask the user to provide the token explicitly or use an already-provided token such as `BAT_AUTH_TOKEN`.
- Do not scrape per-user editor or user settings files for bearer tokens.

## Blueprint Component Primitives

When explicitly requested, prefer these generic primitives:

- `POST /blueprint/components/add`
- `POST /blueprint/components/set`
- `POST /blueprint/components/instances/add`

Guardrails for these primitives:

- Enforce strict class allowlists.
- Enforce explicit property allowlists per class.
- Reuse existing auth, body-size, and rate-limit validation.
- Wrap edits in transaction + `Modify()` calls.
- Mark structural changes when components are added/removed.
- Support optional compile via request flag.

## Source Of Truth In Plugin

- `Plugins/blueprint-automation-toolkit/BlueprintAutomationToolkit.uplugin`
- `Plugins/blueprint-automation-toolkit/README.md`
- `Plugins/blueprint-automation-toolkit/Docs/`
- `Plugins/blueprint-automation-toolkit/Source/BlueprintAutomationToolkit/`

## Commandlet Note

- There are no commandlet source files in this plugin.
- Do not add commandlets unless explicitly requested.

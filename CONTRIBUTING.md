# Contributing

This repository is maintained as a focused Unreal Engine plugin.
Keep changes scoped to the plugin and avoid project-specific or engine-local assumptions in docs and examples.

## Workflow

1. Create a topic branch from `main`.
2. Keep changes narrow and internally consistent.
3. Build `uasEditor Win64 Development` against the host project before opening a pull request.
4. Include docs updates when route contracts, config keys, or user-facing behavior change.

## Code Expectations

- Restrict edits to plugin files.
- Prefer generic, reusable route contracts over one-off payload shapes.
- Do not add commandlets unless explicitly requested.
- Preserve existing auth, validation, and safe-mode behavior when extending routes.

## Pull Requests

- Describe the user-visible change and the technical approach.
- Call out any API, config, or migration impact.
- Include build or test validation in the pull request description.

## Security

Do not commit secrets, bearer tokens, or user-specific local settings.
If auth behavior changes, document the expected token source and persistence behavior.
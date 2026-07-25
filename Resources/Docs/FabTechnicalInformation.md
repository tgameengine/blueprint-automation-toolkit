# Fab Technical Information

## Features

- Typed localhost HTTP API for Unreal Editor automation
- Blueprint graph inspection, editing, compilation, and saving
- Reflected UObject property inspection and mutation
- Exposed UFunction invocation
- Editor-world actor spawning and destruction
- Asset, animation, skeleton, level, selection, focus, and PIE control
- Bearer-token authentication, safe mode, rate limiting, and request limits
- Extensible automation-command registry for other editor plugins

## Code Modules

- `BlueprintAutomationToolkit` - `EditorNoCommandlet` (Editor)

## Asset Counts

- Number of Blueprints: 0
- Number of C++ Classes: 3
- Network Replicated: No

## Platforms

- Supported Development Platforms:
  - Windows: Yes
  - macOS: No
  - Linux: No
- Supported Target Build Platforms: None. This is an editor-only plugin and is
  not included in packaged builds.

## Links

- Documentation:
  https://github.com/tgameengine/blueprint-automation-toolkit
- Example Project:
  https://github.com/tgameengine/blueprint-automation-toolkit/releases/download/v1.0.0/BATExampleProject-1.0.0.zip

## Important / Additional Notes

- Requires the built-in `PCG`, `GeometryProcessing`, and `GeometryScripting`
  plugins.
- Requires Unreal Engine 5.5, 5.6, 5.7, or 5.8 on Windows.
- No third-party software, external SDK, external DLL, package manager,
  subscription, telemetry, or required cloud service.
- The HTTP server is disabled by default, accepts loopback connections only,
  and requires bearer-token authentication when enabled.

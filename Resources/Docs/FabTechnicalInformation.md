# Fab Technical Information

## Features

- Typed localhost HTTP API for Unreal Editor automation
- Blueprint graph inspection, editing, compilation, and saving
- Reflected UObject property inspection and mutation
- Exposed UFunction invocation
- Editor-world actor spawning and destruction
- Asset, animation, skeleton, level, selection, focus, and PIE control
- Source-controlled model/animation/texture import with dry-run, fingerprints,
  idempotent re-import avoidance, and batch operation
- Static/Skeletal Mesh, animation, texture, material, Skeleton, and Physics
  Asset inspection
- Safe automatic asset configuration/repair and Fab-oriented validation
- Ordered synchronous/asynchronous asset pipelines with cancellation,
  progress logs, validation gates, and best-effort rollback
- Static/Skeletal Mesh showcase placement plus viewport PNG or deterministic
  animation-frame evidence capture
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
- The asset pipeline uses Unreal's active importers. FBX/OBJ have typed BAT
  options; glTF/GLB, USD, Alembic, and other allowlisted formats require the
  corresponding built-in Unreal importer to be enabled.
- Imports are restricted to configured roots/extensions/file-size limits and
  `/Game` destinations. External glTF sidecars must remain local and inside an
  allowed root.
- BAT imports existing source files; it does not generate a 3D model and does
  not launch Blender, FFmpeg, a shell, or another external executable.
- Multi-frame evidence is a codec-independent PNG sequence with a JSON manifest.
  A video encoder is intentionally not bundled or invoked.
- The HTTP server is disabled by default, accepts loopback connections only,
  and requires bearer-token authentication when enabled.

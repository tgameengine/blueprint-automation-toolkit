# Blueprint Automation Toolkit 1.0.2

Version 1.0.2 adds a native, Python-free workflow for testing live gameplay
and collecting reproducible showcase evidence from Unreal Editor.

## Highlights

- Added managed live capture sessions with start, status, and stop endpoints.
- Added real-tick PIE frame capture as PNG sequences and native Unreal Engine
  MP4 recording without FFmpeg or external processes.
- Added typed PIE keyboard input for press, release, and tap actions.
- Added runtime assertions for actor existence, actor counts, and reflected
  property values in editor or PIE worlds.
- Added structured capture manifests, progress reporting, dropped-frame
  accounting, safe output paths, duration limits, and lifecycle cleanup.
- Added multi-PIE selection for PNG capture and explicit primary-PIE handling
  for Unreal Engine's native MP4 recorder.
- Extended route discovery, authorization policies, OpenAPI documentation,
  automated tests, and advanced prompt examples.

## Compatibility

- Unreal Engine 5.5, 5.6, 5.7, and 5.8
- Windows 64-bit Unreal Editor
- Editor-only; the plugin is not included in packaged games

## Validation

- Fab-style `BuildPlugin` packaging completed successfully on UE 5.5, 5.6,
  5.7, and 5.8.
- All three focused live-automation tests passed on UE 5.8.
- The OpenAPI contract parsed successfully with all six new live-automation
  routes present.

Each Fab archive is engine-specific, sets the matching `EngineVersion` and
`Installed=true`, and includes precompiled Win64 editor binaries plus source,
configuration, content, documentation, resources, and the MIT license.

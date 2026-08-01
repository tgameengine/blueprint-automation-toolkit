# Blueprint Automation Toolkit 1.0.1

Version 1.0.1 expands BAT's asset and Blueprint automation surface while
preserving its editor-only, localhost-only security model.

## Highlights

- Added a source-asset pipeline for import, inspection, configuration,
  validation, repair, asynchronous execution, rollback, and editor evidence
  capture.
- Added Fab-oriented validation profiles and structured diagnostics for
  automated marketplace-readiness checks.
- Added `SplineMeshComponent` support to Blueprint component automation,
  including creation, mutation, and component replacement.
- Added explicit spline mesh start/end positions and tangents, plus local-space
  sampling from an existing `SplineComponent`.
- Added native C++ UMG Designer automation for creating Widget Blueprints,
  applying responsive widget hierarchies and slot layouts, compiling, saving,
  and reading normalized verification snapshots without Python.
- Added atomic, repeatable UMG layout replacement with exact widget-name
  preservation, rollback on compile/save failure, and structured diagnostics.
- Extended schema discovery, OpenAPI documentation, advanced prompt examples,
  and verified Codex workflow evidence.

## Compatibility

- Unreal Engine 5.5, 5.6, 5.7, and 5.8
- Windows 64-bit Unreal Editor
- Editor-only; the plugin is not included in packaged games

## Validation

- UE 5.5 Development Editor compilation and UE 5.6, 5.7, and 5.8 BuildPlugin
  packaging completed successfully with the native UMG surface.
- Focused native UMG automation tests passed on UE 5.5, including repeat apply,
  exact-name preservation, slot serialization, and Python-free execution.
- Focused OpenAPI contract tests passed on UE 5.5.

The Fab archives are engine-specific precompiled Win64 packages. Each archive
includes an `EngineVersion` value matching its supported Unreal Engine release,
sets `Installed` to `true`, and contains the corresponding editor binaries,
source, configuration, content, documentation, resources, and license.
Generated `Intermediate`, `Build`, and `Saved` directories are excluded.

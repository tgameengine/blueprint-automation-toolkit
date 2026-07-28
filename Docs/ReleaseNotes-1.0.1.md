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
- Extended schema discovery, OpenAPI documentation, advanced prompt examples,
  and verified Codex workflow evidence.

## Compatibility

- Unreal Engine 5.5, 5.6, 5.7, and 5.8
- Windows 64-bit Unreal Editor
- Editor-only; the plugin is not included in packaged games

## Validation

- UE 5.5, 5.6, and 5.8 BuildPlugin packaging completed successfully.
- UE 5.7 completed an isolated Development Editor build with the required
  engine dependencies.
- Focused Blueprint automation tests passed on UE 5.5.
- Focused OpenAPI contract tests passed on UE 5.5.

The downloadable archives are source-only Fab packages. They include the
plugin descriptor, source, configuration, content, documentation, resources,
and license, and exclude generated `Binaries`, `Intermediate`, `Build`, and
`Saved` directories.

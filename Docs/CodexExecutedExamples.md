# Codex Executed Examples

These examples were issued as natural-language prompts to Codex and executed
against a live Unreal Editor on July 28, 2026. Codex discovered the active
Blueprint Automation Toolkit (BAT) API, translated each prompt into typed
localhost requests, inspected the resulting editor state, and reported the
evidence below.

## Test Environment

- Unreal Engine: `5.5.4-0+UE5` source build
- BAT protocol: `1.0`
- Safe Mode: enabled
- Exec route: disabled
- Python execution: disabled
- Transport: authenticated loopback HTTP

Volatile request and job IDs are omitted from the condensed output. The bearer
token is redacted and is not stored in this repository.

## 1. Discover Capabilities and Build a Showcase Map

### Codex prompt

> Work through BAT in the open Unreal Editor. Discover the engine and safety
> gates first. Create `/Game/BAT_CodexExamples/Maps/BAT_CodexShowcase`, then
> build a centered ten-actor showcase with one floor, six pillars, one sphere
> centerpiece, and two point lights. Put every actor under
> `BAT_CodexExamples/Showcase`, tag it `BAT_CodexExample`, audit the result, and
> save only if no actor or property is rejected.

### BAT route trace

`GET /engine/discover` → `GET /ai/capabilities` →
`POST /editor/level/create` → `POST /ai/editor/layout/apply` →
`GET /jobs/{jobId}` → `POST /editor/level/audit` →
`POST /editor/level/save`

### Actual output (condensed)

```json
{
  "engine": "5.5.4-0+UE5",
  "protocol": "1.0",
  "safeMode": true,
  "execEnabled": false,
  "pythonEnabled": false,
  "layout": {
    "created": 10,
    "spawned_actors": 10,
    "rejected_actors": 0,
    "applied_properties": 38,
    "rejected_properties": 0,
    "errors": []
  },
  "audit": {
    "matchedCount": 10,
    "countsByClass": {
      "StaticMeshActor": 8,
      "PointLight": 2
    }
  },
  "save": {
    "saved": true,
    "mapSaved": true,
    "mapPackage": "/Game/BAT_CodexExamples/Maps/BAT_CodexShowcase"
  }
}
```

Included artifact:
`Examples/BATExampleProject/Content/BAT_CodexExamples/Maps/BAT_CodexShowcase.umap`

## 2. Create a Data-Driven Blueprint in One Plan

### Codex prompt

> Create
> `/Game/BAT_CodexExamples/Blueprints/BP_CodexTelemetryBeacon` as an Actor
> Blueprint in one BAT plan. Add instance-editable `DetectionRadius` and
> `ChannelName` variables, a `ReportStatus` function, and a movable
> `BeaconMesh` static mesh component using the engine cone. In `EventGraph`,
> connect BeginPlay to PrintString with `BAT Codex beacon ready`. Compile,
> inspect the schema and graph links, and save only when compilation has no
> errors.

### BAT route trace

`POST /blueprint/apply` → `POST /blueprint/compile_save` →
`POST /blueprint/schema` → `GET /blueprint/graph/links`

### Actual output (condensed)

```json
{
  "apply": {
    "created": 1,
    "variables_added": 2,
    "functions_added": 1,
    "components_added": 1,
    "components_updated": 1,
    "compiled": true,
    "errors": []
  },
  "compileSave": {
    "saved": true,
    "compileStatus": "up_to_date",
    "errorCount": 0,
    "warningCount": 0
  },
  "schema": {
    "variables": ["DetectionRadius", "ChannelName"],
    "instanceEditableVariables": 2,
    "component": "BeaconMesh",
    "function": "ReportStatus"
  },
  "eventGraph": {
    "nodeCount": 5,
    "verifiedLink": "begin_play.then -> print_ready.execute"
  }
}
```

Included artifact:
`Examples/BATExampleProject/Content/BAT_CodexExamples/Blueprints/BP_CodexTelemetryBeacon.uasset`

## 3. Generate a Spline-Driven HISM Blueprint

### Codex prompt

> Create
> `/Game/BAT_CodexExamples/Blueprints/BP_CodexSplineBridge` as an Actor
> Blueprint. Add `BridgeSpline` with five curve points. Add `DeckHISM` using
> `/Engine/BasicShapes/Cube.Cube`, generate its instances from the spline every
> 200 units, align them to the tangent, and apply a thin bridge-deck scale.
> Compile, list the resulting components, and save only with zero compiler
> errors.

### BAT route trace

`POST /blueprint/create` → `GET /jobs/{jobId}` →
`POST /blueprint/set-defaults` with `components_apply` →
`POST /blueprint/compile_save` → `POST /blueprint/apply` with
`components.list`

### Actual output (condensed)

```json
{
  "create": {
    "succeeded": true
  },
  "componentsApply": {
    "applied": ["BridgeSpline", "DeckHISM"]
  },
  "compileSave": {
    "saved": true,
    "compileStatus": "up_to_date",
    "errorCount": 0,
    "warningCount": 0
  },
  "components": [
    {"name": "BridgeSpline", "class": "SplineComponent"},
    {"name": "DeckHISM", "class": "HierarchicalInstancedStaticMeshComponent"}
  ]
}
```

Included artifact:
`Examples/BATExampleProject/Content/BAT_CodexExamples/Blueprints/BP_CodexSplineBridge.uasset`

## 4. Perform a Guarded Actor Cleanup

### Codex prompt

> Create three temporary cube actors under
> `BAT_CodexExamples/Temporary`, each tagged `BAT_CodexCleanup`. Audit that
> exact scope, preview deletion with a dry run and an explicit maximum of three,
> perform the real deletion only if the counts match, then audit again to prove
> that zero matching actors remain.

### BAT route trace

`POST /ai/editor/layout/apply` → `GET /jobs/{jobId}` →
`POST /editor/level/audit` → `POST /editor/level/destroy_actors`
with `dryRun: true` → `POST /editor/level/destroy_actors` with
`dryRun: false` → `POST /editor/level/audit`

### Actual output (condensed)

```json
{
  "spawned": 3,
  "auditBefore": {
    "matchedCount": 3,
    "labels": [
      "BAT_CODEX_TEMP_01",
      "BAT_CODEX_TEMP_02",
      "BAT_CODEX_TEMP_03"
    ]
  },
  "dryRun": {
    "matchedCount": 3,
    "destroyedCount": 0,
    "failedCount": 0
  },
  "delete": {
    "matchedCount": 3,
    "destroyedCount": 3,
    "failedCount": 0
  },
  "auditAfter": {
    "matchedCount": 0
  }
}
```

This example intentionally leaves no asset behind.

## 5. Duplicate, Configure, and Verify a Blueprint Variant

### Codex prompt

> Duplicate `BP_CodexTelemetryBeacon` as
> `BP_CodexTelemetryBeacon_Configured`. On the duplicate only, update the
> existing `BeaconMesh` in place: use the engine sphere, make it movable, move
> it to Z 125, and scale it uniformly to 1.25. Compile and save the duplicate.
> List the source and destination components afterward and prove that both
> still contain exactly one component named `BeaconMesh`.

### BAT route trace

`POST /asset/duplicate` → `POST /blueprint/apply` with `components.set` →
`POST /blueprint/compile_save` → `POST /blueprint/apply` with
`components.list` for source and destination

### Actual output (condensed)

```json
{
  "duplicate": {
    "createdCount": 1,
    "savedCount": 1
  },
  "configure": {
    "components_updated": 1,
    "compiled": true,
    "errors": []
  },
  "compileSave": {
    "saved": true,
    "errorCount": 0,
    "warningCount": 0
  },
  "sourceComponents": [
    {"name": "BeaconMesh", "class": "StaticMeshComponent"}
  ],
  "destinationComponents": [
    {"name": "BeaconMesh", "class": "StaticMeshComponent"}
  ]
}
```

Included artifact:
`Examples/BATExampleProject/Content/BAT_CodexExamples/Blueprints/BP_CodexTelemetryBeacon_Configured.uasset`

## 6. Place and Audit the Generated Blueprints

### Codex prompt

> In `BAT_CodexShowcase`, spawn one instance each of
> `BP_CodexTelemetryBeacon`, `BP_CodexTelemetryBeacon_Configured`, and
> `BP_CodexSplineBridge` at distinct locations. Audit actors whose class name
> contains `BP_Codex`, report the count per generated class, and save the map
> only if all three resolve.

### BAT route trace

`POST /actor/spawn` three times → `POST /editor/level/audit` →
`POST /editor/level/save`

### Actual output (condensed)

```json
{
  "spawned": 3,
  "audit": {
    "matchedCount": 3,
    "countsByClass": {
      "BP_CodexTelemetryBeacon_C": 1,
      "BP_CodexTelemetryBeacon_Configured_C": 1,
      "BP_CodexSplineBridge_C": 1
    }
  },
  "saved": true
}
```

The three actors are included in `BAT_CodexShowcase.umap`.

## 7. Run a Bounded PIE Lifecycle Check

### Codex prompt

> Confirm that PIE is stopped, start PIE through BAT, verify that the capability
> state reports PIE running, then stop PIE in a guaranteed cleanup step and
> verify that the final state reports PIE stopped. Do not mutate editor assets
> while PIE is active.

### BAT route trace

`GET /ai/capabilities` → `POST /pie/start` →
`GET /ai/capabilities` → `POST /pie/stop` →
`GET /ai/capabilities`

### Actual output (condensed)

```json
{
  "before": {"pieRunning": false},
  "during": {"pieRunning": true},
  "after": {"pieRunning": false}
}
```

This example intentionally leaves no asset behind.

## Observed Boundaries and Corrections

- `components_apply` supports `SplineComponent`, `InstancedStaticMeshComponent`,
  and `HierarchicalInstancedStaticMeshComponent`. It explicitly rejects
  `SplineMeshComponent`; the README and advanced prompt guide were corrected to
  match the implementation.
- Updating an existing component with `components.set` preserved the component
  name and source asset. It is used in the verified variant example instead of
  a remove-and-add replacement.
- The run exposed map-save extension handling in the generic asset saver. The
  implementation now detects world packages and writes `.umap`, and the level
  save route directly saves the active map before optional dirty content
  packages.

## Repository Artifact Verification

The source changes and committed example artifacts were verified after the
prompt run:

```json
{
  "pluginBuild": {
    "engine": "5.5.4-0+UE5",
    "platform": "Win64",
    "result": "BUILD SUCCESSFUL"
  },
  "packageValidation": {
    "packagesConsidered": 4,
    "packagesLoadedAndResaved": 4,
    "errors": 0
  },
  "mapCheck": {
    "errors": 0,
    "warnings": 0
  },
  "finalMapAudit": {
    "matchedCount": 3
  },
  "levelSave": {
    "saved": true,
    "mapSaved": true
  },
  "genericAssetSave": {
    "savedCount": 1,
    "mapFiles": ["BAT_CodexShowcase.umap"]
  }
}
```

## Reproducing the Examples

1. Install or build BAT for the Unreal Engine version used by the host project.
2. Open `Examples/BATExampleProject/BATExampleProject.uproject`.
3. Configure a new bearer token locally and start the BAT server.
4. Give Codex the reusable safety preamble from
   [`AdvancedPromptExamples.md`](AdvancedPromptExamples.md), followed by one of
   the prompts above.
5. Ask Codex to return route traces, compiler diagnostics, read-back evidence,
   and save status.

The committed example defaults keep the server disabled, Safe Mode enabled,
and exec and Python disabled. Authentication remains a local user choice.

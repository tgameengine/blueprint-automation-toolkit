# AI Asset Pipeline

Blueprint Automation Toolkit exposes an editor-only, agent-oriented asset pipeline for Unreal Engine 5.5–5.8. It closes the gap between a source model on disk and a verified Unreal result:

```text
Codex prompt
  -> source validation and fingerprint
  -> Unreal importer
  -> inspection
  -> repair/configuration
  -> validation
  -> showcase placement
  -> screenshot or animation frame evidence
```

The pipeline imports existing files. It does not generate a 3D model by itself. A model generator, DCC application, or artist can produce FBX, glTF/GLB, USD, OBJ, Alembic, textures, or animation sources; BAT then owns the deterministic Unreal side of the workflow.

## Endpoints

| Endpoint | Purpose | Mutates assets | Available during PIE |
| --- | --- | ---: | ---: |
| `GET /asset/import/formats` | Discover roots, extensions, limits, and capabilities | No | Yes |
| `POST /asset/import` | Import one or more source files | Yes | No |
| `POST /asset/inspect` | Read normalized asset details and relationships | No | Yes |
| `POST /asset/configure` | Reimport, repair, and configure imported assets | Yes | No |
| `POST /asset/validate` | Produce issues, suggested actions, and scores | No | Yes |
| `POST /asset/pipeline/execute` | Run ordered asset operations synchronously or as a job | Usually | No |
| `POST /asset/showcase/capture` | Place a mesh and capture viewport evidence | Yes | No |

All routes use BAT's canonical response envelope and token/permission policy. Import, configure, pipeline, and capture require both `editor` and `filesystem` permission. Inspect and validate require `editor`.

## Security model

The importer is intentionally not an arbitrary filesystem or process-execution endpoint.

- A source must be inside an **Asset Import Allowed Root**.
- Existing paths are resolved to their final disk location before root checks, preventing junction/symlink escapes.
- Relative paths resolve from the Unreal project directory.
- With no configured roots, only the project directory is allowed.
- Destination paths are restricted to `/Game`.
- Extensions use a configurable allowlist; an empty extension list denies all imports.
- A per-file size limit is enforced before Unreal sees the source.
- A separate aggregate size limit bounds each multi-source batch.
- External glTF buffer/image sidecars are resolved locally, checked for existence and size, and required to remain inside an allowed root. Remote and escaped URIs are rejected.
- Custom factories must derive from `UFactory` and be import-related editor classes.
- Custom option objects must be import/interchange-related classes.
- Generic adapter properties must be editor-visible or Blueprint-visible.
- Import factories execute as trusted Unreal Editor code. BAT validates glTF
  sidecars directly; format-specific dependencies resolved internally by other
  importers remain subject to that importer's own behavior, so enable only
  trusted importer plugins.
- Evidence output is always relative to the project's `Saved` directory.
- BAT never launches Blender, FFmpeg, shell commands, or another arbitrary executable from the asset pipeline.

Configure these limits under:

```text
Editor Preferences
  -> Plugins
  -> Blueprint Automation Toolkit
  -> Security
  -> Asset Pipeline
```

Relevant settings:

- `AssetImportAllowedRoots`
- `AssetImportAllowedExtensions`
- `AssetImportMaxFileSizeMb`
- `AssetImportMaxBatchSizeMb`
- `AssetPipelineMaxCaptureFrames`

## Discover the active import policy

```http
GET /asset/import/formats
Authorization: Bearer <token>
```

The response reports the exact source roots, extensions, per-file and batch
size limits, common model formats, FBX modes, and pipeline feature flags active
in the current project.

## Dry-run an import

Use dry-run before a large or unfamiliar source:

```json
{
  "source": "SourceArt/Characters/Octopus.fbx",
  "destination": "/Game/Characters/Octopus",
  "dry_run": true,
  "options": {
    "import_type": "skeletal_mesh",
    "import_animations": true,
    "import_materials": true,
    "import_textures": true,
    "import_morph_targets": true,
    "create_physics_asset": true
  }
}
```

Dry-run checks path policy, file existence, extension, size, destination, and source fingerprint without creating an Unreal asset.

At pipeline level, `dry_run: true` is propagated to every step. Because a
dry-run import intentionally creates no `$imported` context, later implicit
repair, validation, inspection, showcase, or capture steps are returned as
successful `skipped` previews with an explicit reason. Steps that name an
existing asset still perform read-only inspection/validation, while configure
and showcase/capture return mutation-free previews.

## Import an animated model

```json
{
  "source": "SourceArt/Characters/Octopus.fbx",
  "destination": "/Game/Characters/Octopus",
  "expected_type": "SkeletalMesh",
  "replace_existing": false,
  "replace_existing_settings": false,
  "skip_unchanged": true,
  "save": true,
  "options": {
    "import_type": "skeletal_mesh",
    "import_mesh": true,
    "import_animations": true,
    "import_materials": true,
    "import_textures": true,
    "import_morph_targets": true,
    "create_physics_asset": true,
    "convert_scene": true,
    "convert_scene_unit": true
  }
}
```

BAT calculates an MD5 fingerprint and stores these package metadata values on imported objects:

- `BAT.SourceFile`
- `BAT.SourceFingerprint`
- `BAT.AssetPipelineVersion`

With `skip_unchanged: true`, an identical source is returned as `unchanged` instead of being reimported.

If a later source in the same batch fails policy, adapter, importer, expected-
type, or save checks, BAT performs best-effort cleanup of assets newly created
earlier in that request. It never deletes a pre-existing or replaced asset as
part of this cleanup.

`expected_type` is a primary-asset assertion, not a ban on companion assets. For
example, `SkeletalMesh` succeeds when the import also creates a Skeleton,
Physics Asset, animations, materials, and textures, but fails if no Skeletal
Mesh is produced. Each entry returns `primaryObjectPath`; the top-level response
also returns `primaryObjectPaths`, `importedObjectPaths`, and
`createdObjectPaths`.

### Batch import

Each source can override its name, expected type, factory, or import options:

```json
{
  "destination": "/Game/Environment/Ruins",
  "sources": [
    {
      "source": "SourceArt/Ruins/SM_Arch.fbx",
      "destination_name": "SM_Arch",
      "expected_type": "StaticMesh",
      "options": {
        "import_type": "static_mesh",
        "combine_meshes": true
      }
    },
    "SourceArt/Ruins/T_Arch_BaseColor.png",
    "SourceArt/Ruins/T_Arch_Normal.png"
  ],
  "skip_unchanged": true,
  "save": true
}
```

## Format behavior

FBX and OBJ use typed convenience options through `UFbxImportUI`. Other supported formats use the importer registered in the running Unreal Editor. For example, glTF/GLB commonly uses Interchange and USD requires the corresponding Unreal importer plugin to be enabled.

For `.gltf`, BAT validates local `buffers[].uri` and `images[].uri` references
before import. Embedded `data:` resources are allowed; remote, percent-escaped,
query-bearing, fragment-bearing, missing, oversized, or out-of-root sidecars
are rejected. Binary `.glb` resources are self-contained.

The format allowlist means BAT accepts the file path. It does not guarantee the current Unreal installation has a factory for that format. When no active importer accepts a source, BAT returns `import_failed` with guidance to enable the appropriate importer.

Advanced adapter fields are available for project-specific importers:

```json
{
  "source": "SourceArt/Custom/example.ext",
  "destination": "/Game/Imported",
  "factory_class": "/Script/MyImporter.MyAssetFactory",
  "factory_properties": {
    "SomeEditableFlag": true
  },
  "options_class": "/Script/MyImporter.MyImportOptions",
  "options_properties": {
    "EditableScale": 100.0
  }
}
```

BAT rejects non-import classes, abstract/deprecated classes, non-editor-visible properties, and unsupported property types.

## Inspect assets

```json
{
  "paths": [
    "/Game/Characters/Octopus/SK_Octopus.SK_Octopus",
    "/Game/Characters/Octopus/A_Swim.A_Swim"
  ],
  "include_dependencies": true,
  "include_referencers": true
}
```

Normalized inspection fields include:

- object, package, class, and dirty state
- source files and BAT provenance
- dependency and referencer packages
- Static Mesh LOD, vertex, section, Nanite, bounds, collision, and material slots
- Skeletal Mesh LOD, bone, morph target, Skeleton, Physics Asset, bounds, and material slots
- animation Skeleton, duration, and sampled key count
- texture dimensions, sRGB, and compression setting
- Sound Wave duration and channel count

## Configure and repair

### Explicit Static Mesh configuration

```json
{
  "path": "/Game/Environment/Ruins/SM_Arch.SM_Arch",
  "static_mesh": {
    "recompute_normals": false,
    "recompute_tangents": true,
    "generate_lightmap_uvs": true,
    "nanite": true,
    "create_box_collision": true,
    "build": true
  },
  "material_assignments": {
    "Stone": "/Game/Environment/Ruins/Materials/M_Stone.M_Stone"
  },
  "save": true
}
```

Material assignment keys may be material slot names, imported slot names, or numeric indices.

### Skeletal Mesh repair

```json
{
  "path": "/Game/Characters/Octopus/SK_Octopus.SK_Octopus",
  "skeletal_mesh": {
    "create_physics_asset": true
  },
  "save": true
}
```

### Safe automatic repair

```json
{
  "paths": [
    "/Game/Characters/Octopus/SK_Octopus.SK_Octopus",
    "/Game/Characters/Octopus/T_Octopus_Normal.T_Octopus_Normal"
  ],
  "mode": "safe_auto",
  "save": true
}
```

`safe_auto` currently performs conservative repairs:

- normal-named textures become non-sRGB normal maps
- Static Meshes with no simple collision receive a bounds-based box
- Skeletal Meshes with no Physics Asset receive a generated Physics Asset

Use explicit options when project-specific art direction matters.

## Validate for Fab or production

```json
{
  "paths": [
    "/Game/Characters/Octopus/SK_Octopus.SK_Octopus",
    "/Game/Characters/Octopus/A_Swim.A_Swim"
  ],
  "profile": "fab",
  "rules": {
    "require_materials": true,
    "require_source_metadata": true,
    "require_physics_asset": true,
    "minimum_bones": 8
  }
}
```

Profiles:

- `default`: core renderability and relationship checks
- `production`: strict collision, physics, and provenance defaults
- `strict`: same strict defaults for automated gates
- `fab`: strict defaults intended for marketplace evidence and QA

Required provenance, material, collision, Nanite, Skeleton, Physics Asset, bone,
and playability checks are validation errors. Strict profiles also reject
normal-named textures that still use sRGB or non-normal-map compression; the
default profile reports those two texture problems as warnings. Empty Physics
Assets are distinguished from missing Physics Assets. Every issue has a
severity, stable code, message, asset path, recoverability flag, and suggested
action when BAT can repair it. The response includes per-asset and aggregate
scores.

## Full pipeline

The pipeline passes imported object paths to later steps. Use `path: "$imported"` explicitly, or omit the target in a later step.

```json
{
  "async": true,
  "rollback_on_failure": true,
  "steps": [
    {
      "op": "import",
      "payload": {
        "source": "SourceArt/Characters/Octopus.fbx",
        "destination": "/Game/Characters/Octopus",
        "expected_type": "SkeletalMesh",
        "options": {
          "import_type": "skeletal_mesh",
          "import_animations": true,
          "import_materials": true,
          "import_textures": true,
          "create_physics_asset": true
        }
      }
    },
    {
      "op": "repair",
      "payload": {
        "path": "$imported",
        "mode": "safe_auto",
        "save": true
      }
    },
    {
      "op": "validate",
      "payload": {
        "path": "$imported",
        "profile": "fab"
      }
    },
    {
      "op": "inspect",
      "payload": {
        "path": "$imported",
        "include_dependencies": true
      }
    }
  ]
}
```

An asynchronous request returns a standard BAT job ID:

```http
GET /jobs/<jobId>
Authorization: Bearer <token>
```

Job logs contain a stable entry for every pipeline step. Cancellation is checked between steps.

### Rollback boundary

When a step fails and `rollback_on_failure` is enabled, BAT deletes assets that were newly imported by the current run. It does not restore:

- an asset replaced by `replace_existing`
- an earlier asset's already-saved configuration
- user-authored assets that existed before the request

Use `dry_run` first and avoid `replace_existing` when import atomicity matters.

## Showcase and evidence capture

```json
{
  "asset": "/Game/Characters/Octopus/SK_Octopus.SK_Octopus",
  "animation": "/Game/Characters/Octopus/A_Swim.A_Swim",
  "actor_label": "BAT_Verified_Animated_Octopus",
  "location": [0, 0, 100],
  "rotation": [0, 0, 0],
  "scale": [1, 1, 1],
  "capture": true,
  "output_folder": "BlueprintAutomationToolkit/Captures/Octopus",
  "frame_count": 90,
  "fps": 30
}
```

BAT:

1. spawns and labels the mesh actor;
2. binds the requested animation;
3. focuses the active level viewport;
4. evaluates deterministic positions across the animation;
5. writes PNG frames and `capture-manifest.json`.

A one-frame request is a screenshot. A multi-frame request is a codec-independent PNG sequence. The response sets `requiresVideoEncoding: true` for sequences. This design keeps the plugin cross-version and prevents it from launching an external encoder without user control.

Capture requires an active editor viewport and therefore is not expected to succeed under `-NullRHI`.

## Recommended Codex prompt

> Import the animated octopus from `SourceArt/Characters/Octopus.fbx` into `/Game/Characters/Octopus`. Import its Skeletal Mesh, Skeleton, animations, materials, textures, morph targets, and Physics Asset. Apply safe automatic repairs, validate with the Fab profile, inspect all dependencies, place the verified Skeletal Mesh in the current level, play its swim animation, and capture 90 evidence frames at 30 FPS. Stop if validation has errors and report every created asset and evidence path.

When a pipeline import produces multiple assets, the showcase step automatically
selects the first Skeletal Mesh (then Static Mesh) and binds the first imported
AnimSequence unless explicit `asset` or `animation` values are supplied.
Validation errors are logical pipeline failures: they stop the pipeline by
default, fail asynchronous jobs, and trigger best-effort rollback when enabled.

This prompt maps directly to a pipeline request and produces a structured prompt-to-result audit trail.

## Engine compatibility

The implementation uses editor APIs available across UE 5.5–5.8:

- `UAssetImportTask` and Asset Tools for importer dispatch
- FBX import UI for typed FBX options
- Asset Registry for discovery and relationships
- editor reimport and package save APIs
- Static/Skeletal Mesh editor data for repair and validation
- active Level Editor viewport pixel capture

Actual format availability can differ by engine installation because some format importers are optional Unreal plugins.

Repository automation includes policy/schema checks, a real PNG import-inspect-
validate round trip, an escaping glTF-sidecar rejection test, a real animated
octopus glTF-to-Skeletal-Mesh round trip, failed-batch cleanup, mutation-free
pipeline/showcase dry-run checks, and synchronous/asynchronous validation-gate
tests. Run them with:

```powershell
UnrealEditor-Cmd.exe YourProject.uproject `
  -unattended -nop4 -NullRHI `
  "-ExecCmds=Automation RunTest BlueprintAutomationToolkit.AssetPipeline.;Quit;"
```

Showcase capture itself requires a rendered active editor viewport and is
therefore intentionally excluded from `-NullRHI` evidence tests.

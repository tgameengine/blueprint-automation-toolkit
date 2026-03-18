# PCG Apply API

This document defines the proposed request schema for `POST /pcg/apply`.

The goal of this route is to give BAT one generic, reusable write surface for creating and updating PCG graph assets from a plan document.

This route is intended for graph authoring, not just volume spawning.

## Design Goals

- Keep the contract generic and reusable
- Support desired-state graph authoring for BAT-owned graphs
- Stay strict and predictable through allowlists
- Support real project mesh sets, not just sample graph overrides
- Match BAT's existing plan-centric write style

## Route

`POST /pcg/apply`

## Ownership Model

Graphs managed through this route are BAT-owned by default.

Default behavior:

- `mode = reconcile`
- `ownership = bat`

This means the request describes the intended managed graph state. BAT is allowed to add, update, and remove BAT-managed content in the target graph to match the request.

The route does not attempt to be a safe merge surface for arbitrary manual edits in the same graph in v1.

## Top-Level Request Shape

```json
{
  "graph": "/Game/PCG/Graphs/BAT_CityGraph.BAT_CityGraph",
  "options": {
    "mode": "reconcile",
    "ownership": "bat",
    "create_if_missing": true,
    "clear_unmanaged": false,
    "transaction": true,
    "save": true
  },
  "parameters": [
    {
      "name": "StreetWidth",
      "type": "float",
      "default": 1800.0
    },
    {
      "name": "Seed",
      "type": "int",
      "default": 1337
    }
  ],
  "ops": []
}
```

## Top-Level Fields

### `graph`

Required.

- Must be a valid project asset object path
- Must point to a writable asset under `/Game/`
- BAT creates the asset when `options.create_if_missing` is `true`

Example:

```json
"graph": "/Game/PCG/Graphs/BAT_CityGraph.BAT_CityGraph"
```

### `options`

Optional. Defaults are applied when omitted.

Supported fields:

- `mode`: `"reconcile" | "patch"`
- `ownership`: `"bat"`
- `create_if_missing`: `bool`, default `false`
- `clear_unmanaged`: `bool`, default `false`
- `transaction`: `bool`, default `true`
- `save`: `bool`, default `false`

Recommended defaults for v1:

```json
{
  "mode": "reconcile",
  "ownership": "bat",
  "create_if_missing": false,
  "clear_unmanaged": false,
  "transaction": true,
  "save": false
}
```

Notes:

- `reconcile` means BAT treats the payload as the desired managed graph state.
- `patch` is reserved for later support if needed, but the schema keeps the field now so the contract can grow without a breaking rewrite.
- `clear_unmanaged` should remain `false` by default in v1 unless a future implementation explicitly supports clearing non-BAT content.

### `parameters`

Optional convenience field.

This is a top-level shortcut for graph parameter definitions. The parser should canonicalize this to the same internal representation as `parameters.set` operations.

Supported entry fields in v1:

- `name`: required string
- `type`: required string
- `default`: optional typed value
- `description`: optional string

Supported parameter types in v1:

- `bool`
- `int`
- `float`
- `string`
- `name`
- `vector`

### `ops`

Required.

- Must be an array
- Operations execute in list order after parsing and normalization
- Structural parse and validation errors should fail the request before mutation

## Canonicalization Rules

The request parser should normalize convenience forms into one internal plan model.

Required canonicalization rules:

- Top-level `parameters` becomes an internal `parameters.set` operation list
- Inline `mesh_set` on `nodes.add` becomes the same internal representation as `spawners.set_mesh_set`
- Omitted `options` receive defaults
- Auto-generated node ids are allowed only for anonymous leaf nodes and must not be used for cross-request identity

The executor should not have separate code paths for inline convenience forms versus explicit ops.

## Node Identity Rules

### Required ids

`id` is required for any node that is:

- referenced by another op
- targeted by an edge connection
- updated later in the same request
- expected to survive reconciliation as a managed node

### Optional ids

`id` may be omitted only for anonymous leaf nodes that:

- are not referenced by any other op
- do not participate in later mutation
- can safely exist only within the current request execution

Parser rule:

- If `id` is omitted, BAT may generate a temporary internal id for execution bookkeeping.
- Temporary ids are not stable across requests.

## Supported Operation Families In v1

The route should start with a narrow, explicit allowlist.

Recommended v1 operations:

- `nodes.add`
- `nodes.set`
- `nodes.remove`
- `edges.connect`
- `edges.disconnect`
- `spawners.set_mesh_set`
- `parameters.set`

Other operations can be added later without changing the top-level schema.

## Operation Schemas

### `nodes.add`

Adds a supported PCG node to the graph.

Required fields:

- `op`: `"nodes.add"`
- `type`: supported external node-family string

Optional fields:

- `id`: required when the node must be referenced later
- `x`: editor position number
- `y`: editor position number
- `settings`: flat allowlisted settings object
- `mesh_set`: convenience spawner mesh-set payload when `type` is a supported spawner family

Example:

```json
{
  "op": "nodes.add",
  "id": "structure_spawner",
  "type": "StaticMeshSpawner",
  "x": 420,
  "y": 0,
  "settings": {
    "placement_mode": "surface",
    "density": 1.0
  },
  "mesh_set": {
    "mode": "weighted",
    "meshes": [
      "/Game/StaticMeshes/SM_CommonHouse2.SM_CommonHouse2",
      "/Game/StaticMeshes/SM_Hotel2.SM_Hotel2"
    ]
  }
}
```

### `nodes.set`

Updates supported settings on an existing managed node.

Required fields:

- `op`: `"nodes.set"`
- `node`: authored node id
- `settings`: flat allowlisted settings object

Example:

```json
{
  "op": "nodes.set",
  "node": "structure_spawner",
  "settings": {
    "density": 0.85
  }
}
```

### `nodes.remove`

Removes a managed node.

Required fields:

- `op`: `"nodes.remove"`
- `node`: authored node id

### `edges.connect`

Connects two supported pins.

Required fields:

- `op`: `"edges.connect"`
- `from`: pin reference string
- `to`: pin reference string

Pin reference format:

- `<nodeId>.<pinName>`

Example:

```json
{
  "op": "edges.connect",
  "from": "street_sampler.out",
  "to": "structure_spawner.in"
}
```

### `edges.disconnect`

Disconnects a supported link.

Required fields:

- `op`: `"edges.disconnect"`
- `from`: pin reference string
- `to`: pin reference string

### `spawners.set_mesh_set`

Assigns a mesh set to a supported static mesh spawner node.

Required fields:

- `op`: `"spawners.set_mesh_set"`
- `node`: authored node id
- `mode`: `"weighted" | "weighted_by_category"`

Weighted mode fields:

- `meshes`: array of project static mesh object paths

Weighted-by-category fields:

- `categories`: array of category entries

Category entry fields:

- `name`: required string
- `meshes`: required array of project static mesh object paths

Examples:

Weighted:

```json
{
  "op": "spawners.set_mesh_set",
  "node": "prop_spawner",
  "mode": "weighted",
  "meshes": [
    "/Game/StaticMeshes/Props/SM_Barrel_Tall.SM_Barrel_Tall",
    "/Game/StaticMeshes/Props/SM_Hay_Bale.SM_Hay_Bale"
  ]
}
```

Weighted by category:

```json
{
  "op": "spawners.set_mesh_set",
  "node": "structure_spawner",
  "mode": "weighted_by_category",
  "categories": [
    {
      "name": "large",
      "meshes": [
        "/Game/StaticMeshes/SM_Hotel2.SM_Hotel2",
        "/Game/StaticMeshes/SM_MBank.SM_MBank"
      ]
    },
    {
      "name": "small",
      "meshes": [
        "/Game/StaticMeshes/SM_CommonHouse2.SM_CommonHouse2",
        "/Game/StaticMeshes/SM_CommonHouse6.SM_CommonHouse6"
      ]
    }
  ]
}
```

### `parameters.set`

Creates or updates graph-level parameters.

Required fields:

- `op`: `"parameters.set"`
- `entries`: array of parameter entries

Parameter entry fields:

- `name`: required string
- `type`: required string
- `default`: optional typed value
- `description`: optional string

Example:

```json
{
  "op": "parameters.set",
  "entries": [
    {
      "name": "BuildingDensity",
      "type": "float",
      "default": 0.8,
      "description": "Controls how aggressively lots are filled"
    }
  ]
}
```

## Settings Model

`settings` uses flat allowlisted keys per supported node family.

The route should not accept arbitrary nested property bags in v1.

Reasons:

- easier to validate
- safer to evolve
- easier for AI clients to use consistently

Example shape:

```json
"settings": {
  "density": 1.0,
  "seed": 123,
  "placement_mode": "surface"
}
```

Implementation guidance:

- maintain a registry of supported node families
- maintain a per-family allowlist of supported setting keys
- reject unknown keys with structured validation errors

## Supported Node Family Registry

The external request contract should expose stable generic node-family names rather than raw Unreal class names.

Example families for v1:

- `SurfaceSampler`
- `StaticMeshSpawner`
- `Difference`
- `TransformPoints`
- `DensityFilter`
- `Partition`
- `AttributeFilter`

The implementation should map these stable names to supported Unreal PCG settings classes.

## Validation Rules

### Request validation

- `graph` is required
- `graph` must be a valid writable project asset path
- `ops` is required and must be an array
- every op must contain a valid `op` string
- any referenced node id must resolve
- any duplicate required node id is an error
- any malformed pin reference is an error

### Asset validation

- graph writes are limited to `/Game/`
- mesh paths must resolve to valid project `UStaticMesh` assets
- non-project mesh paths should be rejected in v1

### Operation validation

- unsupported op names are rejected
- unsupported node-family strings are rejected
- unsupported settings keys are rejected
- `spawners.set_mesh_set` is valid only for supported spawner node families
- `weighted_by_category` requires non-empty `categories`
- `weighted` requires non-empty `meshes`

### Execution validation

- perform full parse and validation before mutation
- run on the game thread
- wrap mutation in a transaction when enabled
- call `Modify()` on changed graph objects and nodes
- mark the package dirty after successful mutation
- save only when requested

## Error Model

The route should return BAT's standard response envelope.

Validation and execution failures should include structured issues with at least:

- `code`
- `message`
- `recoverable`
- `suggestedAction`
- `details`

Recommended error codes:

- `invalid_graph_path`
- `graph_not_found`
- `graph_create_failed`
- `invalid_request`
- `invalid_op`
- `duplicate_node_id`
- `unknown_node_reference`
- `invalid_pin_reference`
- `unsupported_node_type`
- `unsupported_setting`
- `invalid_static_mesh`
- `unsupported_mesh_set_mode`
- `save_failed`

Execution-phase errors should include the failing op index when possible.

## Full Example

```json
{
  "graph": "/Game/PCG/Graphs/BAT_CityGraph.BAT_CityGraph",
  "options": {
    "mode": "reconcile",
    "ownership": "bat",
    "create_if_missing": true,
    "transaction": true,
    "save": true
  },
  "parameters": [
    {
      "name": "StreetWidth",
      "type": "float",
      "default": 1800.0
    },
    {
      "name": "BuildingDensity",
      "type": "float",
      "default": 0.8
    },
    {
      "name": "Seed",
      "type": "int",
      "default": 1337
    }
  ],
  "ops": [
    {
      "op": "nodes.add",
      "id": "preserve_mask",
      "type": "Difference",
      "x": 0,
      "y": 0
    },
    {
      "op": "nodes.add",
      "id": "street_sampler",
      "type": "SurfaceSampler",
      "x": 280,
      "y": -120,
      "settings": {
        "density": 1.0,
        "seed": 1337
      }
    },
    {
      "op": "nodes.add",
      "id": "structure_spawner",
      "type": "StaticMeshSpawner",
      "x": 640,
      "y": -120,
      "settings": {
        "density": 0.8,
        "placement_mode": "surface"
      },
      "mesh_set": {
        "mode": "weighted_by_category",
        "categories": [
          {
            "name": "large",
            "meshes": [
              "/Game/StaticMeshes/SM_Hotel2.SM_Hotel2",
              "/Game/StaticMeshes/SM_MBank.SM_MBank"
            ]
          },
          {
            "name": "small",
            "meshes": [
              "/Game/StaticMeshes/SM_CommonHouse2.SM_CommonHouse2",
              "/Game/StaticMeshes/SM_CommonHouse6.SM_CommonHouse6"
            ]
          }
        ]
      }
    },
    {
      "op": "nodes.add",
      "id": "prop_spawner",
      "type": "StaticMeshSpawner",
      "x": 640,
      "y": 120
    },
    {
      "op": "spawners.set_mesh_set",
      "node": "prop_spawner",
      "mode": "weighted",
      "meshes": [
        "/Game/StaticMeshes/Props/SM_Barrel_Tall.SM_Barrel_Tall",
        "/Game/StaticMeshes/Props/SM_Hay_Bale.SM_Hay_Bale",
        "/Game/StaticMeshes/Props/SM_Log_Pile_1.SM_Log_Pile_1"
      ]
    },
    {
      "op": "edges.connect",
      "from": "preserve_mask.out",
      "to": "street_sampler.in"
    },
    {
      "op": "edges.connect",
      "from": "street_sampler.out",
      "to": "structure_spawner.in"
    },
    {
      "op": "edges.connect",
      "from": "street_sampler.out",
      "to": "prop_spawner.in"
    }
  ]
}
```

## Implementation Notes

- The parser should normalize convenience fields first, then validate the canonical op list.
- The executor should work only from the canonical plan model.
- The schema is intentionally narrower than raw PCG internals in v1.
- The route should solve real authoring needs without exposing a brittle reflection-based contract.
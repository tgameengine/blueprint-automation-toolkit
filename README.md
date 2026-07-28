# Blueprint Automation Toolkit (Editor Plugin)

Blueprint Automation Toolkit is an **Editor-only**, **localhost-only**, token-authenticated HTTP implementation bridge for Unreal Editor.

The primary use case is a local AI agent driving Unreal Editor through a small set of deep, generic primitives across assets, reflected objects, Blueprints, animation references, skeleton data, and editor scene state instead of feature-specific gameplay commands.

## Getting the Plugin

| Source | Cost | What you get |
|--------|------|--------------|
| **GitHub** | Free | Source code. Clone and build from source against your UE project. |
| **Fab** | $14.99 | Packaged binary. Launcher installation and engine version management. |

Both versions are identical in features. The Fab listing is a convenience option.

The minimal example host project is available from the
[v1.0.0 release](https://github.com/tgameengine/blueprint-automation-toolkit/releases/tag/v1.0.0).
It enables BAT as a dependency but does not redistribute the plugin.

For copy-ready, multi-step agent workflows, see
[Advanced Prompt Examples](Docs/AdvancedPromptExamples.md).

## Requirements and Dependencies

Blueprint Automation Toolkit is an editor-only C++ plugin. It is loaded as an
`EditorNoCommandlet` module and is not included in packaged games.

Required Unreal Engine plugins:

- `PCG`
- `GeometryProcessing`
- `GeometryScripting`

These are Unreal Engine plugins and are enabled automatically through
`BlueprintAutomationToolkit.uplugin`. BAT does not require an external SDK,
third-party DLL, separate service, package manager, or internet connection.
The HTTP server runs inside Unreal Editor and accepts loopback connections only.

The plugin also uses Unreal Engine's built-in editor, Blueprint, geometry,
HTTP/JSON, Slate, asset, and material modules. Source installations require a
C++ toolchain supported by the target Unreal Engine version. Fab builds include
compiled Win64 editor binaries for Unreal Engine 5.5, 5.6, 5.7, and 5.8.

If the plugin does not load, verify that the three required engine plugins are
present in the engine installation and enabled for the project.

## License

This plugin is open-source software licensed under the
[MIT License](LICENSE).

The preferred workflow is:

1. Discover the server and active gates with `GET /engine/discover`
2. Inspect runtime state with `GET /health` and `GET /ai/capabilities`
3. Resolve or describe targets with `POST /object/resolve` or `GET /object/describe`
4. Read or mutate reflected state with `GET /object/get_property`, `POST /object/set_property`, and `POST /object/call_function`
5. Read or apply Blueprint graph data with `GET /blueprint/graph/read` and `POST /blueprint/graph/apply`
6. Compile or validate changes and inspect diagnostics
7. Audit, load, save, or clean level actors with `POST /editor/level/*`
8. Persist assets or Blueprints with `POST /asset/save` or `POST /blueprint/compile_save`
9. Drive editor selection or focus with `POST /editor/select` and `POST /editor/focus`

Compile-related routes return structured diagnostics with normalized `warnings` and `errors` entries built from Unreal's compiler log and policy layer.

The internal services stay layered, and the public API is intentionally small and reflective so the platform scales by data, not by adding endless one-off routes.

## Core API

Preferred endpoints:

- `POST /blueprint/graph/apply`
- `GET /blueprint/graph/read`
- `POST /blueprint/compile_save`
- `POST /material/texture_samples/set`
- `POST /actor/spawn`
- `POST /actor/destroy`
- `POST /editor/level/audit`
- `POST /editor/level/destroy_actors`
- `POST /editor/level/save`
- `POST /editor/level/load`
- `POST /object/call_function`
- `POST /object/set_property`
- `GET /object/get_property`
- `GET /object/describe`
- `POST /editor/select`
- `POST /editor/focus`
- `POST /pie/start`
- `POST /pie/stop`

Level automation helpers:

- `POST /editor/level/audit` lists and counts actors by filters such as `labelPrefix`, `labelSuffix`, `labelContains`, `className`, `folderPrefix`, and `tag`.
- `POST /editor/level/destroy_actors` destroys all actors matching the same filter language. It refuses an empty filter unless `allowAll=true`; use `dryRun=true` to preview matches.
- `POST /editor/level/save` saves dirty map packages and, by default, dirty content packages so World Partition external actors are persisted.
- `POST /editor/level/load` loads a map from `map`, `mapPackage`, `package`, `path`, or `filename`.

Example:

```json
{
  "labelPrefix": "SKP_TEST_",
  "labelSuffix": "_Light",
  "className": "PointLight",
  "dryRun": true,
  "maxDelete": 20
}
```

## Extending It

Other editor plugins can register additional automation commands at startup without editing this plugin's dispatcher.

Registered extension commands are exposed as `POST` JSON endpoints, inherit the same auth/rate-limit pipeline, and appear in `GET /engine/discover` under `registeredCommands` and `extensionRoutes`.

```cpp
#include "Automation/AutomationCommand.h"
#include "IBlueprintAutomationToolkitModule.h"

class FMyCommand final : public FAutomationCommand
{
public:
	virtual FAutomationResult Execute(FAutomationContext& Context) override
	{
		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("status"), TEXT("ok"));
		return FAutomationResult::Ok(MakeShared<FJsonValueObject>(Data));
	}
};

void FMyPluginModule::StartupModule()
{
	FBATAutomationCommandRegistration Registration;
	Registration.Endpoint = TEXT("/my_plugin/do_work");
	Registration.Factory = []() -> TUniquePtr<FAutomationCommand>
	{
		return MakeUnique<FMyCommand>();
	};
	Registration.PermissionTier = EBATAutomationPermissionTier::Edit;
	Registration.RequiredPermissions = EBATAutomationPermission::Editor;
	Registration.bBindRoute = true;

	FString Error;
	IBlueprintAutomationToolkitModule::Get().RegisterAutomationCommand(MoveTemp(Registration), &Error);
}

void FMyPluginModule::ShutdownModule()
{
	if (IBlueprintAutomationToolkitModule::IsAvailable())
	{
		IBlueprintAutomationToolkitModule::Get().UnregisterAutomationCommand(TEXT("/my_plugin/do_work"));
	}
}
```

Safety notes:

- Built-in endpoints cannot be replaced or unregistered through the extension API.
- Extension routes are `POST`-only and require a JSON object body.
- Set `RequiredPermissions` and `bBlockDuringPie` explicitly so the command matches your editor safety expectations.

## Response Shape

All automation endpoints return a structured JSON envelope:

```json
{
	"ok": true,
	"requestId": "2a4b6f4f-0c7a-4a91-b793-8c7d8c65f3a3",
	"data": {},
	"warnings": [],
	"errors": []
}
```

Failures use the same top-level shape with structured issue entries:

```json
{
	"ok": false,
	"requestId": "2a4b6f4f-0c7a-4a91-b793-8c7d8c65f3a3",
	"data": {},
	"warnings": [],
	"errors": [
		{
			"code": "blueprint_compile_failed",
			"message": "Blueprint compile failed.",
			"recoverable": true,
			"suggestedAction": "inspect_target",
			"details": {}
		}
	]
}
```

## Example: Create a Blueprint Graph

Create or update an Event Graph in one request:

```json
{
	"blueprint": "/Game/BP_Spawner",
	"graph": "EventGraph",
	"options": {
		"compile": true,
		"save": false,
		"transaction": true
	},
	"nodes": [
		{
			"id": "begin_play",
			"type": "K2Node_Event",
			"event": "BeginPlay",
			"x": 0,
			"y": 0
		},
		{
			"id": "print_message",
			"type": "K2Node_PrintString",
			"message": "Spawner ready",
			"x": 320,
			"y": 0
		}
	],
	"links": [
		{
			"from": "begin_play.Then",
			"to": "print_message.execute"
		}
	]
}
```

Actor event nodes are no longer limited to `BeginPlay`. The graph apply API also accepts other Blueprint-overridable actor events, such as `ActorBeginOverlap`:

```json
{
	"blueprint": "/Game/BP_PickupOrb",
	"graph": "EventGraph",
	"options": {
		"compile": true,
		"save": false,
		"transaction": true
	},
	"nodes": [
		{
			"id": "actor_overlap",
			"type": "K2Node_Event",
			"event": "ActorBeginOverlap",
			"x": 0,
			"y": 0
		},
		{
			"id": "destroy_self",
			"type": "K2Node_CallFunction",
			"function": "/Script/Engine.Actor:K2_DestroyActor",
			"x": 320,
			"y": 0
		}
	],
	"links": [
		{
			"from": "actor_overlap.Then",
			"to": "destroy_self.execute"
		}
	]
}
```

Component-bound event nodes are also supported through `K2Node_ComponentBoundEvent`:

```json
{
	"blueprint": "/Game/BP_PickupOrb",
	"graph": "EventGraph",
	"nodes": [
		{
			"id": "mesh_overlap",
			"type": "K2Node_ComponentBoundEvent",
			"component": "PickupMesh",
			"event": "OnComponentBeginOverlap",
			"x": 0,
			"y": 0
		},
		{
			"id": "destroy_self",
			"type": "K2Node_CallFunction",
			"function": "/Script/Engine.Actor:K2_DestroyActor",
			"x": 360,
			"y": 0
		}
	],
	"links": [
		{
			"from": "mesh_overlap.Then",
			"to": "destroy_self.execute"
		}
	]
}
```

Typical success response:

```json
{
	"ok": true,
	"requestId": "2a4b6f4f-0c7a-4a91-b793-8c7d8c65f3a3",
	"data": {
		"blueprint": "/Game/BP_Spawner.BP_Spawner",
		"graph": "EventGraph",
		"createdNodes": ["begin_play", "print_message"],
		"updatedNodes": [],
		"createdLinks": 1,
		"compileStatus": "up_to_date",
		"saveStatus": "not_requested",
		"compileDiagnostics": {
			"compileStatus": "up_to_date",
			"errorCount": 0,
			"warningCount": 0,
			"errors": [],
			"warnings": []
		},
		"warnings": [],
		"errors": []
	},
	"warnings": [],
	"errors": []
}
```

## Scope

### What it can do

- Run while the **Unreal Editor is running**.
- Serve HTTP requests on **loopback only** (`127.0.0.1` / `::1`).
- Let an external agent discover capabilities, limits, permissions, and preferred routes.
- Resolve assets and objects, inspect reflected properties/functions, read current values, and mutate safe editor objects.
- Create and modify Blueprint assets and Blueprint graphs in the editor.
- Inspect and update asset references that participate in animation and skeletal workflows where generic editor semantics are exposed.
- Compile Blueprints explicitly, validate references, and save assets explicitly.
- Select and focus editor targets, with optional advanced surfaces gated behind explicit policy.

### What it cannot do (non-goals)

- Packaged runtime support (Shipping/Development builds). This plugin is **Editor-only**.
- Remote network control (LAN/WAN). It binds to loopback by design.
- Arbitrary editor UI automation (Slate widgets, menus, details panels).
- Deterministic gameplay guarantees (depends on map, pawn/controller, focus, input mappings, and frame timing).
- Multiplayer/network testing harness behavior.

## Security model

- Marketplace-safe defaults:
	- `bServerEnabled=false`
	- `bSafeModeEnabled=true`
	- `bEnableExecRoute=false`
	- `bAllowPythonExec=false`
- Accepts requests **only from loopback** (`127.0.0.1` / `::1`).
- Requires bearer token authentication via `Authorization: Bearer <token>`.
- Shows a permission dialog before starting the HTTP listener.
- Applies request body limits and per-client rate limits to the automation bridge surface.
- Adds route-level permission gates (`permissions.*`) and optional scoped tokens.
- Safe mode policy:
	- Safe mode ON: commands must pass strict allow-list checks and Python is blocked.
	- Safe mode OFF: broader commands are allowed, but separator/injection blocklist still applies.
	- Python requires both `bAllowPythonExec=true` and safe mode OFF.

Optional advanced routes such as `/ai/exec` are not part of the core editor bridge workflow and may be disabled or hidden by policy.

Do not change the bind address to `0.0.0.0` unless you also implement stronger authentication and you fully understand the risk.

## Installation

This repo is intended to be used as a plugin.

Typical setup:

1. Place the plugin in your project:
	 - `<YourProject>/Plugins/blueprint-automation-toolkit/`
2. Ensure the plugin is enabled in the editor.
3. Configure port/token in `DefaultEditor.ini` (below).
4. Launch the editor and verify `GET /engine/discover` responds.

## VS Code / Copilot workflow

For this project's recommended usage patterns (Live Coding builds + `/ai/exec` for console commands + optional Python), see:

- `Resources/Docs/PlanApi.md`
- `Resources/Docs/MarketplaceNotes.md`

## Headless automation tests

To run this plugin's automation tests headlessly, use `UnrealEditor-Cmd` with a quoted `ExecCmds` value and a report export path:

The automation test prefix remains `BlueprintAutomationToolkit.` because the internal module/test identifiers are unchanged.

```powershell
& "<UE_ROOT>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<PROJECT_UPROJECT>" `
	-unattended -nop4 -nosplash -NullRHI -stdout -FullStdOutLogOutput `
	-NoWatchdog -FORCELOGFLUSH -log `
	"-ReportExportPath=<REPORT_DIR>" `
	'-ExecCmds="Automation RunTest BlueprintAutomationToolkit.;Quit;"'
```

Notes:

- Use `Automation RunTest BlueprintAutomationToolkit.` for this plugin's test prefix.
- Keep the full `ExecCmds` argument quoted as a single value.
- `RunTest` is the verified form here; `RunTests` did not produce a reliable report in this setup.
- Unreal writes `index.json` and `index.html` under `<REPORT_DIR>` when the run succeeds.

## Configuration

The public plugin name is **Blueprint Automation Toolkit**, but the config section and runtime module identifiers remain `BlueprintAutomationToolkit`.

In your project’s `Config/DefaultEditor.ini`:

```ini
[BlueprintAutomationToolkit]
Port=9876
bServerEnabled=false
bPermissionPromptAnswered=false
; Safe mode (supports legacy bEnableSandbox and bCommandSandboxEnabled too)
bSafeModeEnabled=true
; Keep /asset/* and other filesystem-backed editor routes available while safe mode is on
bAllowFilesystemInSafeMode=true
; /ai/exec is disabled by default
bEnableExecRoute=false
; Save generated/rotated auth tokens in project settings by default
bSaveTokenInProjectSettings=true
AuthToken=<generated-or-custom-token>
bAllowPythonExec=false
; 64 KB default max body size for all /ai/* requests
MaxRequestBodyBytes=65536
; Token bucket limits (requests/second + burst)
RateLimitPerSecond=10
RateLimitBurst=20
permissions.editor=true
permissions.blueprint=false
permissions.pie=false
permissions.exec=false
permissions.python=false
permissions.filesystem=false
bEnableHmacAuth=false
MaxClockSkewSeconds=120
LogRingSize=500
+CommandSandboxAllowPrefixes=BAT.
+CommandSandboxAllowPrefixes=stat 
+CommandSandboxAllowPrefixes=show 
+CommandSandboxAllowPrefixes=r.
+CommandSandboxAllowPrefixes=t.
+CommandSandboxAllowPrefixes=sg.
+CommandSandboxAllowPrefixes=wp.
+CommandSandboxAllowPrefixes=ke 
+CommandSandboxAllowPrefixes=ce 
+CommandSandboxAllowPrefixes=open 
+CommandSandboxBlockSubstrings=\n
+CommandSandboxBlockSubstrings=\r
+CommandSandboxBlockSubstrings=|
+CommandSandboxBlockSubstrings=&&
+CommandSandboxBlockSubstrings=;
+CommandSandboxBlockSubstrings=py 
+CommandSandboxBlockSubstrings=python 
+CommandSandboxBlockSubstrings=quit
+CommandSandboxBlockSubstrings=exit
```

If `AuthToken` is missing, the plugin generates a token automatically.

- When `bSaveTokenInProjectSettings=true`, generated and rotated tokens are written to project settings.
- When `bSaveTokenInProjectSettings=false`, generated and rotated tokens stay runtime-only for the current editor session.

Environment override:

- If `BAT_AUTH_TOKEN` is set, runtime auth uses that token.
- ENV token values are never written to disk.

Client rule:

- If `bSaveTokenInProjectSettings=true` and a token is present in project config, that project-scoped token is a valid documented source for clients and agents.
- Otherwise, obtain the token from the user explicitly or from an already-provided secure source such as `BAT_AUTH_TOKEN`.
- Do not scrape per-user editor or user settings files for bearer tokens.

Auth failure responses include `projectTokenConfigured=true` when a project-config token is already available.

Every request must include:

`Authorization: Bearer <AuthToken>`

Optional response export:

- JSON POST requests may include `responseOutputPath` to save the HTTP response body to disk.
- The path must be relative. It is resolved under the toolkit response export directory in `Saved/BlueprintAutomationToolkit/Responses`.
- Requests that set `responseOutputPath` also require filesystem permission.
- If the path has no extension, `.json` is appended automatically.

## Runtime behavior (important)

- The server exists only while the **Editor process is running**.
- `POST /pie/start` and `POST /pie/stop` require PIE permission.
- `POST /ai/exec` can be used with PIE **on or off**:
	- With PIE off, the command executes against the editor world context.
	- With PIE on, it executes against the PIE world context.
	- Route must be explicitly enabled (`bEnableExecRoute=true`).

## Start the Editor (AI Workflow)

If an AI request fails before reaching `GET /engine/discover`, make sure Unreal Editor itself is running first.

1. Start Unreal Editor for your project.
2. Wait for the project/map to finish loading.
3. Verify the plugin is enabled (`Blueprint Automation Toolkit`).
4. Then start the HTTP server from the `Blueprint Automation Toolkit` panel.

Quick check from a terminal after startup (raw HTTP):

```http
GET /engine/discover HTTP/1.1
Host: 127.0.0.1:9876
Authorization: Bearer YOUR_TOKEN
```

## Start the Server

If requests fail with connection refused, the HTTP listener is not running yet.

1. Open Unreal Editor with the plugin enabled.
2. Open the `Blueprint Automation Toolkit` panel.
3. Click `Start Server`.
4. Accept the permission prompt the first time.
5. Verify with `GET /engine/discover`.

Optional auto-start:

- Set `[BlueprintAutomationToolkit] bServerEnabled=true` in `Config/DefaultEditor.ini`.
- Restart the editor.

## HTTP API

Plan-centric generic endpoints are documented in `Resources/Docs/PlanApi.md`.

OpenAPI spec:

- `GET /openapi` (source file: `Docs/openapi.yaml`)

Canonical agent workflow:

1. `GET /engine/discover`
2. `GET /health`
3. `POST /object/resolve` or `GET /object/describe`
4. `POST /object/set_property` or `POST /object/call_function`
5. `POST /blueprint/graph/apply`
6. `POST /blueprint/compile_save`
7. `POST /editor/level/save` when level actors or external actor packages changed

Base URL:

`http://127.0.0.1:<Port>`

All responses are JSON.

## Agent Handshake Flow

Recommended capability-first flow for agents:

1. `GET /engine/discover`
2. `GET /health`
3. `POST /object/resolve` or `GET /object/describe`
4. `POST /object/set_property` or `POST /object/call_function`
5. `POST /blueprint/graph/apply`
6. `POST /blueprint/compile_save`

Canonical routes:

- `GET /engine/discover`
- `GET /health`
- `GET /ai/capabilities`
- `POST /object/resolve`
- `GET /object/describe`
- `GET /object/get_property`
- `POST /object/set_property`
- `POST /object/call_function`
- `POST /blueprint/graph/apply`
- `POST /blueprint/compile_save`

Example agent flow:

1. Call `GET /engine/discover` to confirm the editor is reachable, safe mode state, and the recommended route flow.
2. Call `POST /object/resolve` with an object path, soft object path, or actor name to obtain a stable object reference.
3. Call `GET /object/describe` on that resolved object to inspect writable/readable fields and callable functions.
4. Use the appropriate action endpoint, such as `POST /object/set_property`, `POST /object/call_function`, or a Blueprint/editor route, based on the discovered capabilities.

Animation authoring and AnimGraph edits are Unreal forward-axis aware.

Material texture repair can be performed without Python through `POST /material/texture_samples/set`.
The route supports Texture Sample and Texture Object expressions, recompiles the material, and saves it by default:

```json
{
	"material": "/Game/Materials/M_Wall",
	"textures": {
		"MaterialExpressionTextureSample_0": "/Game/Textures/T_Wall_A",
		"MaterialExpressionTextureObject_0": "/Game/Textures/T_Wall_N"
	}
}
```

- `POST /asset/create` with `class: /Script/Engine.AnimSequence` accepts optional `forward_axis: "X" | "Y" | "Z"` and remaps incoming track translations and rotations into Unreal's `+X` forward frame before writing keys.
- `POST /blueprint/graph/apply` accepts optional `forward_axis` on AnimGraph node specs and remaps vector, rotator, quaternion, and transform properties into Unreal's `+X` forward frame before property import.

Asset delete example:

Use `POST /asset/delete` to remove one asset with `path` or multiple assets with `paths`.
If Unreal still holds editor-side references, set `force=true`.

Verified PowerShell example:

```powershell
$headers = @{ Authorization = 'Bearer YOUR_TOKEN' }
$body = @'
{
	"path": "/Game/ThirdPerson/Animations/BAT_AnimSequenceRouteProbe_20260307_052059",
	"force": true
}
'@

Invoke-RestMethod -Uri 'http://127.0.0.1:9876/asset/delete' `
	-Headers $headers `
	-Method Post `
	-ContentType 'application/json' `
	-Body $body
```

Example success response:

```json
{
	"ok": true,
	"request_id": "5e892423-4460-fee7-1809-0498748952ce",
	"deleted": 1,
	"force": true,
	"path": "/Game/ThirdPerson/Animations/BAT_AnimSequenceRouteProbe_20260307_052059",
	"paths": [
		{
			"path": "/Game/ThirdPerson/Animations/BAT_AnimSequenceRouteProbe_20260307_052059",
			"object_path": "/Game/ThirdPerson/Animations/BAT_AnimSequenceRouteProbe_20260307_052059.BAT_AnimSequenceRouteProbe_20260307_052059"
		}
	]
}
```

Standard error model:

```json
{ "code": "...", "message": "...", "details": {}, "requestId": "..." }
```

Status code discipline:

- `200` sync success
- `202` async job accepted
- `400` validation/schema/content-type errors
- `401` authentication failures
- `403` permission/scope failures
- `409` state conflicts
- `500` internal errors

Async jobs:

- `POST /jobs/submit` -> `{ "jobId": "...", "requestId": "..." }`
- `GET /jobs/{jobId}`
- `POST /jobs/{jobId}/cancel`
- `GET /logs/tail?n=200`

### `GET /health`

Use this to verify the server is running.

Response:

```json
{ "ok": true, "port": 9876 }
```

### `GET /ai/editor/map`

Returns information about the currently loaded editor map.

Response:

```json
{ "ok": true, "world": "editor", "map_package": "/Game/Maps/MyMap", "world_object": "MyMap" }
```

### `POST /ai/editor/quit`

Requests the Unreal Editor to exit.

Response:

```json
{ "ok": true, "exit_requested": true }
```

### `POST /ai/editor/layout/apply`

Applies a simple editor layout by spawning a list of actors.

Request body:

```json
{
	"actors": [
		{
			"class": "/Script/Engine.StaticMeshActor",
			"label": "Floor_00",
			"transform": {
				"location": [0, 0, 0],
				"rotation": [0, 0, 0],
				"scale": [3.2, 4.4, 0.12]
			},
			"assets": {
				"static_mesh": "/Engine/BasicShapes/Cube.Cube",
				"material0": "/Game/StarterContent/Materials/M_Metal_Burnished_Steel.M_Metal_Burnished_Steel"
			}
		},
		{
			"class": "/Script/Engine.PointLight",
			"label": "Light_00",
			"transform": {
				"location": [0, 0, 225],
				"rotation": [0, 0, 0],
				"scale": [1, 1, 1]
			},
			"properties": {
				"Intensity": 14000,
				"AttenuationRadius": 550,
				"SourceRadius": 14,
				"bCastShadows": true
			}
		}
	]
}
```

Allowlist policy:

- Allowed classes: `AStaticMeshActor`, `APointLight`.
- Allowed light properties: `Intensity`, `AttenuationRadius`, `SourceRadius`, `bCastShadows`.
- Allowed `APCGVolume` fields:
	- `properties.graph` (optional PCG graph asset path)
	- `properties.generate` (optional bool, defaults to `true`)
	- `assets.static_mesh` (optional static mesh asset path applied to graph spawner entries)
- Arbitrary reflection-based property sets are not allowed.

If `properties.graph` is omitted for `APCGVolume`, BAT ensures a suitable project graph at `/Game/PCG/Graphs/BAT_SpheresOnLandscape` by duplicating a working engine sample graph, then uses it for generation.

PCG example (spawn a volume that generates sphere instances):

```json
{
	"actors": [
		{
			"class": "/Script/PCG.PCGVolume",
			"label": "PCG_Spheres_01",
			"transform": {
				"location": [0, 0, 0],
				"rotation": [0, 0, 0],
				"scale": [20, 20, 8]
			},
			"properties": {
				"graph": "/PCG/SampleContent/SimpleForest/PCGGraphs/SimpleForestWorldRayHits.SimpleForestWorldRayHits",
				"generate": true
			},
			"assets": {
				"static_mesh": "/Engine/BasicShapes/Sphere.Sphere"
			}
		}
	]
}
```

Response:

```json
{ "ok": true, "spawned_actors": 2, "rejected_actors": 0, "applied_properties": 4, "rejected_properties": 0 }
```

### `POST /ai/exec`

Executes a console command.

This route is disabled by default and must be explicitly enabled.

Request body:

```json
{ "command": "stat unit" }
```

Optional world selector (default is `"auto"`):

```json
{ "command": "stat unit", "world": "editor" }
```

Valid values: `"auto"` (default), `"editor"`, `"pie"`.

Multi-PIE support:

- `pie_index` (number, default `0`) selects which PIE instance to target when `world="pie"`.
- `require_world` (bool, default `false`) forces the request to explicitly specify `world` (rejects missing/`auto`).

Batch request body (runs commands in order):

```json
{ "commands": ["stat unit", "stat fps"] }
```

Python request body (requires `bAllowPythonExec=true` and safe mode OFF):

```json
{ "python": "print('hello from python')" }
```

Response:

```json
{ "ok": true, "pie": false, "output": "..." }
```

Batch response shape:

```json
{ "mode": "batch", "ok": true, "pie": false, "results": [{"command":"...","ok":true,"output":"..."}] }
```

Notes:

- Output is whatever Unreal returns for that command (may be empty).
- Some commands only make sense in PIE.

If `/ai/exec` is disabled, the route is not bound and requests return HTTP 404.

When the route is bound but runtime checks deny execution, the response is:

```json
{ "ok": false, "error": "exec_route_disabled" }
```

Raw HTTP example:

```http
POST /ai/exec HTTP/1.1
Host: 127.0.0.1:9876
Authorization: Bearer YOUR_TOKEN
Content-Type: application/json

{"command":"stat fps"}
```

### `POST /pie/start`

Starts PIE (active viewport).

Response:

```json
{ "ok": true, "pie": true }
```

### `POST /pie/stop`

Stops PIE.

Response:

```json
{ "ok": true, "pie": false }
```

### Blueprint endpoints (Editor asset automation)

These endpoints operate on editor assets and do **not** require PIE.
Mutating editor-asset routes are blocked while PIE is running and return `409 pie_edit_blocked`; stop PIE before calling create/apply/set-defaults/compile/save/component/graph mutation endpoints.

#### `POST /blueprint/create`

Creates a new Blueprint asset.

Request body:

```json
{ "path": "/Game/Blueprints", "name": "BP_TestActor", "parent": "/Script/Engine.Actor" }
```

Response:

```json
{ "ok": true, "package": "/Game/Blueprints/BP_TestActor", "object_path": "/Game/Blueprints/BP_TestActor.BP_TestActor" }
```

Compose behaviors from primitives (client-side JSON plans):

- Use `POST /blueprint/create` for asset creation.
- Use `POST /blueprint/set-defaults` for CDO defaults.
- Use `POST /blueprint/*` graph/node endpoints for Blueprint logic.
- Use `POST /ai/editor/layout/apply` to place geometry/lights in the current map.

Notes:

- `path` must be a valid long package path (example: `/Game/Blueprints`).
- `parent` is optional; defaults to `AActor`.

#### `POST /blueprint/set-defaults`

Sets Class Default values (CDO) on a Blueprint.

Request body:

```json
{
	"blueprint": "/Game/Blueprints/BP_TestActor.BP_TestActor",
	"defaults": {
		"SomeFloat": 1.25,
		"SomeBool": true,
		"SomeName": "Foo",
		"SomeVector": { "x": 0, "y": 0, "z": 100 }
	},
	"components_apply": [
		{
			"class": "/Script/Engine.SplineComponent",
			"name": "CorridorSpline",
			"points": [
				[0, 0, 0],
				[600, 0, 0],
				[1200, 250, 0]
			],
			"point_type": "Curve",
			"closed_loop": false
		}
	]
}
```

Response:

```json
{ "ok": true, "set": ["SomeFloat","SomeBool"], "errors": [] }
```

`components_apply` supports a small allowlist:

- `/Script/Engine.InstancedStaticMeshComponent`
- `/Script/Engine.HierarchicalInstancedStaticMeshComponent`
- `/Script/Engine.SplineComponent`
- `/Script/Engine.SplineMeshComponent`

For ISM/HISM entries, optional `from_spline` can generate instances procedurally from an existing spline component template:

```json
{
	"class": "/Script/Engine.InstancedStaticMeshComponent",
	"name": "FloorISM",
	"static_mesh": "/Engine/BasicShapes/Cube.Cube",
	"from_spline": {
		"component": "CorridorSpline",
		"step": 300,
		"start_distance": 0,
		"end_distance": 1800,
		"align_to_tangent": true,
		"offset": [0, 0, 50],
		"rotation": [0, 0, 0],
		"scale": [3, 5, 0.12]
	}
}
```

Notes for `from_spline`:

- `component` is required and must reference a `SplineComponent` in the same Blueprint.
- `step` is required and must be `> 0`.
- `start_distance` and `end_distance` are optional and clamp to spline length.
- `align_to_tangent` defaults to `true`.
- `offset`/`rotation`/`scale` are local per-instance adjustments.

Blueprint-only spline corridor mode:

- For `class: "/Script/Engine.SplineMeshComponent"`, provide `from_spline.component` and `static_mesh`.
- BAT generates `UserConstructionScript` nodes (Blueprint graph only) that dynamically add spline mesh components along spline point segments via `Add Spline Mesh Component`.
- No runtime module/code is required for this path.
- Optional `from_spline` tuning fields for spline meshes:
	- `offset`: `[x,y,z]` or `{ "x":..., "y":..., "z":... }`
	- `scale_start`: `[x,y]` or `{ "x":..., "y":... }`
	- `scale_end`: `[x,y]` or `{ "x":..., "y":... }`
	- `roll_start_degrees`: number
	- `roll_end_degrees`: number
	- `forward_axis`: `"X" | "Y" | "Z"`

For spline entries:

- `points` is required and accepts vectors as `[x,y,z]` or `{ "x":..., "y":..., "z":... }`.
- `point_type` is optional (`Linear`, `Curve`, `Constant`, `CurveClamped`, `CurveCustomTangent`).
- `closed_loop` is optional (defaults to `false`).

Supported property types (initial): `bool`, numbers (int/float), `FString`, `FName`, `FVector`, `FRotator`.

### Blueprint plan/apply (generic asset + graph plan)

Use `POST /blueprint/apply` when an AI client needs to compose multiple Blueprint edits in one request.

This route keeps the contract generic and reusable. It is the preferred write surface for:

- asset creation
- variable declaration
- user function graph creation
- component edits
- graph patch application
- optional compile at the end of the plan

Supported operations today:

- `create`
- `variables.add`
- `variables.list`
- `functions.add`
- `functions.list`
- `components.add`
- `components.remove`
- `components.set`
- `components.instances.add`
- `components.list`
- `graph.apply`
- `compile`

Example request:

```json
{
	"blueprint": "/Game/Blueprints/BP_TestActor.BP_TestActor",
	"ops": [
		{
			"op": "variables.add",
			"name": "Health",
			"type": "float",
			"default": 100.0,
			"instance_editable": true
		},
		{
			"op": "functions.add",
			"name": "AI_UpdateTarget"
		},
		{
			"op": "graph.apply",
			"graph": "AI_UpdateTarget",
			"nodes": [
				{
					"id": "print_1",
					"type": "K2Node_CallFunction",
					"function": "/Script/Engine.KismetSystemLibrary:PrintString",
					"pins": {
						"InString": "Updating target"
					}
				}
			],
			"links": []
		},
		{ "op": "compile" }
	]
}
```

Response fields include operation counts such as `variables_added`, `functions_added`, `components_added`, and `instances_added`.

Variable type strings supported by `variables.add`:

- `bool`
- `int`
- `int64`
- `float`
- `double`
- `string`
- `name`
- `text`
- `vector`
- `rotator`
- `transform`
- `object:/Script/Engine.Actor`
- `class:/Script/Engine.Actor`

Array variables are supported by adding `[]` to the type string (example: `float[]`, `vector[]`).

Current `variables.add` notes:

- `default` currently supports scalar literal defaults (`bool`, numbers, `string`, `name`, `text`).
- `instance_editable` is optional and defaults to Blueprint-only editing.
- `blueprint_read_only` is optional.

`variables.list` supports optional filters:

- `name_prefix`
- `type`

`functions.list` supports optional filters:

- `class`
- `search`
- `blueprint_callable_only`
- `exclude_latent`
- `exclude_unsafe`
- `limit`

### Blueprint graph/node editing (K2 / EventGraph)

This is still **Editor-only** and focuses on practical K2 graph editing (add nodes, connect pins, set pin defaults). It does not try to replicate every Blueprint Editor feature.

`POST /blueprint/graph/apply` currently supports these node families:

- `K2Node_Event`
- `K2Node_SpawnActor`
- `K2Node_CallFunction`
- `K2Node_PrintString`
- `K2Node_Delay`
- `K2Node_AddComponent`
- `K2Node_VariableGet`
- `K2Node_VariableSet`
- `K2Node_ExecutionSequence`
- `K2Node_Knot`
- `K2Node_MacroInstance`

Notes for the new graph node types:

- `K2Node_Delay` is implemented as a typed alias over `/Script/Engine.KismetSystemLibrary:Delay`; set `pins.Duration` to control the delay length.
- `K2Node_ExecutionSequence` accepts optional `outputs` to request more than the default two `Then_*` pins.
- `K2Node_Knot` is a reroute node; connect `InputPin` and `OutputPin` through normal link entries.
- `K2Node_VariableSet` uses `variable` plus optional `pins` values for the variable input pin.

#### `POST /blueprint/schema`

Returns Blueprint authoring metadata for AI clients before they issue write operations.

This route can expose:

- `graphs`
- `components`
- `variables`
- `functions`
- `supported_node_types`
- `graph_snapshot`

Request body:

```json
{
	"blueprint": "/Game/Blueprints/BP_TestActor.BP_TestActor",
	"graph": "EventGraph",
	"include": ["graphs", "components", "variables", "functions", "supported_node_types", "graph_snapshot"],
	"function_filter": {
		"class": "/Script/Engine.KismetSystemLibrary",
		"search": "print",
		"blueprint_callable_only": true,
		"exclude_latent": true,
		"exclude_unsafe": true,
		"limit": 50
	}
}
```

Notes:

- `blueprint` is required.
- `graph` is optional; it is only needed when requesting `graph_snapshot`.
- omit `include` to return all supported sections.
- if `function_filter.class` is omitted, function discovery is limited to the Blueprint generated class and parent class.

#### `POST /blueprint/graphs`

Lists graphs contained in a Blueprint.
#### `POST /blueprint/compile_save`

Compiles a Blueprint and optionally saves it in one request.

```json
{ "blueprint": "/Game/Blueprints/BP_TestActor.BP_TestActor" }
```
{ "blueprint": "/Game/Blueprints/BP_TestActor.BP_TestActor", "compile": true, "save": true }
#### `POST /blueprint/graph/nodes`

Lists normalized node metadata for a specific Blueprint graph.

```json
{ "blueprint": "/Game/Blueprints/BP_TestActor.BP_TestActor", "graph": "EventGraph" }
```
2. `POST /blueprint/compile_save`
#### `POST /blueprint/node/add-custom-event`

```json
{ "blueprint": "/Game/Blueprints/BP_TestActor.BP_TestActor", "graph": "EventGraph", "name": "AI_OnTick", "x": 0, "y": 0 }
```

#### `POST /blueprint/node/add-callfunction`

```json
{ "blueprint": "/Game/Blueprints/BP_TestActor.BP_TestActor", "graph": "EventGraph", "function": "/Script/Engine.KismetSystemLibrary:PrintString", "x": 350, "y": 0 }
```

#### `POST /blueprint/node/add-branch`

```json
{ "blueprint": "/Game/Blueprints/BP_TestActor.BP_TestActor", "graph": "EventGraph", "x": 250, "y": 150 }
```

#### `POST /blueprint/pin/connect`

```json
{
	"blueprint": "/Game/Blueprints/BP_TestActor.BP_TestActor",
	"from": { "node_guid": "...", "pin": "Then" },
	"to": { "node_guid": "...", "pin": "execute" }
}
```

#### `POST /blueprint/pin/set-default`

Sets a pin’s default value as a string (for pins that support a literal default).

```json
{ "blueprint": "/Game/Blueprints/BP_TestActor.BP_TestActor", "node_guid": "...", "pin": "InString", "value": "Hello" }
```

Object and class pins accept a mounted asset or class object path. BAT resolves
the path, verifies that it is compatible with the pin type, and assigns the
pin's `DefaultObject` rather than storing an invalid string literal:

```json
{
	"blueprint": "/Game/Blueprints/BP_TestActor.BP_TestActor",
	"node_guid": "...",
	"pin": "Sound",
	"value": "/Game/Audio/SW_Impact.SW_Impact"
}
```

Successful object assignments return `default_kind: "object"` and the resolved
object path. An unresolved or incompatible object path returns
`pin_default_object_not_found` without modifying the pin.

#### `POST /blueprint/node/describe`

Returns node metadata + pins (useful to discover pin names for connecting).

```json
{ "blueprint": "/Game/Blueprints/BP_TestActor.BP_TestActor", "node_guid": "..." }
```

#### `POST /blueprint/node/delete`

```json
{ "blueprint": "/Game/Blueprints/BP_TestActor.BP_TestActor", "node_guid": "..." }
```

#### `POST /blueprint/compile_save`

Compiles a Blueprint and optionally saves it in one request.

Request body:

```json
{ "blueprint": "/Game/Blueprints/BP_TestActor.BP_TestActor", "compile": true, "save": true }
```

## API-First Graph Flow

Primary sequence (no external scripts):

1. `POST /blueprint/graph/apply`
2. `POST /blueprint/compile_save`
3. `GET /blueprint/graph/links?blueprint=<path>&graph=<name>`

Raw HTTP example for graph apply:

```http
POST /blueprint/graph/apply HTTP/1.1
Host: 127.0.0.1:9876
Authorization: Bearer YOUR_TOKEN
Content-Type: application/json

{
  "blueprint": "/Game/BP_Test",
  "graph": "EventGraph",
  "nodes": [
    {"id":"begin","type":"K2Node_Event","event":"BeginPlay","x":0,"y":0}
  ],
  "links": []
}
```

Optional Python API client example:

```python
import requests

url = "http://localhost:30010/blueprint/graph/apply"

payload = {
  "blueprint": "/Game/BP_Test",
  "graph": "EventGraph",
  "nodes": [
    {"id":"begin","type":"K2Node_Event","event":"BeginPlay","x":0,"y":0}
  ],
  "links": []
}

r = requests.post(url, json=payload)
print(r.json())
```

## Troubleshooting

- `GET /health` fails:
	- Ensure Unreal Editor itself is running and the project is fully loaded.
	- Ensure the server was started from the `Blueprint Automation Toolkit` panel (or set `bServerEnabled=true` and restart).
	- Verify port/config in `DefaultEditor.ini` under `[BlueprintAutomationToolkit]`.
	- If `AuthToken` is set, ensure the Authorization header is present.
- Actor not found:
	- Confirm the Actor Label (in editor) or Object Name.
	- Prefer stable labels for automation.
- Live Coding crash on compile (EXCEPTION_ACCESS_VIOLATION in `FAutoConsoleObject` / `IConsoleManager.h`):
	- Avoid registering console commands via static/global `FAutoConsoleCommand*` objects (they run in dynamic initializers when Live Coding loads a patch DLL).
	- Prefer registering/unregistering commands in your module `StartupModule()` / `ShutdownModule()` (store them as members or `TUniquePtr`s), so registration happens at a safe time.

## Extending

The plugin is intentionally small. If you need additional actions (spawn actors, teleport, query world state, etc.), add endpoints in your own project/plugin while preserving the same security posture (loopback + optional bearer token).


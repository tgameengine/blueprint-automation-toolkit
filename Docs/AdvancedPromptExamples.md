# Advanced Prompt Examples

This guide contains copy-ready prompts for AI agents that control Unreal Editor through Blueprint Automation Toolkit (BAT).

BAT is an implementation bridge, not an embedded language model. The external agent reads a prompt, discovers the active BAT capabilities, translates the requested work into authenticated localhost HTTP calls, verifies the result, and saves only when the acceptance criteria are satisfied.

## Before Using These Prompts

The Unreal Editor must be running with BAT enabled and the localhost server started. Every request must use the configured bearer token.

Ask the agent to follow this baseline workflow:

1. Call `GET /engine/discover`, `GET /health`, and `GET /ai/capabilities`.
2. Use `GET /openapi` or `POST /blueprint/schema` before constructing unfamiliar write requests.
3. Inspect existing objects, assets, graphs, actors, and values before changing them.
4. Prefer transactions, stable labels, folders, tags, and explicit asset paths.
5. Compile and validate before saving.
6. Use a dry run before destructive level cleanup.
7. Save assets or levels only after all requested acceptance checks pass.
8. Never claim success from an HTTP status alone; verify the resulting editor state.
9. Do not enable `/ai/exec`, Python, unsafe mode, or broader permissions unless the user explicitly requests them.
10. If a requested operation is not exposed by the discovered API, stop and report the capability gap instead of inventing a route.

## Reusable Safety Preamble

Prefix any advanced prompt with the following block:

> Work through Blueprint Automation Toolkit in the currently open Unreal Editor. Start with `/engine/discover`, `/health`, and `/ai/capabilities`. Inspect before editing, use only discovered and documented routes, keep the work transactional where supported, and do not save until every acceptance check passes. If an operation is unsupported, stop and explain the gap. Return the routes used, changed objects, compile diagnostics, warnings, errors, and final save status.

## 1. Build and Review a Modular Showcase Scene

> In the currently open map, create a polished modular showcase scene under the folder `BAT_Showcase_Demo`. Add a metallic stage, eight evenly spaced pillars, four chrome spheres, a raised gold centerpiece, two point lights, and a title actor. Use stable labels beginning with `BAT_SHOWCASE_` and tag every created actor with `BAT_Showcase_Demo`. Keep the composition centered around the world origin and make it easy to review from one viewport frame. After creation, audit the level by folder, select the created geometry, focus the viewport on it, and report the exact actor count. Do not save the level until the audit confirms that only the requested actors were created and no properties were rejected.

Typical BAT flow:

- `GET /ai/editor/map`
- `POST /ai/editor/layout/apply`
- `POST /editor/level/audit`
- `POST /editor/select`
- `POST /editor/focus`
- `POST /editor/level/save`

Acceptance evidence:

- All actor labels share the requested prefix.
- The audit count matches the requested layout.
- The layout response contains no rejected actors or properties.
- The final response identifies the saved map package.

## 2. Create a Verified Pickup Blueprint

> Create `/Game/BAT_Demos/BP_BAT_PickupOrb` as an Actor Blueprint. Add a static mesh component named `PickupMesh` and build an Event Graph that handles `PickupMesh.OnComponentBeginOverlap`, prints `BAT pickup collected`, and destroys the pickup actor. Use the component-bound overlap event rather than a generic tick-based workaround. Compile without saving first, inspect normalized compiler warnings and errors, read the graph back, and verify the execution links. Save only when compilation succeeds with zero errors and the expected overlap-to-print-to-destroy execution chain is present.

Typical BAT flow:

- `POST /blueprint/create`
- `POST /blueprint/apply` or component routes
- `POST /blueprint/graph/apply`
- `POST /blueprint/compile_save` with saving disabled for the first compile
- `GET /blueprint/graph/read`
- `GET /blueprint/graph/links`
- `POST /blueprint/compile_save` with saving enabled

Acceptance evidence:

- `PickupMesh` exists.
- The graph contains a `K2Node_ComponentBoundEvent`.
- Compile diagnostics report zero errors.
- Read-back confirms every required execution link.

## 3. Refactor an Existing Blueprint Without Rebuilding It

> Inspect `/Game/Blueprints/BP_DoorController.BP_DoorController` and its `EventGraph`. Preserve unrelated nodes, comments, variables, and links. Find the existing custom event responsible for opening the door and update only its connected logic so it prints `Door opening`, waits 0.25 seconds, and then calls the existing open-door function. Use stable node IDs from graph read-back when possible and prefer update-only graph edits. Auto-arrange only the connected nodes affected by this change. Compile without saving, read the graph back, compare the changed subgraph with the original snapshot, and save only if no unrelated nodes were deleted and compilation has zero errors.

Typical BAT flow:

- `POST /blueprint/schema`
- `GET /blueprint/graph/read`
- `POST /blueprint/graph/apply`
- `POST /blueprint/compile_save`
- `GET /blueprint/graph/read`
- `GET /blueprint/graph/links`

Acceptance evidence:

- The original unrelated graph inventory remains intact.
- Only the intended connected subgraph changed.
- The delay duration is `0.25`.
- Compile diagnostics report zero errors.

## 4. Create a Spline-Driven Modular Corridor Blueprint

> Create `/Game/BAT_Demos/BP_BAT_ModularCorridor` as an Actor Blueprint. Add a spline component named `CorridorSpline` with five editable curve points forming a gentle S-shaped path. Add a hierarchical instanced static mesh component named `FloorHISM` that places `/Engine/BasicShapes/Cube.Cube` along the spline every 300 Unreal units, aligned to the tangent, with a thin floor-like scale and a 50-unit vertical offset. Add a spline mesh component configuration for corridor side segments using the same spline. Compile, inspect the generated components and construction logic, and save only when the component list, spline points, generated instances, and compile diagnostics match the request.

Typical BAT flow:

- `POST /blueprint/create`
- `POST /blueprint/set-defaults` with `components_apply`
- `POST /blueprint/schema`
- `POST /blueprint/compile_save`
- `POST /blueprint/schema`

Acceptance evidence:

- `CorridorSpline`, `FloorHISM`, and the requested spline mesh configuration exist.
- The spline contains five points.
- Instance spacing and tangent alignment match the prompt.
- Compilation succeeds with zero errors.

## 5. Generate and Validate a PCG Test Area

> In the current map, create a PCG volume labeled `BAT_PCG_SphereField` under the folder `BAT_PCG_Demos`. Use the discovered PCG graph capability or BAT's supported default PCG graph, generate sphere instances from `/Engine/BasicShapes/Sphere.Sphere`, and tag the volume `BAT_PCG_Demo`. Place it away from existing production actors, focus the editor on the new volume, and audit the level to prove that exactly one PCG volume with that label exists. Do not save if graph assignment or generation reports an error.

Typical BAT flow:

- `GET /engine/discover`
- `GET /ai/editor/map`
- `POST /ai/editor/layout/apply`
- `POST /actor/find`
- `POST /editor/focus`
- `POST /editor/level/audit`
- `POST /editor/level/save`

Acceptance evidence:

- The PCG volume resolves by its stable label.
- The requested graph and sphere mesh are assigned.
- Generation completes without rejected properties.
- The level audit finds exactly one matching volume.

## 6. Build a Data-Driven Targeting Blueprint in One Plan

> Create or update `/Game/BAT_Demos/BP_BAT_TargetingAgent.BP_BAT_TargetingAgent` through a single Blueprint plan where supported. Add an instance-editable float variable `DetectionRadius` with a default of `1500`, an instance-editable Actor object variable `CurrentTarget`, and a function graph named `BAT_UpdateTarget`. In that function, print `Updating target` and preserve a clear execution path for future targeting logic. Add a scene component named `TargetOrigin`, compile at the end of the plan, then query the schema to verify the variables, function, component, and graph. Save only after the schema and compiler diagnostics confirm the requested structure.

Typical BAT flow:

- `POST /blueprint/schema`
- `POST /blueprint/apply`
- `POST /blueprint/schema`
- `POST /blueprint/compile_save`

Acceptance evidence:

- Both variables have the requested types and editability.
- `BAT_UpdateTarget` exists and contains the expected print call.
- `TargetOrigin` exists.
- Compilation succeeds with zero errors.

## 7. Repair Material Texture References Without Python

> Inspect `/Game/Materials/M_BAT_Wall.M_BAT_Wall` and identify its supported texture sample and texture object expressions. Assign `/Game/Textures/T_BAT_Wall_BaseColor.T_BAT_Wall_BaseColor` to the base-color texture sample and `/Game/Textures/T_BAT_Wall_Normal.T_BAT_Wall_Normal` to the normal texture object. Use the dedicated material texture route, not Python or console execution. Recompile the material, save it, then read back or describe the relevant expressions and report every updated expression name, unresolved reference, warning, and error.

Typical BAT flow:

- `POST /object/resolve`
- `GET /object/describe`
- `POST /material/texture_samples/set`
- `POST /asset/save`
- `GET /object/describe`

Acceptance evidence:

- Both texture asset paths resolve.
- The dedicated route reports the intended expression updates.
- The material recompiles and saves.
- No Python or `/ai/exec` route is used.

## 8. Perform a Guarded Test-Actor Cleanup

> Audit the current level for actors whose labels begin with `BAT_TEST_` and whose folder path begins with `AutomationTests`. Return the full matched count and a preview list, but do not delete anything yet. If the count is greater than 25, stop and request confirmation. Otherwise run a dry-run destroy with `maxDelete` set to the exact matched count. Continue with the real deletion only when the dry-run result matches the audit exactly. Audit again after deletion and save the level only if zero matching actors remain and no deletion failed.

Typical BAT flow:

- `POST /editor/level/audit`
- `POST /editor/level/destroy_actors` with `dryRun: true`
- `POST /editor/level/destroy_actors` with `dryRun: false`
- `POST /editor/level/audit`
- `POST /editor/level/save`

Acceptance evidence:

- The initial audit and dry-run counts match.
- The deletion never exceeds the explicit limit.
- The final audit reports zero matches.
- Failed deletion count is zero.

## 9. Duplicate, Configure, and Verify an Asset Variant

> Duplicate `/Game/Props/BP_Crate.BP_Crate` to `/Game/BAT_Demos/BP_Crate_Heavy.BP_Crate_Heavy`. Resolve and describe the duplicated asset before changing it. Set only discovered writable defaults needed to make it a heavy variant, including a display name of `Heavy Crate` and a mass-related value if that property is exposed and allowlisted. Do not modify the source asset. Compile the duplicate if it is a Blueprint, save it, then read the changed values back and verify that the original asset retained its previous values.

Typical BAT flow:

- `POST /asset/duplicate`
- `POST /object/resolve`
- `GET /object/describe`
- `GET /object/get_property`
- `POST /object/set_property`
- `POST /blueprint/compile_save` or `POST /asset/save`
- `GET /object/get_property`

Acceptance evidence:

- Source and destination object paths are different and both resolve.
- Only discovered writable properties are changed.
- The duplicate saves successfully.
- Read-back proves the source asset was not modified.

## 10. Migrate a Blueprint Component Safely

> Inspect `/Game/Blueprints/BP_LegacyLamp.BP_LegacyLamp` and list its components. Replace the legacy point-light component named `OldLampLight` with the supported target light component. Reapply its explicitly requested relative transform and do not remove or replace any other component. If the discovered replacement schema cannot represent an attachment that must be preserved, stop before changing the asset and report that limitation. Otherwise, compile without saving, query the component list again, and confirm that exactly one replacement occurred. Save only if the old component is gone, the new component exists with the requested transform, and compiler diagnostics contain zero errors.

Typical BAT flow:

- `POST /blueprint/schema`
- `POST /blueprint/components/replace`
- `POST /blueprint/compile_save`
- `POST /blueprint/schema`
- `POST /blueprint/compile_save` with saving enabled

Acceptance evidence:

- Exactly one component is replaced.
- Unrelated components remain unchanged.
- The requested relative transform is verified.
- A required attachment is either representable or reported before mutation.
- Compilation succeeds with zero errors.

## 11. Author an Animation Asset With Explicit Forward-Axis Conversion

> Create `/Game/BAT_Demos/Animations/A_BAT_TurnProbe` as an `AnimSequence` using the discovered skeleton and animation authoring schema. Treat incoming animation data as `Y` forward and request BAT's forward-axis conversion into Unreal's `+X` forward frame. Add a short root-motion probe with clearly named tracks and deterministic keys, save the asset, then inspect the resulting references and report the source axis, converted axis, track count, key count, sequence length, skeleton path, warnings, and errors. If the skeleton or track schema cannot be resolved, stop without creating a partial asset.

Typical BAT flow:

- `GET /engine/discover`
- `GET /openapi`
- `POST /object/resolve`
- `POST /asset/create`
- `POST /asset/save`
- `GET /object/describe`

Acceptance evidence:

- The skeleton resolves before asset creation.
- The request explicitly uses `forward_axis: "Y"`.
- The resulting asset reports the expected tracks and keys.
- No partial asset remains after a failed validation.

## 12. Run a Bounded PIE Smoke Test

> Run a bounded Play in Editor smoke test for the currently open map. Confirm PIE permission first. Start PIE, wait only for the documented ready state, resolve the test actor labeled `BAT_SMOKE_TARGET`, and read a small set of discovered runtime-safe properties. Do not attempt arbitrary UI input or claim deterministic gameplay behavior. Stop PIE in a guaranteed cleanup step even if inspection fails. Report PIE start and stop status, the resolved actor reference, property values read, warnings, errors, and any operation skipped because it was blocked during PIE.

Typical BAT flow:

- `GET /engine/discover`
- `GET /ai/capabilities`
- `POST /pie/start`
- `POST /object/resolve`
- `GET /object/get_property`
- `POST /pie/stop`

Acceptance evidence:

- PIE starts only when the required permission is active.
- The actor is resolved by a stable label.
- Only discovered runtime-safe reads are attempted.
- PIE is stopped regardless of intermediate success or failure.

## 13. Execute a Reflection-Driven Editor Configuration Pass

> Resolve the actor labeled `BAT_ConfigTarget` in the current editor world. Describe its reflected properties and callable functions before changing anything. Read the current values of every requested field, then set only writable allowlisted properties needed to match this target state: location `[500, 0, 200]`, rotation `[0, 90, 0]`, and any project-specific exposed setting named `AutomationProfile` if it exists. Call a discovered editor-safe refresh function only if the schema marks it callable. Read all changed values back, focus the actor in the viewport, and do not save the level if any requested field is unavailable or fails verification.

Typical BAT flow:

- `POST /object/resolve`
- `GET /object/describe`
- `GET /object/get_property`
- `POST /object/set_property`
- `POST /object/call_function`
- `GET /object/get_property`
- `POST /editor/focus`
- `POST /editor/level/save`

Acceptance evidence:

- Every changed property was discovered before mutation.
- Original and final values are reported.
- Optional project-specific fields are skipped safely when absent.
- Read-back matches the requested target state.

## Prompt Design Pattern

Complex BAT prompts are most reliable when they contain five explicit parts:

1. **Target** — exact asset path, actor label, graph name, or map.
2. **Desired state** — the concrete editor result, not a vague instruction.
3. **Constraints** — objects to preserve, route restrictions, limits, and naming rules.
4. **Acceptance checks** — compile status, read-back values, graph links, audit counts, and rejected-operation counts.
5. **Persistence rule** — exactly when to save, when to stop, and how to clean up.

Example template:

> Inspect **[target]** and change it to **[desired state]**. Preserve **[protected content]** and use only **[allowed BAT surfaces]**. Before editing, capture **[baseline evidence]**. After editing, verify **[acceptance checks]**. Save only when **[success rule]**; otherwise leave the asset or level unsaved and report **[required diagnostics]**.

## Important Boundaries

- BAT is Editor-only and does not run in packaged games.
- BAT accepts loopback connections only; it is not a remote-control server.
- BAT does not provide arbitrary Slate UI automation.
- Editor-asset mutation routes are blocked during PIE.
- Gameplay and multiplayer behavior are not deterministic test-harness guarantees.
- Advanced execution and Python surfaces are optional policy-gated features, not requirements for the workflows above.
- Actual route availability, schemas, permissions, and limits must come from the running editor's discovery and OpenAPI responses.

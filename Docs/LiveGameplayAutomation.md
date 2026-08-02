# Live Gameplay Automation

Blueprint Automation Toolkit can exercise and record a running Unreal Editor
world without Python, console execution, FFmpeg, or a plugin-specific hard
dependency. The API is generic enough to test destruction, physics, vehicles,
interaction systems, animation, and other gameplay plugins.

## Surfaces

| Endpoint | Purpose | Permissions |
|---|---|---|
| `GET /capture/schema` | Discover formats, limits, and MP4 availability | `editor` |
| `POST /capture/session/start` | Start asynchronous editor/PIE capture | `editor`, `filesystem`, plus `pie` unless `source=editor` |
| `GET /capture/session/status` | Read progress and output paths | `editor` |
| `POST /capture/session/stop` | Finalize or discard an active session | `editor`, `filesystem` |
| `POST /pie/input` | Send a key press, release, or tap to a PIE player | `pie` |
| `POST /runtime/assert` | Verify live actor/property state | `editor`, plus `pie` unless `world=editor` |

Only one capture session runs at a time. Capture output is constrained beneath
the project `Saved` directory. A requested `output_folder` must be relative and
cannot traverse outside that root.

## Capture lifecycle

Start PIE through `POST /pie/start`, wait for its documented ready state, then:

```http
POST /capture/session/start
Content-Type: application/json

{
  "source": "pie",
  "pie_index": 0,
  "output_format": "both",
  "duration_seconds": 12,
  "fps": 30,
  "warmup_seconds": 1,
  "width": 1920,
  "height": 1080,
  "file_prefix": "shatterkit",
  "stop_pie_when_complete": false
}
```

The request returns `202` immediately. Unreal continues to tick gameplay while
BAT captures frames. Poll `GET /capture/session/status` until `state` is
`completed`, `failed`, or `canceled`. The response includes target/captured
frames, dropped frames, progress, first/last PNG, MP4, manifest, and error paths.

Formats:

- `png_sequence` reads the selected live viewport at the requested cadence.
- `mp4` uses Unreal's platform video recording system. Availability, minimum
  duration, maximum duration, and platform audio behavior are reported by
  `GET /capture/schema`.
- `both` records the platform MP4 and writes verification PNGs in one session.

`pie_index` precisely selects the viewport for PNG capture. Unreal's platform
MP4 recorder uses the primary game back buffer, so BAT accepts MP4 only with
`pie_index: 0`; use `png_sequence` to capture another multi-PIE instance.

BAT never launches an external encoder. If MP4 is unavailable in the current
editor session, use `png_sequence`; it remains suitable for visual diffs or for
encoding later in a user-controlled publishing workflow.

Stop early and preserve current output:

```json
{ "session_id": "20260802T120000Z-a1b2c3d4", "discard": false }
```

Set `discard` to `true` to cancel platform video finalization. Existing PNGs and
the cancellation manifest remain as an audit record.

## Typed PIE input

`POST /pie/input` supports Unreal key names and explicit events:

```json
{ "key": "W", "event": "press", "pie_index": 0, "player_index": 0 }
```

```json
{ "key": "W", "event": "release", "pie_index": 0, "player_index": 0 }
```

Use `tap` for a press followed immediately by release. Input is delivered to
the selected player's `PlayerInput`; BAT does not fake Slate focus or invoke OS
UI automation. A project's pawn, controller, Enhanced Input mapping contexts,
and current gameplay state determine the resulting behavior.

## Runtime assertions

Assertions return both expected and actual values. The route returns `200` only
when all checks pass and `422 runtime_assertion_failed` otherwise.

```json
{
  "world": "pie",
  "pie_index": 0,
  "assertions": [
    {
      "type": "actor_exists",
      "class": "ShatterkitProDynamicMeshActor",
      "tag": "BAT_ShowcaseTarget"
    },
    {
      "type": "actor_count",
      "name_contains": "Debris",
      "operator": "gt",
      "expected": 0
    },
    {
      "type": "property",
      "actor": "SKP_ShowcaseTarget",
      "property": "bCanBeDamaged",
      "operator": "eq",
      "expected": true,
      "tolerance": 0.01
    }
  ]
}
```

Supported assertion types are `actor_exists`, `actor_count`, and `property`.
Actor filters accept `class`, `tag`, `label_contains`, and `name_contains`.
Property targets accept an actor name/label/path plus optional tag, or an exact
`object_path`. Operators are `eq`, `ne`, `gt`, `gte`, `lt`, `lte`, and string
`contains`. Reflected properties must pass BAT's existing safe-read policy.

## ShatterKit showcase workflow

BAT does not need to link against ShatterKit. A Codex agent can use generic
reflection and live automation in this sequence:

1. Load a ShatterKit sample or test map and inspect the target actor through
   `/object/describe`.
2. Start PIE and a live capture session.
3. Trigger destruction through a documented input mapping or an allowlisted
   reflected Blueprint-callable function using `/object/call_function`.
4. Assert the target actor, generated pieces/debris count, and any exposed
   ShatterKit runtime state through `/runtime/assert`.
5. Poll capture completion, stop PIE, and report the exact manifest, PNG, and
   MP4 paths together with the structured assertion results.

This establishes the useful evidence chain: **prompt → live Unreal execution →
viewport recording → machine-verifiable runtime output**. Project-specific
acceptance checks still depend on the public properties/functions that the
tested gameplay plugin exposes.

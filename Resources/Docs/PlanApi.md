# Editor Bridge API

This document describes the bridge-oriented automation API for Unreal Editor.

All operations are API-first and run inside the plugin. External script files are not required.

Base URL:

`http://127.0.0.1:<Port>`

## Request Validation

All requests are validated by plugin endpoints:

- `ValidateAndHandleRequest`
- Bearer token auth
- Request size limit
- Rate limiting
- Safe mode checks

## Primary Agent Loop

1. `GET /engine/discover`
2. `POST /object/resolve` or `GET /object/describe`
3. `POST /blueprint/graph/apply` or another typed mutation route
4. `POST /blueprint/compile_save` or another validation/save route
5. Re-read state to verify the result

## Raw HTTP Example

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

## Optional Python Client Example

```python
import requests

base = "http://127.0.0.1:9876"
headers = {"Authorization": "Bearer YOUR_TOKEN"}

apply_payload = {
  "blueprint": "/Game/BP_Test",
  "graph": "EventGraph",
  "nodes": [
    {"id": "begin", "type": "K2Node_Event", "event": "BeginPlay", "x": 0, "y": 0}
  ],
  "links": []
}

r1 = requests.post(f"{base}/blueprint/graph/apply", json=apply_payload, headers=headers)
print(r1.json())

r2 = requests.post(
    f"{base}/blueprint/compile_save",
    json={"blueprint": "/Game/BP_Test"},
    headers=headers,
)
print(r2.json())

r3 = requests.get(
    f"{base}/blueprint/graph/links",
    params={"blueprint": "/Game/BP_Test", "graph": "EventGraph"},
    headers=headers,
)
print(r3.json())
```

## Event Node Variants

`POST /blueprint/graph/apply` supports more than `BeginPlay` for event authoring.

Actor event example:

```json
{
  "blueprint": "/Game/BP_PickupOrb",
  "graph": "EventGraph",
  "nodes": [
    {"id": "overlap", "type": "K2Node_Event", "event": "ActorBeginOverlap", "x": 0, "y": 0},
    {"id": "destroy", "type": "K2Node_CallFunction", "function": "/Script/Engine.Actor:K2_DestroyActor", "x": 320, "y": 0}
  ],
  "links": [
    {"from": "overlap.Then", "to": "destroy.execute"}
  ]
}
```

Component-bound event example:

```json
{
  "blueprint": "/Game/BP_PickupOrb",
  "graph": "EventGraph",
  "nodes": [
    {"id": "mesh_overlap", "type": "K2Node_ComponentBoundEvent", "component": "PickupMesh", "event": "OnComponentBeginOverlap", "x": 0, "y": 0},
    {"id": "destroy", "type": "K2Node_CallFunction", "function": "/Script/Engine.Actor:K2_DestroyActor", "x": 360, "y": 0}
  ],
  "links": [
    {"from": "mesh_overlap.Then", "to": "destroy.execute"}
  ]
}
```

## Response Envelope

All endpoints use this response envelope:

```json
{
  "ok": true,
  "requestId": "...",
  "data": {},
  "warnings": [],
  "errors": []
}
```

Failures use the same top-level shape and place structured issues in `errors`.

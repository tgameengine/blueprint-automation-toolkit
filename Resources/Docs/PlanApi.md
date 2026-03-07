# Plan API

This document describes the generic, plan-based automation API.

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

## Primary Blueprint Graph Flow

1. `POST /blueprint/graph/apply`
2. `POST /blueprint/compile_save`
3. `GET /blueprint/graph/links?blueprint=<path>&graph=<name>`

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

## Response Envelope

All endpoints use this response envelope:

```json
{
  "ok": true,
  "errors": [],
  "warnings": [],
  "data": {}
}
```

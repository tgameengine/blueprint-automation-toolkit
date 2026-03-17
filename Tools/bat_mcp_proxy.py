#!/usr/bin/env python3

import argparse
import json
import os
import sys
import traceback
import urllib.error
import urllib.parse
import urllib.request
from typing import Any, Dict, Optional


SERVER_NAME = "bat"
SERVER_TITLE = "Blueprint Automation Toolkit MCP Proxy"
SERVER_VERSION = "0.1.0"
DEFAULT_BASE_URL = "http://127.0.0.1:9876"
DEFAULT_TIMEOUT_SECONDS = 60.0
SUPPORTED_PROTOCOL_VERSION = "2024-11-05"


def _stderr(message: str) -> None:
    sys.stderr.write(message + "\n")
    sys.stderr.flush()


def _json_dumps(payload: Dict[str, Any]) -> bytes:
    return json.dumps(payload, separators=(",", ":"), ensure_ascii=True).encode("utf-8")


def _write_message(payload: Dict[str, Any]) -> None:
    body = _json_dumps(payload)
    header = f"Content-Length: {len(body)}\r\n\r\n".encode("ascii")
    sys.stdout.buffer.write(header)
    sys.stdout.buffer.write(body)
    sys.stdout.buffer.flush()


def _read_message() -> Optional[Dict[str, Any]]:
    headers: Dict[str, str] = {}
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        if line in (b"\r\n", b"\n"):
            break
        decoded = line.decode("ascii", errors="replace").strip()
        if not decoded:
            break
        name, _, value = decoded.partition(":")
        headers[name.lower()] = value.strip()

    length_text = headers.get("content-length")
    if not length_text:
        raise ValueError("Missing Content-Length header")

    length = int(length_text)
    body = sys.stdin.buffer.read(length)
    if len(body) != length:
        raise ValueError("Unexpected EOF while reading message body")

    return json.loads(body.decode("utf-8"))


def _make_text_result(payload: Any, *, is_error: bool = False) -> Dict[str, Any]:
    text = json.dumps(payload, indent=2, ensure_ascii=True, sort_keys=True)
    result: Dict[str, Any] = {
        "content": [
            {
                "type": "text",
                "text": text,
            }
        ],
        "structuredContent": payload,
    }
    if is_error:
        result["isError"] = True
    return result


class BatClient:
    def __init__(self, base_url: str, auth_token: str, timeout_seconds: float) -> None:
        self.base_url = base_url.rstrip("/")
        self.auth_token = auth_token.strip()
        self.timeout_seconds = timeout_seconds

    def request(
        self,
        method: str,
        path: str,
        *,
        query: Optional[Dict[str, Any]] = None,
        json_body: Optional[Dict[str, Any]] = None,
        extra_headers: Optional[Dict[str, str]] = None,
    ) -> Dict[str, Any]:
        normalized_path = path if path.startswith("/") else f"/{path}"
        url = self.base_url + normalized_path
        if query:
            encoded_query = urllib.parse.urlencode(query, doseq=True)
            if encoded_query:
                url = f"{url}?{encoded_query}"

        body_bytes: Optional[bytes] = None
        headers = {
            "Accept": "application/json",
        }
        if self.auth_token:
            headers["Authorization"] = f"Bearer {self.auth_token}"
        if extra_headers:
            headers.update(extra_headers)
        if json_body is not None:
            body_bytes = json.dumps(json_body).encode("utf-8")
            headers["Content-Type"] = "application/json"

        request = urllib.request.Request(url=url, data=body_bytes, method=method.upper(), headers=headers)
        try:
            with urllib.request.urlopen(request, timeout=self.timeout_seconds) as response:
                status_code = getattr(response, "status", 200)
                raw_body = response.read().decode("utf-8", errors="replace")
                parsed_body = self._parse_json(raw_body)
                return {
                    "ok": True,
                    "status": status_code,
                    "url": url,
                    "method": method.upper(),
                    "headers": dict(response.headers.items()),
                    "body": parsed_body,
                }
        except urllib.error.HTTPError as error:
            raw_body = error.read().decode("utf-8", errors="replace")
            parsed_body = self._parse_json(raw_body)
            return {
                "ok": False,
                "status": error.code,
                "url": url,
                "method": method.upper(),
                "headers": dict(error.headers.items()) if error.headers else {},
                "body": parsed_body,
            }
        except urllib.error.URLError as error:
            reason = getattr(error, "reason", str(error))
            return {
                "ok": False,
                "status": None,
                "url": url,
                "method": method.upper(),
                "error": f"transport_error: {reason}",
            }

    @staticmethod
    def _parse_json(raw_body: str) -> Any:
        if not raw_body:
            return None
        try:
            return json.loads(raw_body)
        except json.JSONDecodeError:
            return raw_body


def _tool_definitions() -> list[Dict[str, Any]]:
    return [
        {
            "name": "bat_discover",
            "description": "Call GET /engine/discover to retrieve BAT capabilities, route metadata, and runtime gates.",
            "inputSchema": {
                "type": "object",
                "properties": {},
                "additionalProperties": False,
            },
        },
        {
            "name": "bat_health",
            "description": "Call GET /health to verify the BAT server is reachable and healthy.",
            "inputSchema": {
                "type": "object",
                "properties": {},
                "additionalProperties": False,
            },
        },
        {
            "name": "bat_object_resolve",
            "description": "Call POST /object/resolve with a BAT resolve payload to locate an asset, actor, or reflected object.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "payload": {
                        "type": "object",
                        "description": "JSON body forwarded to POST /object/resolve.",
                    }
                },
                "required": ["payload"],
                "additionalProperties": False,
            },
        },
        {
            "name": "bat_object_describe",
            "description": "Call GET /object/describe with BAT query parameters to inspect reflected object metadata.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "query": {
                        "type": "object",
                        "description": "Query parameters forwarded to GET /object/describe.",
                    }
                },
                "required": ["query"],
                "additionalProperties": False,
            },
        },
        {
            "name": "bat_blueprint_graph_read",
            "description": "Call GET /blueprint/graph/read to inspect Blueprint graph contents and schema metadata.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "query": {
                        "type": "object",
                        "description": "Query parameters forwarded to GET /blueprint/graph/read.",
                    }
                },
                "required": ["query"],
                "additionalProperties": False,
            },
        },
        {
            "name": "bat_blueprint_graph_apply",
            "description": "Call POST /blueprint/graph/apply with a full BAT graph apply payload.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "payload": {
                        "type": "object",
                        "description": "JSON body forwarded to POST /blueprint/graph/apply.",
                    }
                },
                "required": ["payload"],
                "additionalProperties": False,
            },
        },
        {
            "name": "bat_blueprint_compile_save",
            "description": "Call POST /blueprint/compile_save to compile and optionally save a Blueprint.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "payload": {
                        "type": "object",
                        "description": "JSON body forwarded to POST /blueprint/compile_save.",
                    }
                },
                "required": ["payload"],
                "additionalProperties": False,
            },
        },
        {
            "name": "bat_request",
            "description": "Make a raw request to any BAT endpoint using the configured base URL and bearer token.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "method": {
                        "type": "string",
                        "description": "HTTP method such as GET or POST.",
                    },
                    "path": {
                        "type": "string",
                        "description": "BAT route path, for example /engine/discover.",
                    },
                    "query": {
                        "type": "object",
                        "description": "Optional query parameters.",
                    },
                    "payload": {
                        "type": "object",
                        "description": "Optional JSON request body.",
                    }
                },
                "required": ["method", "path"],
                "additionalProperties": False,
            },
        },
    ]


def _initialize_result(client_protocol_version: Optional[str]) -> Dict[str, Any]:
    protocol_version = client_protocol_version or SUPPORTED_PROTOCOL_VERSION
    return {
        "protocolVersion": protocol_version,
        "capabilities": {
            "tools": {
                "listChanged": False,
            },
        },
        "serverInfo": {
            "name": SERVER_NAME,
            "title": SERVER_TITLE,
            "version": SERVER_VERSION,
        },
    }


def _handle_tool_call(client: BatClient, name: str, arguments: Dict[str, Any]) -> Dict[str, Any]:
    if name == "bat_discover":
        return _make_text_result(client.request("GET", "/engine/discover"))
    if name == "bat_health":
        return _make_text_result(client.request("GET", "/health"))
    if name == "bat_object_resolve":
        return _make_text_result(client.request("POST", "/object/resolve", json_body=_require_object(arguments, "payload")))
    if name == "bat_object_describe":
        return _make_text_result(client.request("GET", "/object/describe", query=_require_object(arguments, "query")))
    if name == "bat_blueprint_graph_read":
        return _make_text_result(client.request("GET", "/blueprint/graph/read", query=_require_object(arguments, "query")))
    if name == "bat_blueprint_graph_apply":
        return _make_text_result(client.request("POST", "/blueprint/graph/apply", json_body=_require_object(arguments, "payload")))
    if name == "bat_blueprint_compile_save":
        return _make_text_result(client.request("POST", "/blueprint/compile_save", json_body=_require_object(arguments, "payload")))
    if name == "bat_request":
        method = _require_string(arguments, "method")
        path = _require_string(arguments, "path")
        query = _optional_object(arguments, "query")
        payload = _optional_object(arguments, "payload")
        return _make_text_result(client.request(method, path, query=query, json_body=payload))
    raise KeyError(f"Unknown tool: {name}")


def _require_object(arguments: Dict[str, Any], key: str) -> Dict[str, Any]:
    value = arguments.get(key)
    if not isinstance(value, dict):
        raise ValueError(f"Expected '{key}' to be an object")
    return value


def _optional_object(arguments: Dict[str, Any], key: str) -> Optional[Dict[str, Any]]:
    if key not in arguments or arguments.get(key) is None:
        return None
    return _require_object(arguments, key)


def _require_string(arguments: Dict[str, Any], key: str) -> str:
    value = arguments.get(key)
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"Expected '{key}' to be a non-empty string")
    return value.strip()


def _jsonrpc_result(message_id: Any, result: Dict[str, Any]) -> Dict[str, Any]:
    return {
        "jsonrpc": "2.0",
        "id": message_id,
        "result": result,
    }


def _jsonrpc_error(message_id: Any, code: int, message: str, data: Optional[Any] = None) -> Dict[str, Any]:
    error: Dict[str, Any] = {
        "code": code,
        "message": message,
    }
    if data is not None:
        error["data"] = data
    return {
        "jsonrpc": "2.0",
        "id": message_id,
        "error": error,
    }


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="BAT stdio MCP proxy")
    parser.add_argument("--base-url", default=os.environ.get("BAT_BASE_URL", DEFAULT_BASE_URL))
    parser.add_argument("--auth-token", default=os.environ.get("BAT_AUTH_TOKEN", ""))
    parser.add_argument("--timeout-seconds", type=float, default=float(os.environ.get("BAT_TIMEOUT_SECONDS", DEFAULT_TIMEOUT_SECONDS)))
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    client = BatClient(args.base_url, args.auth_token, args.timeout_seconds)

    while True:
        try:
            message = _read_message()
            if message is None:
                return 0

            message_id = message.get("id")
            method = message.get("method")
            params = message.get("params") or {}

            if method == "initialize":
                protocol_version = params.get("protocolVersion") if isinstance(params, dict) else None
                _write_message(_jsonrpc_result(message_id, _initialize_result(protocol_version)))
                continue

            if method == "notifications/initialized":
                continue

            if method == "ping":
                _write_message(_jsonrpc_result(message_id, {}))
                continue

            if method == "tools/list":
                _write_message(_jsonrpc_result(message_id, {"tools": _tool_definitions()}))
                continue

            if method == "tools/call":
                if not isinstance(params, dict):
                    _write_message(_jsonrpc_error(message_id, -32602, "Invalid params"))
                    continue
                tool_name = params.get("name")
                tool_args = params.get("arguments") or {}
                if not isinstance(tool_name, str):
                    _write_message(_jsonrpc_error(message_id, -32602, "Missing tool name"))
                    continue
                if not isinstance(tool_args, dict):
                    _write_message(_jsonrpc_error(message_id, -32602, "Tool arguments must be an object"))
                    continue
                try:
                    result = _handle_tool_call(client, tool_name, tool_args)
                    _write_message(_jsonrpc_result(message_id, result))
                except Exception as error:  # pragma: no cover - defensive boundary
                    _write_message(
                        _jsonrpc_result(
                            message_id,
                            _make_text_result(
                                {
                                    "ok": False,
                                    "error": str(error),
                                    "tool": tool_name,
                                },
                                is_error=True,
                            ),
                        )
                    )
                continue

            if message_id is not None:
                _write_message(_jsonrpc_error(message_id, -32601, f"Method not found: {method}"))
        except Exception as error:  # pragma: no cover - top-level guard
            _stderr(f"bat_mcp_proxy fatal error: {error}")
            _stderr(traceback.format_exc())
            if isinstance(error, KeyboardInterrupt):
                return 130
            message_id = None
            try:
                message_id = message.get("id") if isinstance(message, dict) else None  # type: ignore[name-defined]
            except Exception:
                message_id = None
            if message_id is not None:
                _write_message(_jsonrpc_error(message_id, -32603, "Internal error", str(error)))


if __name__ == "__main__":
    raise SystemExit(main())
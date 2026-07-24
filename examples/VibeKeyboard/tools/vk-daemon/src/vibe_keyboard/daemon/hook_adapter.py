"""CLI adapter installed into Claude Code and Codex hook configuration."""

from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from typing import Any

from .terminal import detect_current_terminal


def build_payload(raw: dict[str, Any], event_type: str, codex: bool = False) -> dict[str, Any]:
    terminal = detect_current_terminal()
    actual_type = str(raw.get("hook_event_name", event_type))
    session_id = str(raw.get("session_id", raw.get("thread-id", raw.get("thread_id", ""))))
    tool_input = raw.get("tool_input", raw.get("input", ""))
    if not isinstance(tool_input, str):
        tool_input = json.dumps(tool_input, separators=(",", ":"), ensure_ascii=False)
    return {
        "type": actual_type,
        "session_id": session_id,
        "name": str(raw.get("name", "")),
        "status": str(raw.get("status", "")),
        "tool_name": str(raw.get("tool_name", raw.get("tool", ""))),
        "tool_input": tool_input,
        "source": "codex" if codex else "claude-code",
        "cwd": str(raw.get("cwd", os.getcwd())),
        "permission_mode": str(raw.get("permission_mode", "")),
        "transcript_path": str(raw.get("transcript_path", "")),
        "bundle_id": terminal.bundle_id,
        "session_tty": terminal.session_tty,
        "error": str(raw.get("error", "")),
    }


def format_response(
    response: dict[str, Any], payload: dict[str, Any], codex: bool = False
) -> dict[str, Any]:
    if not codex or payload.get("type") != "PermissionRequest":
        return response
    hook_output = response.get("hookSpecificOutput")
    if isinstance(hook_output, dict):
        hook_output["hookEventName"] = "PermissionRequest"
    return response


def post_event(payload: dict[str, Any], port: int, timeout: float = 305.0) -> dict[str, Any]:
    request = urllib.request.Request(
        f"http://127.0.0.1:{port}/event",
        data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json", "X-Vibe-Keyboard": "hook"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        body = response.read(1_048_576)
    parsed = json.loads(body or b"{}")
    return parsed if isinstance(parsed, dict) else {}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=19280)
    parser.add_argument("--event", default="Notification")
    parser.add_argument("--codex", action="store_true")
    args = parser.parse_args(argv)
    try:
        raw = json.load(sys.stdin)
        if not isinstance(raw, dict):
            raw = {}
    except json.JSONDecodeError:
        raw = {}
    try:
        payload = build_payload(raw, args.event, args.codex)
        response = post_event(payload, args.port)
    except (OSError, urllib.error.URLError, json.JSONDecodeError):
        response = {
            "hookSpecificOutput": {"decision": {"behavior": "deny"}}
        } if args.event in {"PreToolUse", "PermissionRequest"} else {}
        payload = {"type": args.event}
    response = format_response(response, payload, args.codex)
    if response:
        json.dump(response, sys.stdout, separators=(",", ":"))
        sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""Normalize Claude Code, Codex, and generic hook payloads."""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import asdict, dataclass
from enum import StrEnum
from pathlib import Path
from typing import Any

from ...core import SessionStatus


class SessionEventKind(StrEnum):
    STARTED = "started"
    ENDED = "ended"
    STATUS_CHANGED = "status_changed"
    PERMISSION_REQUEST = "permission_request"


@dataclass(slots=True)
class SessionEvent:
    kind: SessionEventKind
    session_id: str
    name: str = ""
    status: SessionStatus | None = None
    source: str = ""
    cwd: str = ""
    bundle_id: str = ""
    session_tty: str = ""
    transcript_path: str = ""
    tool_name: str = ""
    tool_input: str = ""


@dataclass(slots=True)
class HookEvent:
    event_type: str = ""
    session_id: str = ""
    name: str = ""
    status: str = ""
    tool_name: str = ""
    tool_input: str = ""
    source: str = ""
    cwd: str = ""
    permission_mode: str = ""
    transcript_path: str = ""
    bundle_id: str = ""
    session_tty: str = ""
    error: str = ""

    @classmethod
    def from_dict(cls, value: Mapping[str, Any]) -> HookEvent:
        def text(key: str) -> str:
            raw = value.get(key, "")
            if isinstance(raw, (dict, list)):
                import json

                return json.dumps(raw, separators=(",", ":"), ensure_ascii=False)
            return "" if raw is None else str(raw)

        return cls(
            event_type=text("type") or text("event_type"),
            session_id=text("session_id"),
            name=text("name"),
            status=text("status"),
            tool_name=text("tool_name"),
            tool_input=text("tool_input"),
            source=text("source"),
            cwd=text("cwd"),
            permission_mode=text("permission_mode"),
            transcript_path=text("transcript_path"),
            bundle_id=text("bundle_id"),
            session_tty=text("session_tty"),
            error=text("error"),
        )

    def to_dict(self) -> dict[str, str]:
        result = asdict(self)
        result["type"] = result.pop("event_type")
        return result


_STATUS_BY_NAME = {
    "thinking": SessionStatus.THINKING,
    "tool_use": SessionStatus.TOOL_USE,
    "writing": SessionStatus.WRITING,
    "done": SessionStatus.DONE,
    "error": SessionStatus.ERROR,
    "idle": SessionStatus.IDLE,
    "permission_needed": SessionStatus.PERMISSION_NEEDED,
}


def parse_hook_event(event: HookEvent) -> SessionEvent | None:
    kind = event.event_type
    if kind in {"SessionStart", "session_start", "init"}:
        fallback = f"S-{event.session_id[:8]}"
        name = event.name or (Path(event.cwd).name if event.cwd else fallback) or fallback
        return SessionEvent(
            SessionEventKind.STARTED,
            event.session_id,
            name=name,
            source=event.source,
            cwd=event.cwd,
            bundle_id=event.bundle_id,
            session_tty=event.session_tty,
            transcript_path=event.transcript_path,
        )
    if kind in {"SessionEnd", "session_end", "exit"}:
        return SessionEvent(SessionEventKind.ENDED, event.session_id)
    if kind == "PreToolUse":
        return SessionEvent(SessionEventKind.STATUS_CHANGED, event.session_id, status=SessionStatus.TOOL_USE)
    if kind == "PostToolUse":
        return SessionEvent(SessionEventKind.STATUS_CHANGED, event.session_id, status=SessionStatus.WRITING)
    if kind == "Notification":
        if "permission" in event.tool_name or event.tool_input:
            return SessionEvent(
                SessionEventKind.PERMISSION_REQUEST,
                event.session_id,
                tool_name=event.tool_name,
                tool_input=event.tool_input,
            )
        return SessionEvent(SessionEventKind.STATUS_CHANGED, event.session_id, status=SessionStatus.THINKING)
    direct_status = {
        "UserPromptSubmit": SessionStatus.THINKING,
        "Stop": SessionStatus.DONE,
        "SubagentStart": SessionStatus.THINKING,
        "SubagentStop": SessionStatus.WRITING,
    }.get(kind)
    if direct_status is not None:
        return SessionEvent(SessionEventKind.STATUS_CHANGED, event.session_id, status=direct_status)
    if kind in {"status", "tool_use", "message"}:
        status = SessionStatus.TOOL_USE if kind == "tool_use" else _STATUS_BY_NAME.get(event.status)
        if status is None:
            return None
        return SessionEvent(SessionEventKind.STATUS_CHANGED, event.session_id, status=status)
    if kind in {"permission", "permission_request"}:
        return SessionEvent(
            SessionEventKind.PERMISSION_REQUEST,
            event.session_id,
            tool_name=event.tool_name,
            tool_input=event.tool_input,
        )
    return None


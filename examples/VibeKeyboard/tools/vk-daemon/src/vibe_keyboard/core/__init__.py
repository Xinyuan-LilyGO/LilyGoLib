"""Dependency-free domain types shared by every Vibe Keyboard package."""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass, fields, replace
from enum import StrEnum
from typing import Any, ClassVar, Self


class ButtonId(StrEnum):
    """Button identifiers in physical-layout and wire-protocol order."""

    DELETE = "delete"
    CANCEL = "cancel"
    MODE = "mode"
    SESSION = "session"
    SEND = "send"
    VOICE = "voice"


class Direction(StrEnum):
    """Rotary encoder direction."""

    CLOCKWISE = "clockwise"
    COUNTER_CLOCKWISE = "counter_clockwise"


@dataclass(frozen=True, slots=True)
class LedColor:
    """An RGB LED color."""

    r: int
    g: int
    b: int

    OFF: ClassVar[LedColor]
    GREEN: ClassVar[LedColor]
    AMBER: ClassVar[LedColor]
    RED: ClassVar[LedColor]
    ORANGE: ClassVar[LedColor]

    def __post_init__(self) -> None:
        for name, value in (("r", self.r), ("g", self.g), ("b", self.b)):
            if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0xFF:
                raise ValueError(f"{name} must be an integer from 0 to 255")

    def to_dict(self) -> dict[str, int]:
        return {"r": self.r, "g": self.g, "b": self.b}

    @classmethod
    def from_dict(cls, value: Mapping[str, Any]) -> Self:
        return cls(r=int(value["r"]), g=int(value["g"]), b=int(value["b"]))


LedColor.OFF = LedColor(0, 0, 0)
LedColor.GREEN = LedColor(0, 200, 0)
LedColor.AMBER = LedColor(245, 158, 11)
LedColor.RED = LedColor(239, 68, 68)
LedColor.ORANGE = LedColor(255, 140, 0)


class SessionStatus(StrEnum):
    """Session status serialized as the Rust implementation's snake-case value."""

    THINKING = "thinking"
    TOOL_USE = "tool_use"
    WRITING = "writing"
    DONE = "done"
    ERROR = "error"
    IDLE = "idle"
    PERMISSION_NEEDED = "permission_needed"

    def priority(self) -> int:
        """Return notification priority, where a lower number is more urgent."""

        if self is SessionStatus.PERMISSION_NEEDED:
            return 0
        if self is SessionStatus.ERROR:
            return 1
        if self in {SessionStatus.THINKING, SessionStatus.TOOL_USE, SessionStatus.WRITING}:
            return 2
        if self is SessionStatus.DONE:
            return 3
        return 4


class PermissionAction(StrEnum):
    """A response to a permission request."""

    ALLOW = "allow"
    DENY = "deny"
    ALWAYS = "always"


class SoundType(StrEnum):
    """Built-in semantic sound events."""

    PERMISSION_ALERT = "permission_alert"
    SESSION_COMPLETE = "session_complete"
    ERROR = "error"
    CLICK = "click"


@dataclass(slots=True)
class SessionInfo:
    """Complete device-facing session snapshot.

    Field order is intentionally identical to the Rust binary codec. The object is
    mutable because daemon scanners enrich it incrementally.
    """

    id: int = 0
    name: str = ""
    status: SessionStatus = SessionStatus.IDLE
    has_permission_request: bool = False
    source: str = ""
    cwd: str = ""
    permission_mode: str = ""
    model: str = ""
    tokens_in: int = 0
    tokens_out: int = 0
    cost_usd: float = 0.0
    context_pct: int = 0
    last_message: str = ""
    last_ai_output: str = ""
    bundle_id: str = ""
    session_tty: str = ""
    started_at: int = 0
    last_activity: int = 0

    def copy(self, **changes: Any) -> SessionInfo:
        """Return an independent shallow copy, optionally replacing fields."""

        return replace(self, **changes)

    @property
    def has_permission(self) -> bool:
        """Compatibility alias used by the HTTP API."""

        return self.has_permission_request

    @has_permission.setter
    def has_permission(self, value: bool) -> None:
        self.has_permission_request = value

    def to_dict(self) -> dict[str, Any]:
        result = {field.name: getattr(self, field.name) for field in fields(self)}
        result["status"] = self.status.value
        return result

    def to_json_dict(self) -> dict[str, Any]:
        return self.to_dict()

    @classmethod
    def from_dict(cls, value: Mapping[str, Any]) -> Self:
        data = {field.name: value[field.name] for field in fields(cls) if field.name in value}
        if "has_permission_request" not in data and "has_permission" in value:
            data["has_permission_request"] = value["has_permission"]
        if "status" in data and not isinstance(data["status"], SessionStatus):
            data["status"] = SessionStatus(str(data["status"]))
        return cls(**data)

    @classmethod
    def from_json_dict(cls, value: Mapping[str, Any]) -> Self:
        return cls.from_dict(value)


@dataclass(slots=True)
class NotificationInfo:
    """Notification snapshot sent through the device or HTTP interfaces."""

    id: int = 0
    session_id: int = 0
    session_name: str = ""
    status: SessionStatus = SessionStatus.IDLE
    description: str = ""
    timestamp: int = 0
    read: bool = False

    def copy(self, **changes: Any) -> NotificationInfo:
        return replace(self, **changes)

    def to_dict(self) -> dict[str, Any]:
        return {
            "id": self.id,
            "session_id": self.session_id,
            "session_name": self.session_name,
            "status": self.status.value,
            "description": self.description,
            "timestamp": self.timestamp,
            "read": self.read,
        }

    def to_json_dict(self) -> dict[str, Any]:
        return self.to_dict()

    @classmethod
    def from_dict(cls, value: Mapping[str, Any]) -> Self:
        status = value.get("status", SessionStatus.IDLE)
        if not isinstance(status, SessionStatus):
            status = SessionStatus(str(status))
        return cls(
            id=int(value.get("id", 0)),
            session_id=int(value.get("session_id", 0)),
            session_name=str(value.get("session_name", "")),
            status=status,
            description=str(value.get("description", "")),
            timestamp=int(value.get("timestamp", 0)),
            read=bool(value.get("read", False)),
        )

    @classmethod
    def from_json_dict(cls, value: Mapping[str, Any]) -> Self:
        return cls.from_dict(value)


__all__ = [
    "ButtonId",
    "Direction",
    "LedColor",
    "NotificationInfo",
    "PermissionAction",
    "SessionInfo",
    "SessionStatus",
    "SoundType",
]

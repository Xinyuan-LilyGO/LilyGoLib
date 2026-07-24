"""Typed messages exchanged between keyboard transports and the daemon."""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass

from vibe_keyboard.core import (
    ButtonId,
    Direction,
    LedColor,
    NotificationInfo,
    PermissionAction,
    SessionInfo,
    SessionStatus,
    SoundType,
)


class UplinkMessage:
    """Base class for keyboard-to-daemon messages."""

    __slots__ = ()


@dataclass(frozen=True, slots=True)
class ButtonPress(UplinkMessage):
    button: ButtonId


@dataclass(frozen=True, slots=True)
class ButtonRelease(UplinkMessage):
    button: ButtonId


@dataclass(frozen=True, slots=True)
class KnobRotate(UplinkMessage):
    direction: Direction
    steps: int


@dataclass(frozen=True, slots=True)
class KnobPress(UplinkMessage):
    pass


@dataclass(frozen=True, slots=True)
class KnobRelease(UplinkMessage):
    pass


@dataclass(frozen=True, slots=True)
class PermissionResponse(UplinkMessage):
    session_id: int
    action: PermissionAction


@dataclass(frozen=True, slots=True)
class SessionSwitch(UplinkMessage):
    session_id: int


@dataclass(frozen=True, slots=True)
class SetupActionRequest(UplinkMessage):
    request_id: int
    action_id: int
    tool: str
    command: str
    daemon_port: int


@dataclass(frozen=True, slots=True)
class SetupStatusRequest(UplinkMessage):
    request_id: int
    daemon_port: int


@dataclass(frozen=True, slots=True)
class TimeSyncRequest(UplinkMessage):
    pass


@dataclass(frozen=True, slots=True)
class YoloConfigRequest(UplinkMessage):
    pass


@dataclass(frozen=True, slots=True)
class YoloConfigSet(UplinkMessage):
    active: bool
    notify_auto_allow: bool
    allow: tuple[str, ...]
    deny: tuple[str, ...]

    def __init__(
        self,
        active: bool,
        notify_auto_allow: bool,
        allow: Sequence[str],
        deny: Sequence[str],
    ) -> None:
        object.__setattr__(self, "active", active)
        object.__setattr__(self, "notify_auto_allow", notify_auto_allow)
        object.__setattr__(self, "allow", tuple(allow))
        object.__setattr__(self, "deny", tuple(deny))


class DownlinkMessage:
    """Base class for daemon-to-keyboard messages."""

    __slots__ = ()


@dataclass(frozen=True, slots=True)
class SessionListUpdate(DownlinkMessage):
    sessions: tuple[SessionInfo, ...]
    active_index: int

    def __init__(self, sessions: Sequence[SessionInfo], active_index: int) -> None:
        object.__setattr__(self, "sessions", tuple(sessions))
        object.__setattr__(self, "active_index", active_index)


@dataclass(frozen=True, slots=True)
class SessionStatusChange(DownlinkMessage):
    session_id: int
    status: SessionStatus


@dataclass(frozen=True, slots=True)
class SessionListClear(DownlinkMessage):
    active_session_id: int


@dataclass(frozen=True, slots=True)
class SessionUpsert(DownlinkMessage):
    session: SessionInfo
    active: bool


@dataclass(frozen=True, slots=True)
class SessionRemove(DownlinkMessage):
    session_id: int


@dataclass(frozen=True, slots=True)
class PermissionRequest(DownlinkMessage):
    session_id: int
    action_desc: str


@dataclass(frozen=True, slots=True)
class SetLed(DownlinkMessage):
    button: ButtonId
    color: LedColor
    blink: bool


@dataclass(frozen=True, slots=True)
class SetKnobRing(DownlinkMessage):
    color: LedColor


@dataclass(frozen=True, slots=True)
class PlaySound(DownlinkMessage):
    sound: SoundType


@dataclass(frozen=True, slots=True)
class DismissPermission(DownlinkMessage):
    session_id: int


@dataclass(frozen=True, slots=True)
class NotificationListUpdate(DownlinkMessage):
    notifications: tuple[NotificationInfo, ...]

    def __init__(self, notifications: Sequence[NotificationInfo]) -> None:
        object.__setattr__(self, "notifications", tuple(notifications))


@dataclass(frozen=True, slots=True)
class FrameData(DownlinkMessage):
    width: int
    height: int
    pixels: bytes

    def __init__(self, width: int, height: int, pixels: bytes | bytearray | memoryview) -> None:
        object.__setattr__(self, "width", width)
        object.__setattr__(self, "height", height)
        object.__setattr__(self, "pixels", bytes(pixels))


@dataclass(frozen=True, slots=True)
class SetVolume(DownlinkMessage):
    volume: int


@dataclass(frozen=True, slots=True)
class SetMuted(DownlinkMessage):
    muted: bool


@dataclass(frozen=True, slots=True)
class SetSoundMapping(DownlinkMessage):
    sound_type: SoundType
    sound_id: str


@dataclass(frozen=True, slots=True)
class SetupActionResult(DownlinkMessage):
    request_id: int
    success: bool


@dataclass(frozen=True, slots=True)
class SetupToolStatus:
    id: str
    name: str
    detected: bool
    hook_installed: bool
    detail: str = ""


@dataclass(frozen=True, slots=True)
class SetupStatusUpdate(DownlinkMessage):
    request_id: int
    tools: tuple[SetupToolStatus, ...]

    def __init__(self, request_id: int, tools: Sequence[SetupToolStatus]) -> None:
        object.__setattr__(self, "request_id", request_id)
        object.__setattr__(self, "tools", tuple(tools))


@dataclass(frozen=True, slots=True)
class TimeSync(DownlinkMessage):
    unix_time_ms: int
    utc_offset_minutes: int


@dataclass(frozen=True, slots=True)
class YoloConfigUpdate(DownlinkMessage):
    active: bool
    notify_auto_allow: bool
    allow: tuple[str, ...]
    deny: tuple[str, ...]

    def __init__(
        self,
        active: bool,
        notify_auto_allow: bool,
        allow: Sequence[str],
        deny: Sequence[str],
    ) -> None:
        object.__setattr__(self, "active", active)
        object.__setattr__(self, "notify_auto_allow", notify_auto_allow)
        object.__setattr__(self, "allow", tuple(allow))
        object.__setattr__(self, "deny", tuple(deny))


__all__ = [
    "ButtonPress",
    "ButtonRelease",
    "DismissPermission",
    "DownlinkMessage",
    "FrameData",
    "KnobPress",
    "KnobRelease",
    "KnobRotate",
    "NotificationListUpdate",
    "PermissionRequest",
    "PermissionResponse",
    "PlaySound",
    "SessionListClear",
    "SessionListUpdate",
    "SessionRemove",
    "SessionStatusChange",
    "SessionSwitch",
    "SessionUpsert",
    "SetKnobRing",
    "SetLed",
    "SetMuted",
    "SetSoundMapping",
    "SetVolume",
    "SetupActionRequest",
    "SetupActionResult",
    "SetupStatusRequest",
    "SetupStatusUpdate",
    "SetupToolStatus",
    "TimeSync",
    "TimeSyncRequest",
    "UplinkMessage",
    "YoloConfigRequest",
    "YoloConfigSet",
    "YoloConfigUpdate",
]

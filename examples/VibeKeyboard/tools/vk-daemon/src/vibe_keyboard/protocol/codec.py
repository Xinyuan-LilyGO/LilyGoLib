"""Wire-compatible binary codec for Vibe Keyboard protocol messages.

The format intentionally matches the Rust implementation byte for byte:
``[tag:u8][fields...]``, little-endian integers, and UTF-8 strings prefixed by
their byte length as ``u16``.
"""

from __future__ import annotations

import struct

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

from .messages import (
    ButtonPress,
    ButtonRelease,
    DismissPermission,
    DownlinkMessage,
    FrameData,
    KnobPress,
    KnobRelease,
    KnobRotate,
    NotificationListUpdate,
    PermissionRequest,
    PermissionResponse,
    PlaySound,
    SessionListClear,
    SessionListUpdate,
    SessionRemove,
    SessionStatusChange,
    SessionSwitch,
    SessionUpsert,
    SetKnobRing,
    SetLed,
    SetMuted,
    SetSoundMapping,
    SetupActionRequest,
    SetupActionResult,
    SetupStatusRequest,
    SetupStatusUpdate,
    SetupToolStatus,
    SetVolume,
    TimeSync,
    TimeSyncRequest,
    UplinkMessage,
    YoloConfigRequest,
    YoloConfigSet,
    YoloConfigUpdate,
)

TAG_BUTTON_PRESS = 0x01
TAG_BUTTON_RELEASE = 0x02
TAG_KNOB_ROTATE = 0x03
TAG_KNOB_PRESS = 0x04
TAG_KNOB_RELEASE = 0x05
TAG_PERMISSION_RESPONSE = 0x06
TAG_SESSION_SWITCH = 0x07
TAG_SETUP_ACTION_REQUEST = 0x08
TAG_SETUP_STATUS_REQUEST = 0x09
TAG_TIME_SYNC_REQUEST = 0x0A
TAG_YOLO_CONFIG_REQUEST = 0x0B
TAG_YOLO_CONFIG_SET = 0x0C

TAG_SESSION_LIST_UPDATE = 0x81
TAG_SESSION_STATUS_CHANGE = 0x82
TAG_PERMISSION_REQUEST = 0x83
TAG_SET_LED = 0x84
TAG_SET_KNOB_RING = 0x85
TAG_PLAY_SOUND = 0x86
TAG_DISMISS_PERMISSION = 0x87
TAG_FRAME_DATA = 0x88
TAG_NOTIFICATION_LIST_UPDATE = 0x89
TAG_SET_VOLUME = 0x8A
TAG_SET_MUTED = 0x8B
TAG_SET_SOUND_MAPPING = 0x8C
TAG_SETUP_ACTION_RESULT = 0x8D
TAG_SESSION_LIST_CLEAR = 0x8E
TAG_SESSION_UPSERT = 0x8F
TAG_SESSION_REMOVE = 0x90
TAG_SETUP_STATUS_UPDATE = 0x91
TAG_TIME_SYNC = 0x92
TAG_YOLO_CONFIG_UPDATE = 0x93


class CodecError(ValueError):
    """Base class for all deterministic codec failures."""


class InvalidTag(CodecError):
    def __init__(self, tag: int) -> None:
        self.tag = tag
        super().__init__(f"invalid tag: 0x{tag:02X}")


class BufferTooShort(CodecError):
    def __init__(self) -> None:
        super().__init__("buffer too short")


class InvalidUtf8(CodecError):
    def __init__(self) -> None:
        super().__init__("invalid UTF-8 in string field")


class InvalidData(CodecError):
    def __init__(self, message: str) -> None:
        self.message = message
        super().__init__(f"invalid data: {message}")


class StringTooLong(CodecError):
    def __init__(self, length: int) -> None:
        self.length = length
        super().__init__(f"string too long: {length} bytes (max 65535)")


class TooManyItems(CodecError):
    def __init__(self, count: int) -> None:
        self.count = count
        super().__init__(f"too many items: {count} (max 255)")


_BUTTON_TO_BYTE = {
    ButtonId.DELETE: 0,
    ButtonId.CANCEL: 1,
    ButtonId.MODE: 2,
    ButtonId.SESSION: 3,
    ButtonId.SEND: 4,
    ButtonId.VOICE: 5,
}
_DIRECTION_TO_BYTE = {Direction.CLOCKWISE: 0, Direction.COUNTER_CLOCKWISE: 1}
_ACTION_TO_BYTE = {
    PermissionAction.ALLOW: 0,
    PermissionAction.DENY: 1,
    PermissionAction.ALWAYS: 2,
}
_STATUS_TO_BYTE = {
    SessionStatus.THINKING: 0,
    SessionStatus.TOOL_USE: 1,
    SessionStatus.WRITING: 2,
    SessionStatus.DONE: 3,
    SessionStatus.ERROR: 4,
    SessionStatus.IDLE: 5,
    SessionStatus.PERMISSION_NEEDED: 6,
}
_SOUND_TO_BYTE = {
    SoundType.PERMISSION_ALERT: 0,
    SoundType.SESSION_COMPLETE: 1,
    SoundType.ERROR: 2,
    SoundType.CLICK: 3,
}

def _reverse_lookup[T](mapping: dict[T, int], value: int, label: str) -> T:
    for item, encoded in mapping.items():
        if encoded == value:
            return item
    raise InvalidData(f"invalid {label}: {value}")


def _enum_byte[T](mapping: dict[T, int], value: T, label: str) -> int:
    try:
        return mapping[value]
    except (KeyError, TypeError):
        raise InvalidData(f"invalid {label}: {value!r}") from None


def _checked_int(value: int, maximum: int, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= maximum:
        raise InvalidData(f"{label} out of range: {value!r}")
    return value


def _checked_bool(value: bool, label: str) -> bool:
    if not isinstance(value, bool):
        raise InvalidData(f"{label} is not a boolean: {value!r}")
    return value


def _append_u16(buffer: bytearray, value: int, label: str) -> None:
    buffer.extend(struct.pack("<H", _checked_int(value, 0xFFFF, label)))


def _append_i16(buffer: bytearray, value: int, label: str) -> None:
    if isinstance(value, bool) or not isinstance(value, int) or not -0x8000 <= value <= 0x7FFF:
        raise InvalidData(f"{label} out of range: {value!r}")
    buffer.extend(struct.pack("<h", value))


def _append_u32(buffer: bytearray, value: int, label: str) -> None:
    buffer.extend(struct.pack("<I", _checked_int(value, 0xFFFF_FFFF, label)))


def _append_u64(buffer: bytearray, value: int, label: str) -> None:
    buffer.extend(struct.pack("<Q", _checked_int(value, 0xFFFF_FFFF_FFFF_FFFF, label)))


def _append_string(buffer: bytearray, value: str) -> None:
    if not isinstance(value, str):
        raise InvalidData(f"string field is not str: {value!r}")
    encoded = value.encode("utf-8")
    if len(encoded) > 0xFFFF:
        raise StringTooLong(len(encoded))
    buffer.extend(struct.pack("<H", len(encoded)))
    buffer.extend(encoded)


def _append_color(buffer: bytearray, color: LedColor) -> None:
    if not isinstance(color, LedColor):
        raise InvalidData(f"invalid LED color: {color!r}")
    buffer.extend((color.r, color.g, color.b))


class _Reader:
    __slots__ = ("data", "offset")

    def __init__(self, data: bytes | bytearray | memoryview, offset: int = 0) -> None:
        self.data = memoryview(data).cast("B")
        self.offset = offset

    def take(self, length: int) -> memoryview:
        end = self.offset + length
        if end > len(self.data):
            raise BufferTooShort
        result = self.data[self.offset:end]
        self.offset = end
        return result

    def u8(self) -> int:
        return int(self.take(1)[0])

    def u16(self) -> int:
        return int(struct.unpack("<H", self.take(2))[0])

    def i16(self) -> int:
        return int(struct.unpack("<h", self.take(2))[0])

    def u32(self) -> int:
        return int(struct.unpack("<I", self.take(4))[0])

    def u64(self) -> int:
        return int(struct.unpack("<Q", self.take(8))[0])

    def f64(self) -> float:
        return float(struct.unpack("<d", self.take(8))[0])

    def string(self) -> str:
        raw = self.take(self.u16())
        try:
            return raw.tobytes().decode("utf-8")
        except UnicodeDecodeError:
            raise InvalidUtf8 from None

    def color(self) -> LedColor:
        raw = self.take(3)
        return LedColor(int(raw[0]), int(raw[1]), int(raw[2]))


def _encode_session_info(buffer: bytearray, info: SessionInfo) -> None:
    if not isinstance(info, SessionInfo):
        raise InvalidData(f"invalid session info: {info!r}")
    _append_u16(buffer, info.id, "session id")
    _append_string(buffer, info.name)
    buffer.append(_enum_byte(_STATUS_TO_BYTE, info.status, "session status"))
    buffer.append(int(_checked_bool(info.has_permission_request, "has_permission_request")))
    _append_string(buffer, info.source)
    _append_string(buffer, info.cwd)
    _append_string(buffer, info.permission_mode)
    _append_string(buffer, info.model)
    _append_u64(buffer, info.tokens_in, "tokens_in")
    _append_u64(buffer, info.tokens_out, "tokens_out")
    try:
        buffer.extend(struct.pack("<d", info.cost_usd))
    except (OverflowError, struct.error, TypeError):
        raise InvalidData(f"invalid cost_usd: {info.cost_usd!r}") from None
    buffer.append(_checked_int(info.context_pct, 0xFF, "context_pct"))
    _append_string(buffer, info.last_message)
    _append_string(buffer, info.last_ai_output)
    _append_string(buffer, info.bundle_id)
    _append_string(buffer, info.session_tty)
    _append_u64(buffer, info.started_at, "started_at")
    _append_u64(buffer, info.last_activity, "last_activity")


def _decode_session_info(reader: _Reader) -> SessionInfo:
    session_id = reader.u16()
    name = reader.string()
    status = _reverse_lookup(_STATUS_TO_BYTE, reader.u8(), "session status")
    has_permission_request = reader.u8() != 0
    source = reader.string()
    cwd = reader.string()
    permission_mode = reader.string()
    model = reader.string()
    tokens_in = reader.u64()
    tokens_out = reader.u64()
    cost_usd = reader.f64()
    context_pct = reader.u8()
    last_message = reader.string()
    last_ai_output = reader.string()
    bundle_id = reader.string()
    session_tty = reader.string()
    started_at = reader.u64()
    last_activity = reader.u64()
    return SessionInfo(
        id=session_id,
        name=name,
        status=status,
        has_permission_request=has_permission_request,
        source=source,
        cwd=cwd,
        permission_mode=permission_mode,
        model=model,
        tokens_in=tokens_in,
        tokens_out=tokens_out,
        cost_usd=cost_usd,
        context_pct=context_pct,
        last_message=last_message,
        last_ai_output=last_ai_output,
        bundle_id=bundle_id,
        session_tty=session_tty,
        started_at=started_at,
        last_activity=last_activity,
    )


def _encode_notification_info(buffer: bytearray, info: NotificationInfo) -> None:
    if not isinstance(info, NotificationInfo):
        raise InvalidData(f"invalid notification info: {info!r}")
    _append_u32(buffer, info.id, "notification id")
    _append_u16(buffer, info.session_id, "notification session id")
    _append_string(buffer, info.session_name)
    buffer.append(_enum_byte(_STATUS_TO_BYTE, info.status, "session status"))
    _append_string(buffer, info.description)
    _append_u64(buffer, info.timestamp, "notification timestamp")
    buffer.append(int(_checked_bool(info.read, "notification read")))


def _decode_notification_info(reader: _Reader) -> NotificationInfo:
    return NotificationInfo(
        id=reader.u32(),
        session_id=reader.u16(),
        session_name=reader.string(),
        status=_reverse_lookup(_STATUS_TO_BYTE, reader.u8(), "session status"),
        description=reader.string(),
        timestamp=reader.u64(),
        read=reader.u8() != 0,
    )


def _encode_setup_tool_status(buffer: bytearray, info: SetupToolStatus) -> None:
    if not isinstance(info, SetupToolStatus):
        raise InvalidData(f"invalid setup tool status: {info!r}")
    _append_string(buffer, info.id)
    _append_string(buffer, info.name)
    buffer.append(int(_checked_bool(info.detected, "setup tool detected")))
    buffer.append(int(_checked_bool(info.hook_installed, "setup tool hook_installed")))
    _append_string(buffer, info.detail)


def _decode_setup_tool_status(reader: _Reader) -> SetupToolStatus:
    return SetupToolStatus(
        id=reader.string(),
        name=reader.string(),
        detected=reader.u8() != 0,
        hook_installed=reader.u8() != 0,
        detail=reader.string(),
    )


def _encode_yolo_config(
    buffer: bytearray,
    active: bool,
    notify_auto_allow: bool,
    allow: tuple[str, ...],
    deny: tuple[str, ...],
) -> None:
    buffer.append(int(_checked_bool(active, "YOLO active")))
    buffer.append(int(_checked_bool(notify_auto_allow, "YOLO notify_auto_allow")))
    if len(allow) > 0xFF:
        raise TooManyItems(len(allow))
    buffer.append(len(allow))
    for rule in allow:
        _append_string(buffer, rule)
    if len(deny) > 0xFF:
        raise TooManyItems(len(deny))
    buffer.append(len(deny))
    for rule in deny:
        _append_string(buffer, rule)


def _decode_yolo_config(reader: _Reader) -> tuple[bool, bool, tuple[str, ...], tuple[str, ...]]:
    active = reader.u8() != 0
    notify_auto_allow = reader.u8() != 0
    allow = tuple(reader.string() for _ in range(reader.u8()))
    deny = tuple(reader.string() for _ in range(reader.u8()))
    return active, notify_auto_allow, allow, deny


def encode_uplink(message: UplinkMessage) -> bytes:
    """Encode one keyboard-to-daemon message."""

    buffer = bytearray()
    if isinstance(message, ButtonPress):
        buffer.extend((TAG_BUTTON_PRESS, _enum_byte(_BUTTON_TO_BYTE, message.button, "button id")))
    elif isinstance(message, ButtonRelease):
        buffer.extend((TAG_BUTTON_RELEASE, _enum_byte(_BUTTON_TO_BYTE, message.button, "button id")))
    elif isinstance(message, KnobRotate):
        buffer.extend(
            (
                TAG_KNOB_ROTATE,
                _enum_byte(_DIRECTION_TO_BYTE, message.direction, "direction"),
                _checked_int(message.steps, 0xFF, "knob steps"),
            )
        )
    elif isinstance(message, KnobPress):
        buffer.append(TAG_KNOB_PRESS)
    elif isinstance(message, KnobRelease):
        buffer.append(TAG_KNOB_RELEASE)
    elif isinstance(message, PermissionResponse):
        buffer.append(TAG_PERMISSION_RESPONSE)
        _append_u16(buffer, message.session_id, "session id")
        buffer.append(_enum_byte(_ACTION_TO_BYTE, message.action, "permission action"))
    elif isinstance(message, SessionSwitch):
        buffer.append(TAG_SESSION_SWITCH)
        _append_u16(buffer, message.session_id, "session id")
    elif isinstance(message, SetupActionRequest):
        buffer.append(TAG_SETUP_ACTION_REQUEST)
        _append_u32(buffer, message.request_id, "request id")
        buffer.append(_checked_int(message.action_id, 0xFF, "action id"))
        _append_string(buffer, message.tool)
        _append_string(buffer, message.command)
        _append_u16(buffer, message.daemon_port, "daemon port")
    elif isinstance(message, SetupStatusRequest):
        buffer.append(TAG_SETUP_STATUS_REQUEST)
        _append_u32(buffer, message.request_id, "request id")
        _append_u16(buffer, message.daemon_port, "daemon port")
    elif isinstance(message, TimeSyncRequest):
        buffer.append(TAG_TIME_SYNC_REQUEST)
    elif isinstance(message, YoloConfigRequest):
        buffer.append(TAG_YOLO_CONFIG_REQUEST)
    elif isinstance(message, YoloConfigSet):
        buffer.append(TAG_YOLO_CONFIG_SET)
        _encode_yolo_config(
            buffer,
            message.active,
            message.notify_auto_allow,
            message.allow,
            message.deny,
        )
    else:
        raise TypeError(f"unsupported uplink message: {type(message).__name__}")
    return bytes(buffer)


def decode_uplink(data: bytes | bytearray | memoryview) -> UplinkMessage:
    """Decode one keyboard-to-daemon message.

    As in Rust, trailing bytes are ignored after a complete message.
    """

    reader = _Reader(data)
    tag = reader.u8()
    if tag == TAG_BUTTON_PRESS:
        return ButtonPress(_reverse_lookup(_BUTTON_TO_BYTE, reader.u8(), "button id"))
    if tag == TAG_BUTTON_RELEASE:
        return ButtonRelease(_reverse_lookup(_BUTTON_TO_BYTE, reader.u8(), "button id"))
    if tag == TAG_KNOB_ROTATE:
        return KnobRotate(
            _reverse_lookup(_DIRECTION_TO_BYTE, reader.u8(), "direction"), reader.u8()
        )
    if tag == TAG_KNOB_PRESS:
        return KnobPress()
    if tag == TAG_KNOB_RELEASE:
        return KnobRelease()
    if tag == TAG_PERMISSION_RESPONSE:
        return PermissionResponse(
            reader.u16(), _reverse_lookup(_ACTION_TO_BYTE, reader.u8(), "permission action")
        )
    if tag == TAG_SESSION_SWITCH:
        return SessionSwitch(reader.u16())
    if tag == TAG_SETUP_ACTION_REQUEST:
        return SetupActionRequest(
            request_id=reader.u32(),
            action_id=reader.u8(),
            tool=reader.string(),
            command=reader.string(),
            daemon_port=reader.u16(),
        )
    if tag == TAG_SETUP_STATUS_REQUEST:
        return SetupStatusRequest(request_id=reader.u32(), daemon_port=reader.u16())
    if tag == TAG_TIME_SYNC_REQUEST:
        return TimeSyncRequest()
    if tag == TAG_YOLO_CONFIG_REQUEST:
        return YoloConfigRequest()
    if tag == TAG_YOLO_CONFIG_SET:
        return YoloConfigSet(*_decode_yolo_config(reader))
    raise InvalidTag(tag)


def encode_downlink(message: DownlinkMessage) -> bytes:
    """Encode one daemon-to-keyboard message."""

    buffer = bytearray()
    if isinstance(message, SessionListUpdate):
        buffer.append(TAG_SESSION_LIST_UPDATE)
        if len(message.sessions) > 0xFF:
            raise TooManyItems(len(message.sessions))
        buffer.append(len(message.sessions))
        for session in message.sessions:
            _encode_session_info(buffer, session)
        buffer.append(_checked_int(message.active_index, 0xFF, "active index"))
    elif isinstance(message, SessionListClear):
        buffer.append(TAG_SESSION_LIST_CLEAR)
        _append_u16(buffer, message.active_session_id, "active session id")
    elif isinstance(message, SessionUpsert):
        buffer.append(TAG_SESSION_UPSERT)
        _encode_session_info(buffer, message.session)
        buffer.append(int(_checked_bool(message.active, "active")))
    elif isinstance(message, SessionRemove):
        buffer.append(TAG_SESSION_REMOVE)
        _append_u16(buffer, message.session_id, "session id")
    elif isinstance(message, SessionStatusChange):
        buffer.append(TAG_SESSION_STATUS_CHANGE)
        _append_u16(buffer, message.session_id, "session id")
        buffer.append(_enum_byte(_STATUS_TO_BYTE, message.status, "session status"))
    elif isinstance(message, PermissionRequest):
        buffer.append(TAG_PERMISSION_REQUEST)
        _append_u16(buffer, message.session_id, "session id")
        _append_string(buffer, message.action_desc)
    elif isinstance(message, SetLed):
        buffer.extend((TAG_SET_LED, _enum_byte(_BUTTON_TO_BYTE, message.button, "button id")))
        _append_color(buffer, message.color)
        buffer.append(int(_checked_bool(message.blink, "blink")))
    elif isinstance(message, SetKnobRing):
        buffer.append(TAG_SET_KNOB_RING)
        _append_color(buffer, message.color)
    elif isinstance(message, PlaySound):
        buffer.extend((TAG_PLAY_SOUND, _enum_byte(_SOUND_TO_BYTE, message.sound, "sound type")))
    elif isinstance(message, DismissPermission):
        buffer.append(TAG_DISMISS_PERMISSION)
        _append_u16(buffer, message.session_id, "session id")
    elif isinstance(message, NotificationListUpdate):
        buffer.append(TAG_NOTIFICATION_LIST_UPDATE)
        if len(message.notifications) > 0xFF:
            raise TooManyItems(len(message.notifications))
        buffer.append(len(message.notifications))
        for notification in message.notifications:
            _encode_notification_info(buffer, notification)
    elif isinstance(message, FrameData):
        buffer.append(TAG_FRAME_DATA)
        _append_u16(buffer, message.width, "frame width")
        _append_u16(buffer, message.height, "frame height")
        _append_u32(buffer, len(message.pixels), "pixel byte length")
        buffer.extend(message.pixels)
    elif isinstance(message, SetVolume):
        buffer.extend((TAG_SET_VOLUME, _checked_int(message.volume, 0xFF, "volume")))
    elif isinstance(message, SetMuted):
        buffer.extend((TAG_SET_MUTED, int(_checked_bool(message.muted, "muted"))))
    elif isinstance(message, SetSoundMapping):
        buffer.extend(
            (TAG_SET_SOUND_MAPPING, _enum_byte(_SOUND_TO_BYTE, message.sound_type, "sound type"))
        )
        _append_string(buffer, message.sound_id)
    elif isinstance(message, SetupActionResult):
        buffer.append(TAG_SETUP_ACTION_RESULT)
        _append_u32(buffer, message.request_id, "request id")
        buffer.append(int(_checked_bool(message.success, "success")))
    elif isinstance(message, SetupStatusUpdate):
        buffer.append(TAG_SETUP_STATUS_UPDATE)
        _append_u32(buffer, message.request_id, "request id")
        if len(message.tools) > 0xFF:
            raise TooManyItems(len(message.tools))
        buffer.append(len(message.tools))
        for tool in message.tools:
            _encode_setup_tool_status(buffer, tool)
    elif isinstance(message, TimeSync):
        buffer.append(TAG_TIME_SYNC)
        _append_u64(buffer, message.unix_time_ms, "Unix time milliseconds")
        _append_i16(buffer, message.utc_offset_minutes, "UTC offset minutes")
    elif isinstance(message, YoloConfigUpdate):
        buffer.append(TAG_YOLO_CONFIG_UPDATE)
        _encode_yolo_config(
            buffer,
            message.active,
            message.notify_auto_allow,
            message.allow,
            message.deny,
        )
    else:
        raise TypeError(f"unsupported downlink message: {type(message).__name__}")
    return bytes(buffer)


def decode_downlink(data: bytes | bytearray | memoryview) -> DownlinkMessage:
    """Decode one daemon-to-keyboard message, accepting trailing bytes like Rust."""

    reader = _Reader(data)
    tag = reader.u8()
    if tag == TAG_SESSION_LIST_UPDATE:
        sessions = tuple(_decode_session_info(reader) for _ in range(reader.u8()))
        return SessionListUpdate(sessions, reader.u8())
    if tag == TAG_SESSION_LIST_CLEAR:
        return SessionListClear(reader.u16())
    if tag == TAG_SESSION_UPSERT:
        return SessionUpsert(_decode_session_info(reader), bool(reader.u8()))
    if tag == TAG_SESSION_REMOVE:
        return SessionRemove(reader.u16())
    if tag == TAG_SESSION_STATUS_CHANGE:
        return SessionStatusChange(
            reader.u16(), _reverse_lookup(_STATUS_TO_BYTE, reader.u8(), "session status")
        )
    if tag == TAG_PERMISSION_REQUEST:
        return PermissionRequest(reader.u16(), reader.string())
    if tag == TAG_SET_LED:
        return SetLed(
            _reverse_lookup(_BUTTON_TO_BYTE, reader.u8(), "button id"),
            reader.color(),
            reader.u8() != 0,
        )
    if tag == TAG_SET_KNOB_RING:
        return SetKnobRing(reader.color())
    if tag == TAG_PLAY_SOUND:
        return PlaySound(_reverse_lookup(_SOUND_TO_BYTE, reader.u8(), "sound type"))
    if tag == TAG_DISMISS_PERMISSION:
        return DismissPermission(reader.u16())
    if tag == TAG_FRAME_DATA:
        width = reader.u16()
        height = reader.u16()
        pixels = reader.take(reader.u32()).tobytes()
        return FrameData(width, height, pixels)
    if tag == TAG_NOTIFICATION_LIST_UPDATE:
        return NotificationListUpdate(
            tuple(_decode_notification_info(reader) for _ in range(reader.u8()))
        )
    if tag == TAG_SET_VOLUME:
        return SetVolume(reader.u8())
    if tag == TAG_SET_MUTED:
        return SetMuted(reader.u8() != 0)
    if tag == TAG_SET_SOUND_MAPPING:
        sound_type = _reverse_lookup(_SOUND_TO_BYTE, reader.u8(), "sound type")
        return SetSoundMapping(sound_type, reader.string())
    if tag == TAG_SETUP_ACTION_RESULT:
        return SetupActionResult(reader.u32(), reader.u8() != 0)
    if tag == TAG_SETUP_STATUS_UPDATE:
        return SetupStatusUpdate(
            reader.u32(), tuple(_decode_setup_tool_status(reader) for _ in range(reader.u8()))
        )
    if tag == TAG_TIME_SYNC:
        return TimeSync(reader.u64(), reader.i16())
    if tag == TAG_YOLO_CONFIG_UPDATE:
        return YoloConfigUpdate(*_decode_yolo_config(reader))
    raise InvalidTag(tag)


__all__ = [
    "BufferTooShort",
    "CodecError",
    "InvalidData",
    "InvalidTag",
    "InvalidUtf8",
    "StringTooLong",
    "TooManyItems",
    "decode_downlink",
    "decode_uplink",
    "encode_downlink",
    "encode_uplink",
]

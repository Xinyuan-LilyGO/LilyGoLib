from __future__ import annotations

import struct
import unittest

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
from vibe_keyboard.protocol import (
    BufferTooShort,
    ButtonPress,
    ButtonRelease,
    DismissPermission,
    FrameData,
    InvalidData,
    InvalidTag,
    InvalidUtf8,
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
    StringTooLong,
    TimeSync,
    TimeSyncRequest,
    TooManyItems,
    YoloConfigRequest,
    YoloConfigSet,
    YoloConfigUpdate,
    decode_downlink,
    decode_uplink,
    encode_downlink,
    encode_uplink,
)


def rich_session() -> SessionInfo:
    return SessionInfo(
        id=0x1234,
        name="Agent",
        status=SessionStatus.TOOL_USE,
        has_permission_request=True,
        source="claude-code",
        cwd="/tmp/proj",
        permission_mode="default",
        model="claude-opus-4-6",
        tokens_in=123,
        tokens_out=456,
        cost_usd=1.25,
        context_pct=73,
        last_message="hello",
        last_ai_output="working",
        bundle_id="com.googlecode.iterm2",
        session_tty="/dev/ttys001",
        started_at=1000,
        last_activity=2000,
    )


class UplinkCodecTests(unittest.TestCase):
    def test_all_uplink_variants_roundtrip_and_tags(self) -> None:
        cases = [
            (ButtonPress(ButtonId.DELETE), 0x01),
            (ButtonRelease(ButtonId.VOICE), 0x02),
            (KnobRotate(Direction.COUNTER_CLOCKWISE, 7), 0x03),
            (KnobPress(), 0x04),
            (KnobRelease(), 0x05),
            (PermissionResponse(0x1234, PermissionAction.ALWAYS), 0x06),
            (SessionSwitch(0x4567), 0x07),
            (SetupActionRequest(0xAABBCCDD, 3, "codex", "install_hook", 19280), 0x08),
            (SetupStatusRequest(0xAABBCCDD, 19280), 0x09),
            (TimeSyncRequest(), 0x0A),
            (YoloConfigRequest(), 0x0B),
            (
                YoloConfigSet(True, False, ["Read(*)", "Glob(*)"], ["Bash(sudo*)"]),
                0x0C,
            ),
        ]
        for message, tag in cases:
            with self.subTest(message=message):
                encoded = encode_uplink(message)
                self.assertEqual(encoded[0], tag)
                self.assertEqual(decode_uplink(encoded), message)

    def test_uplink_golden_vectors(self) -> None:
        self.assertEqual(encode_uplink(ButtonPress(ButtonId.SEND)), b"\x01\x04")
        self.assertEqual(encode_uplink(ButtonRelease(ButtonId.CANCEL)), b"\x02\x01")
        self.assertEqual(
            encode_uplink(KnobRotate(Direction.CLOCKWISE, 2)), b"\x03\x00\x02"
        )
        self.assertEqual(
            encode_uplink(PermissionResponse(0x1234, PermissionAction.DENY)),
            b"\x06\x34\x12\x01",
        )
        self.assertEqual(encode_uplink(SessionSwitch(0x1234)), b"\x07\x34\x12")

    def test_setup_action_request_golden_vector(self) -> None:
        message = SetupActionRequest(0x12345678, 4, "x", "go", 19280)
        expected = (
            b"\x08\x78\x56\x34\x12\x04"
            b"\x01\x00x"
            b"\x02\x00go"
            b"\x50\x4b"
        )
        self.assertEqual(encode_uplink(message), expected)

    def test_setup_status_request_golden_vector(self) -> None:
        self.assertEqual(
            encode_uplink(SetupStatusRequest(0x12345678, 19280)),
            b"\x09\x78\x56\x34\x12\x50\x4b",
        )

    def test_time_sync_request_golden_vector(self) -> None:
        self.assertEqual(encode_uplink(TimeSyncRequest()), b"\x0a")

    def test_yolo_config_set_golden_vector(self) -> None:
        self.assertEqual(
            encode_uplink(YoloConfigSet(True, False, ["Read(*)"], ["Bash(sudo*)"])),
            b"\x0c\x01\x00\x01\x07\x00Read(*)\x01\x0b\x00Bash(sudo*)",
        )

    def test_uplink_errors_and_trailing_bytes(self) -> None:
        with self.assertRaises(BufferTooShort):
            decode_uplink(b"")
        with self.assertRaises(InvalidTag) as caught:
            decode_uplink(b"\xff")
        self.assertEqual(caught.exception.tag, 0xFF)
        with self.assertRaises(BufferTooShort):
            decode_uplink(b"\x03\x00")
        with self.assertRaises(InvalidData):
            decode_uplink(b"\x01\xff")
        with self.assertRaises(InvalidData):
            decode_uplink(b"\x03\xff\x01")
        self.assertEqual(decode_uplink(b"\x04trailing"), KnobPress())

    def test_uplink_numeric_and_string_boundaries(self) -> None:
        with self.assertRaises(InvalidData):
            encode_uplink(SessionSwitch(0x1_0000))
        with self.assertRaises(InvalidData):
            encode_uplink(KnobRotate(Direction.CLOCKWISE, 256))
        with self.assertRaises(StringTooLong) as caught:
            encode_uplink(SetupActionRequest(1, 1, "界" * 22_000, "go", 1))
        self.assertGreater(caught.exception.length, 0xFFFF)


class DownlinkCodecTests(unittest.TestCase):
    def test_all_downlink_variants_roundtrip_and_tags(self) -> None:
        notification = NotificationInfo(
            id=7,
            session_id=2,
            session_name="Agent",
            status=SessionStatus.ERROR,
            description="build failed",
            timestamp=99,
            read=False,
        )
        cases = [
            (SessionListUpdate([rich_session()], 0), 0x81),
            (SessionStatusChange(1, SessionStatus.WRITING), 0x82),
            (PermissionRequest(1, "Write(main.py)"), 0x83),
            (SetLed(ButtonId.SESSION, LedColor.RED, True), 0x84),
            (SetKnobRing(LedColor.AMBER), 0x85),
            (PlaySound(SoundType.CLICK), 0x86),
            (DismissPermission(1), 0x87),
            (FrameData(2, 1, b"\x00\x00\xff\xff"), 0x88),
            (NotificationListUpdate([notification]), 0x89),
            (SetVolume(80), 0x8A),
            (SetMuted(True), 0x8B),
            (SetSoundMapping(SoundType.ERROR, "custom:oops"), 0x8C),
            (SetupActionResult(0x12345678, True), 0x8D),
            (SessionListClear(0x1234), 0x8E),
            (SessionUpsert(rich_session(), True), 0x8F),
            (SessionRemove(0x1234), 0x90),
            (
                SetupStatusUpdate(
                    0x12345678,
                    [SetupToolStatus("codex", "Codex", True, False, "notify events only")],
                ),
                0x91,
            ),
            (TimeSync(1_700_000_000_123, -420), 0x92),
            (
                YoloConfigUpdate(True, True, ["Read(*)", "Glob(*)"], ["Bash(sudo*)"]),
                0x93,
            ),
        ]
        for message, tag in cases:
            with self.subTest(message=message):
                encoded = encode_downlink(message)
                self.assertEqual(encoded[0], tag)
                self.assertEqual(decode_downlink(encoded), message)

    def test_downlink_golden_vectors(self) -> None:
        self.assertEqual(
            encode_downlink(SessionStatusChange(0x1234, SessionStatus.ERROR)),
            b"\x82\x34\x12\x04",
        )
        self.assertEqual(
            encode_downlink(SetLed(ButtonId.MODE, LedColor(1, 2, 3), True)),
            b"\x84\x02\x01\x02\x03\x01",
        )
        self.assertEqual(encode_downlink(PlaySound(SoundType.SESSION_COMPLETE)), b"\x86\x01")
        self.assertEqual(encode_downlink(SetVolume(100)), b"\x8ad")
        self.assertEqual(
            encode_downlink(YoloConfigUpdate(False, True, [], [])),
            b"\x93\x00\x01\x00\x00",
        )
        self.assertEqual(encode_downlink(SetMuted(False)), b"\x8b\x00")
        self.assertEqual(
            encode_downlink(SetupActionResult(0x12345678, True)),
            b"\x8d\x78\x56\x34\x12\x01",
        )
        self.assertEqual(encode_downlink(SessionListClear(0x1234)), b"\x8e\x34\x12")
        self.assertEqual(encode_downlink(SessionRemove(0x1234)), b"\x90\x34\x12")
        self.assertEqual(
            encode_downlink(
                SetupStatusUpdate(
                    0x12345678,
                    [SetupToolStatus("codex", "Codex", True, False, "notify events only")],
                )
            ),
            (
                b"\x91\x78\x56\x34\x12\x01"
                b"\x05\x00codex"
                b"\x05\x00Codex"
                b"\x01\x00"
                b"\x12\x00notify events only"
            ),
        )
        self.assertEqual(
            encode_downlink(TimeSync(1_700_000_000_123, 480)),
            b"\x92\x7b\x68\xe5\xcf\x8b\x01\x00\x00\xe0\x01",
        )

    def test_session_info_binary_field_order(self) -> None:
        info = rich_session()
        encoded = encode_downlink(SessionListUpdate([info], 9))
        expected = bytearray((0x81, 1))

        def text(value: str) -> None:
            raw = value.encode()
            expected.extend(struct.pack("<H", len(raw)))
            expected.extend(raw)

        expected.extend(struct.pack("<H", info.id))
        text(info.name)
        expected.extend((1, 1))
        text(info.source)
        text(info.cwd)
        text(info.permission_mode)
        text(info.model)
        expected.extend(struct.pack("<Q", info.tokens_in))
        expected.extend(struct.pack("<Q", info.tokens_out))
        expected.extend(struct.pack("<d", info.cost_usd))
        expected.append(info.context_pct)
        text(info.last_message)
        text(info.last_ai_output)
        text(info.bundle_id)
        text(info.session_tty)
        expected.extend(struct.pack("<Q", info.started_at))
        expected.extend(struct.pack("<Q", info.last_activity))
        expected.append(9)
        self.assertEqual(encoded, bytes(expected))

    def test_session_upsert_reuses_session_info_binary_field_order(self) -> None:
        info = rich_session()
        encoded = encode_downlink(SessionUpsert(info, True))
        expected = bytearray(encode_downlink(SessionListUpdate([info], 0)))
        expected[0] = 0x8F
        del expected[1]
        expected[-1] = 1
        self.assertEqual(encoded, bytes(expected))

    def test_unicode_string_uses_utf8_byte_length(self) -> None:
        encoded = encode_downlink(PermissionRequest(1, "批准"))
        self.assertEqual(encoded, b"\x83\x01\x00\x06\x00\xe6\x89\xb9\xe5\x87\x86")
        self.assertEqual(decode_downlink(encoded), PermissionRequest(1, "批准"))

    def test_frame_data_length_and_bytes(self) -> None:
        message = FrameData(2, 1, b"\x00\x01\x02\x03")
        self.assertEqual(
            encode_downlink(message),
            b"\x88\x02\x00\x01\x00\x04\x00\x00\x00\x00\x01\x02\x03",
        )
        with self.assertRaises(BufferTooShort):
            decode_downlink(b"\x88\x02\x00\x01\x00\x04\x00\x00\x00\x00")

    def test_downlink_decode_errors(self) -> None:
        with self.assertRaises(BufferTooShort):
            decode_downlink(b"")
        with self.assertRaises(InvalidTag):
            decode_downlink(b"\x80")
        with self.assertRaises(BufferTooShort):
            decode_downlink(b"\x82\x01\x00")
        with self.assertRaises(BufferTooShort):
            decode_downlink(b"\x8e\x01")
        with self.assertRaises(BufferTooShort):
            decode_downlink(b"\x90\x01")
        with self.assertRaises(BufferTooShort):
            decode_downlink(b"\x92\x00\x00\x00\x00\x00\x00\x00\x00\x00")
        with self.assertRaises(BufferTooShort):
            decode_downlink(b"\x93\x01\x01\x01")
        with self.assertRaises(InvalidData):
            decode_downlink(b"\x82\x01\x00\xff")
        with self.assertRaises(InvalidUtf8):
            decode_downlink(b"\x83\x01\x00\x01\x00\xff")

    def test_nonzero_boolean_bytes_decode_true(self) -> None:
        self.assertEqual(decode_downlink(b"\x8b\x7f"), SetMuted(True))
        self.assertEqual(
            decode_downlink(b"\x8d\x01\x00\x00\x00\x02"), SetupActionResult(1, True)
        )
        self.assertEqual(
            decode_downlink(b"\x91\x01\x00\x00\x00\x01\x01\x00x\x01\x00X\x02\x03\x00\x00"),
            SetupStatusUpdate(1, [SetupToolStatus("x", "X", True, True)]),
        )

    def test_item_and_string_limits(self) -> None:
        with self.assertRaises(TooManyItems):
            encode_downlink(SessionListUpdate([SessionInfo()] * 256, 0))
        with self.assertRaises(TooManyItems):
            encode_downlink(NotificationListUpdate([NotificationInfo()] * 256))
        with self.assertRaises(TooManyItems):
            encode_downlink(SetupStatusUpdate(1, [SetupToolStatus("x", "X", True, True)] * 256))
        with self.assertRaises(TooManyItems):
            encode_downlink(YoloConfigUpdate(True, True, ["Read(*)"] * 256, []))
        with self.assertRaises(StringTooLong):
            encode_downlink(PermissionRequest(1, "x" * 65_536))

    def test_numeric_and_boolean_encode_validation(self) -> None:
        with self.assertRaises(InvalidData):
            encode_downlink(SetVolume(256))
        with self.assertRaises(InvalidData):
            encode_downlink(SetMuted(1))  # type: ignore[arg-type]
        with self.assertRaises(InvalidData):
            encode_downlink(SessionListUpdate([], -1))
        with self.assertRaises(InvalidData):
            encode_downlink(SessionListUpdate([SessionInfo(id=-1)], 0))
        with self.assertRaises(InvalidData):
            encode_downlink(SessionListClear(0x1_0000))
        with self.assertRaises(InvalidData):
            encode_downlink(SessionRemove(-1))
        with self.assertRaises(InvalidData):
            encode_downlink(SessionUpsert(SessionInfo(), 1))  # type: ignore[arg-type]
        with self.assertRaises(InvalidData):
            encode_downlink(TimeSync(-1, 0))
        with self.assertRaises(InvalidData):
            encode_downlink(TimeSync(0, 0x8000))
        with self.assertRaises(InvalidData):
            encode_downlink(TimeSync(0, True))  # type: ignore[arg-type]
        with self.assertRaises(InvalidData):
            encode_uplink(YoloConfigSet(1, True, [], []))  # type: ignore[arg-type]

    def test_trailing_bytes_are_accepted_for_rust_compatibility(self) -> None:
        self.assertEqual(decode_downlink(b"\x8a\x50extra"), SetVolume(80))


if __name__ == "__main__":
    unittest.main()

from __future__ import annotations

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


class CoreTypeTests(unittest.TestCase):
    def test_enum_json_values_are_snake_case(self) -> None:
        self.assertEqual(ButtonId.SEND.value, "send")
        self.assertEqual(Direction.COUNTER_CLOCKWISE.value, "counter_clockwise")
        self.assertEqual(PermissionAction.ALWAYS.value, "always")
        self.assertEqual(SoundType.PERMISSION_ALERT.value, "permission_alert")

    def test_session_status_priority(self) -> None:
        self.assertEqual(SessionStatus.PERMISSION_NEEDED.priority(), 0)
        self.assertEqual(SessionStatus.ERROR.priority(), 1)
        for status in (
            SessionStatus.THINKING,
            SessionStatus.TOOL_USE,
            SessionStatus.WRITING,
        ):
            self.assertEqual(status.priority(), 2)
        self.assertEqual(SessionStatus.DONE.priority(), 3)
        self.assertEqual(SessionStatus.IDLE.priority(), 4)

    def test_led_color_constants_match_hardware_palette(self) -> None:
        self.assertEqual(LedColor.OFF, LedColor(0, 0, 0))
        self.assertEqual(LedColor.GREEN, LedColor(0, 200, 0))
        self.assertEqual(LedColor.AMBER, LedColor(245, 158, 11))
        self.assertEqual(LedColor.RED, LedColor(239, 68, 68))
        self.assertEqual(LedColor.ORANGE, LedColor(255, 140, 0))

    def test_led_color_rejects_out_of_range_channels(self) -> None:
        with self.assertRaises(ValueError):
            LedColor(-1, 0, 0)
        with self.assertRaises(ValueError):
            LedColor(0, 256, 0)
        with self.assertRaises(ValueError):
            LedColor(0, 0, True)

    def test_session_info_defaults_are_complete_and_mutable(self) -> None:
        session = SessionInfo()
        self.assertEqual(session.id, 0)
        self.assertEqual(session.status, SessionStatus.IDLE)
        self.assertFalse(session.has_permission_request)
        self.assertEqual(session.tokens_in, 0)
        self.assertEqual(session.cost_usd, 0.0)
        session.name = "Agent"
        session.tokens_in = 12
        self.assertEqual(session.name, "Agent")
        self.assertEqual(session.tokens_in, 12)

    def test_session_info_copy_is_independent(self) -> None:
        original = SessionInfo(id=7, name="Agent", status=SessionStatus.THINKING)
        copied = original.copy(name="Worker")
        self.assertEqual(original.name, "Agent")
        self.assertEqual(copied.name, "Worker")
        self.assertEqual(copied.id, 7)
        self.assertIsNot(original, copied)

    def test_session_info_json_dict_roundtrip_and_http_alias(self) -> None:
        session = SessionInfo(
            id=4,
            name="Agent",
            status=SessionStatus.PERMISSION_NEEDED,
            has_permission_request=True,
            cwd="/tmp/project",
            tokens_out=99,
        )
        payload = session.to_json_dict()
        self.assertEqual(payload["status"], "permission_needed")
        self.assertTrue(payload["has_permission_request"])
        self.assertEqual(SessionInfo.from_json_dict(payload), session)

        aliased = SessionInfo.from_dict(
            {"id": 4, "status": "thinking", "has_permission": True}
        )
        self.assertTrue(aliased.has_permission_request)
        aliased.has_permission = False
        self.assertFalse(aliased.has_permission_request)

    def test_notification_info_json_roundtrip(self) -> None:
        notification = NotificationInfo(
            id=9,
            session_id=2,
            session_name="Build",
            status=SessionStatus.ERROR,
            description="failed",
            timestamp=123,
            read=True,
        )
        self.assertEqual(NotificationInfo.from_dict(notification.to_dict()), notification)


if __name__ == "__main__":
    unittest.main()

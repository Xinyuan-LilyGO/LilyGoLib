from __future__ import annotations

import unittest

from vibe_keyboard.core import SessionInfo, SessionStatus
from vibe_keyboard.daemon.session import (
    DaemonSession,
    HookEvent,
    SessionEventKind,
    SessionStore,
    parse_hook_event,
)


class SessionStoreTests(unittest.TestCase):
    def test_store_update_list_remove(self) -> None:
        store = SessionStore()
        store.update(DaemonSession(SessionInfo(id=3, name="C")))
        store.update(DaemonSession(SessionInfo(id=1, name="A")))
        self.assertEqual([item.id for item in store.list()], [1, 3])
        self.assertEqual(store.get(1).name, "A")
        self.assertEqual(store.remove(1).id, 1)
        self.assertEqual(len(store), 1)

    def test_allocate_skips_existing_ids(self) -> None:
        store = SessionStore()
        store.update(DaemonSession(SessionInfo(id=1)))
        self.assertEqual(store.allocate_id(), 2)

    def test_protocol_copy_is_independent(self) -> None:
        original = DaemonSession(SessionInfo(id=1, name="A"))
        copied = original.to_protocol()
        copied.name = "B"
        self.assertEqual(original.name, "A")

    def test_first_with_permission(self) -> None:
        store = SessionStore()
        store.update(DaemonSession(SessionInfo(id=1, has_permission_request=False)))
        store.update(DaemonSession(SessionInfo(id=2, has_permission_request=True)))
        self.assertEqual(store.first_with_permission().id, 2)


class HookEventTests(unittest.TestCase):
    def test_roundtrip_dict_uses_type_key(self) -> None:
        hook = HookEvent.from_dict({"type": "SessionStart", "session_id": "abc"})
        self.assertEqual(hook.event_type, "SessionStart")
        self.assertEqual(hook.to_dict()["type"], "SessionStart")

    def test_started_uses_cwd_name(self) -> None:
        event = parse_hook_event(HookEvent(event_type="init", session_id="abc", cwd="/tmp/project"))
        self.assertEqual(event.kind, SessionEventKind.STARTED)
        self.assertEqual(event.name, "project")

    def test_status_aliases(self) -> None:
        cases = {
            "PreToolUse": SessionStatus.TOOL_USE,
            "PostToolUse": SessionStatus.WRITING,
            "UserPromptSubmit": SessionStatus.THINKING,
            "Stop": SessionStatus.DONE,
        }
        for name, expected in cases.items():
            with self.subTest(name=name):
                event = parse_hook_event(HookEvent(event_type=name, session_id="abc"))
                self.assertEqual(event.status, expected)

    def test_notification_permission_and_status(self) -> None:
        permission = parse_hook_event(
            HookEvent(event_type="Notification", session_id="a", tool_input="file.py")
        )
        thinking = parse_hook_event(HookEvent(event_type="Notification", session_id="a"))
        self.assertEqual(permission.kind, SessionEventKind.PERMISSION_REQUEST)
        self.assertEqual(thinking.status, SessionStatus.THINKING)

    def test_unknown_or_invalid_status_returns_none(self) -> None:
        self.assertIsNone(parse_hook_event(HookEvent(event_type="unknown")))
        self.assertIsNone(parse_hook_event(HookEvent(event_type="status", status="invalid")))


if __name__ == "__main__":
    unittest.main()


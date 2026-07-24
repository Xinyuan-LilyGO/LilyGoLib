from __future__ import annotations

import unittest

from vibe_keyboard.core import PermissionAction, SessionStatus
from vibe_keyboard.daemon.notifications import NotificationQueue, NullNotificationBackend
from vibe_keyboard.daemon.permissions import (
    PendingPermission,
    PermissionQueue,
    YoloConfig,
    YoloDecision,
    evaluate_yolo,
)


class PermissionTests(unittest.TestCase):
    def test_yolo_inactive_asks(self) -> None:
        self.assertEqual(evaluate_yolo(YoloConfig(), "Read", "a.py"), YoloDecision.ASK_USER)

    def test_deny_precedes_allow(self) -> None:
        config = YoloConfig(active=True, allow=["Bash(*)"], deny=["Bash(git push*)"])
        self.assertEqual(evaluate_yolo(config, "Bash", "git push origin main"), YoloDecision.AUTO_DENY)
        self.assertEqual(evaluate_yolo(config, "Bash", "git status"), YoloDecision.AUTO_ALLOW)

    def test_glob_matches_path(self) -> None:
        config = YoloConfig(active=True, allow=["Read(src/*)"], deny=[])
        self.assertEqual(evaluate_yolo(config, "Read", "src/a/b.py"), YoloDecision.AUTO_ALLOW)

    def test_queue_resolve_always(self) -> None:
        queue = PermissionQueue()
        queue.push(PendingPermission(1, "Write", "src/a.py"))
        resolved = queue.resolve(1, PermissionAction.ALWAYS)
        self.assertEqual(resolved.tool_name, "Write")
        self.assertTrue(queue.is_always_allowed("Write", "src/a.py"))

    def test_queue_limit(self) -> None:
        queue = PermissionQueue()
        for index in range(queue.MAX_PENDING):
            self.assertTrue(queue.push(PendingPermission(index, "Read", "x")))
        self.assertFalse(queue.push(PendingPermission(999, "Read", "x")))


class NotificationTests(unittest.TestCase):
    def test_priority_and_history(self) -> None:
        queue = NotificationQueue()
        done = queue.push(1, "one", SessionStatus.DONE, "done")
        queue.push(2, "two", SessionStatus.ERROR, "error")
        queue.push(3, "three", SessionStatus.PERMISSION_NEEDED, "permission")
        self.assertEqual(
            [item.status for item in queue.unread()],
            [SessionStatus.PERMISSION_NEEDED, SessionStatus.ERROR, SessionStatus.DONE],
        )
        queue.mark_read(done)
        self.assertEqual(queue.history()[0].id, done)

    def test_remove_by_session(self) -> None:
        queue = NotificationQueue()
        queue.push(1, "one", SessionStatus.DONE, "a")
        queue.push(2, "two", SessionStatus.DONE, "b")
        queue.remove_by_session(1)
        self.assertEqual([item.session_id for item in queue.all()], [2])

    def test_remove_permission_by_session_preserves_other_notifications(self) -> None:
        queue = NotificationQueue()
        queue.push(1, "one", SessionStatus.PERMISSION_NEEDED, "permission")
        done = queue.push(1, "one", SessionStatus.DONE, "done")
        other = queue.push(2, "two", SessionStatus.PERMISSION_NEEDED, "other")

        queue.remove_permission_by_session(1)

        self.assertEqual({item.id for item in queue.all()}, {done, other})

    def test_unread_is_capped(self) -> None:
        queue = NotificationQueue()
        for index in range(55):
            queue.push(index, str(index), SessionStatus.DONE, "done")
        self.assertEqual(queue.unread_count, 50)

    def test_null_backend_contract(self) -> None:
        backend = NullNotificationBackend()
        self.assertEqual(backend.name, "null")
        self.assertIsNone(backend.notify("title", "body", None))


if __name__ == "__main__":
    unittest.main()

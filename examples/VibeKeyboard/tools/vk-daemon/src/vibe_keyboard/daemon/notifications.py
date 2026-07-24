"""System notification adapter and priority queue."""

from __future__ import annotations

import subprocess
import sys
import time
from dataclasses import dataclass
from typing import Protocol

from ..core import NotificationInfo, SessionStatus


class NotificationBackend(Protocol):
    @property
    def name(self) -> str: ...

    def notify(self, title: str, body: str, click_bundle_id: str | None = None) -> None: ...


class NullNotificationBackend:
    name = "null"

    def notify(self, title: str, body: str, click_bundle_id: str | None = None) -> None:
        del title, body, click_bundle_id


class MacNotificationBackend:
    name = "mac-native"

    def notify(self, title: str, body: str, click_bundle_id: str | None = None) -> None:
        del click_bundle_id
        script = "display notification " + _apple_quote(body) + " with title " + _apple_quote(title)
        subprocess.run(["osascript", "-e", script], check=True, timeout=5, capture_output=True)


def _apple_quote(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", " ") + '"'


def default_backend() -> NotificationBackend:
    return MacNotificationBackend() if sys.platform == "darwin" else NullNotificationBackend()


@dataclass(slots=True)
class Notification:
    id: int
    session_id: int
    session_name: str
    status: SessionStatus
    description: str
    timestamp: int
    read: bool = False

    def to_protocol(self) -> NotificationInfo:
        return NotificationInfo(
            id=self.id,
            session_id=self.session_id,
            session_name=self.session_name,
            status=self.status,
            description=self.description,
            timestamp=self.timestamp,
            read=self.read,
        )


class NotificationQueue:
    MAX_UNREAD = 50

    def __init__(self, max_history: int = 20) -> None:
        self._items: list[Notification] = []
        self._next_id = 1
        self._max_history = max_history

    def push(
        self,
        session_id: int,
        session_name: str,
        status: SessionStatus,
        description: str,
    ) -> int:
        notification_id = self._next_id
        self._next_id += 1
        self._items.append(
            Notification(
                notification_id,
                session_id,
                session_name,
                status,
                description,
                int(time.time()),
            )
        )
        unread = [item for item in self._items if not item.read]
        for item in sorted(unread, key=lambda value: value.timestamp)[: -self.MAX_UNREAD]:
            item.read = True
        history = sorted(
            (item for item in self._items if item.read), key=lambda value: value.timestamp
        )
        remove_ids = {item.id for item in history[: -self._max_history]}
        self._items = [item for item in self._items if item.id not in remove_ids]
        return notification_id

    def mark_read(self, notification_id: int) -> None:
        for item in self._items:
            if item.id == notification_id:
                item.read = True
                break

    def remove_by_session(self, session_id: int) -> None:
        self._items = [item for item in self._items if item.session_id != session_id]

    def remove_permission_by_session(self, session_id: int) -> None:
        self._items = [
            item
            for item in self._items
            if not (
                item.session_id == session_id
                and item.status is SessionStatus.PERMISSION_NEEDED
            )
        ]

    def remove(self, notification_id: int) -> None:
        self._items = [item for item in self._items if item.id != notification_id]

    def unread(self) -> list[Notification]:
        return sorted(
            (item for item in self._items if not item.read),
            key=lambda item: (item.status.priority(), item.id),
        )

    def history(self) -> list[Notification]:
        return sorted(
            (item for item in self._items if item.read),
            key=lambda item: (item.timestamp, item.id),
            reverse=True,
        )

    def all(self) -> list[Notification]:
        return [*self.unread(), *self.history()]

    @property
    def unread_count(self) -> int:
        return sum(not item.read for item in self._items)

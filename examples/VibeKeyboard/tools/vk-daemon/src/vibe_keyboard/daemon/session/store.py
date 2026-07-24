"""In-memory daemon session aggregation."""

from __future__ import annotations

import builtins
from dataclasses import dataclass, field

from ...core import SessionInfo


@dataclass(slots=True)
class WindowInfo:
    app_name: str
    window_title: str
    pid: int | None = None


@dataclass(slots=True)
class DaemonSession:
    info: SessionInfo = field(default_factory=SessionInfo)
    window_info: WindowInfo | None = None

    def to_protocol(self) -> SessionInfo:
        return self.info.copy()

    @property
    def id(self) -> int:
        return self.info.id

    @property
    def name(self) -> str:
        return self.info.name


class SessionStore:
    """Mutable store with stable 16-bit device-facing identifiers."""

    def __init__(self) -> None:
        self._sessions: dict[int, DaemonSession] = {}
        self._next_id = 1

    def allocate_id(self) -> int:
        start = self._next_id
        while True:
            candidate = self._next_id
            self._next_id = (self._next_id + 1) & 0xFFFF
            if self._next_id == 0:
                self._next_id = 1
            if candidate != 0 and candidate not in self._sessions:
                return candidate
            if self._next_id == start:
                raise RuntimeError("session id space exhausted")

    def update(self, session: DaemonSession) -> None:
        if not 0 <= session.id <= 0xFFFF:
            raise ValueError("session id must fit in u16")
        self._sessions[session.id] = session
        if session.id >= self._next_id:
            self._next_id = (session.id + 1) & 0xFFFF or 1

    def remove(self, session_id: int) -> DaemonSession | None:
        return self._sessions.pop(session_id, None)

    def get(self, session_id: int) -> DaemonSession | None:
        return self._sessions.get(session_id)

    def list(self) -> builtins.list[DaemonSession]:
        return sorted(self._sessions.values(), key=lambda session: session.id)

    def __len__(self) -> int:
        return len(self._sessions)

    def __bool__(self) -> bool:
        return bool(self._sessions)

    def to_protocol_list(self) -> builtins.list[SessionInfo]:
        return [session.to_protocol() for session in self.list()]

    def first_with_permission(self) -> DaemonSession | None:
        return next(
            (session for session in self.list() if session.info.has_permission_request),
            None,
        )

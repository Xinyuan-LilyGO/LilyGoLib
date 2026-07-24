"""Shared state owned by one asyncio event loop."""

from __future__ import annotations

import asyncio
from collections import deque
from dataclasses import dataclass, field
from typing import Any

from ...core import PermissionAction
from ..config import DaemonConfig
from ..keystroke import KeystrokeInjector, default_injector
from ..notifications import NotificationQueue
from ..permissions import PermissionQueue, YoloConfig
from ..session import SessionStore
from ..speaker import LocalSpeaker


@dataclass(slots=True)
class DaemonState:
    config: DaemonConfig = field(default_factory=DaemonConfig)
    store: SessionStore = field(default_factory=SessionStore)
    session_id_map: dict[str, int] = field(default_factory=dict)
    permission_queue: PermissionQueue = field(default_factory=PermissionQueue)
    notification_queue: NotificationQueue = field(default_factory=NotificationQueue)
    held_keys: set[str] = field(default_factory=set)
    activity_log: deque[str] = field(default_factory=lambda: deque(maxlen=50))
    active_session_id: int | None = None
    permission_timeout: float = 300.0
    injector: KeystrokeInjector = field(default_factory=default_injector)
    speaker: LocalSpeaker = field(default_factory=LocalSpeaker)
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)
    permission_responses: dict[int, asyncio.Future[PermissionAction]] = field(default_factory=dict)
    transport: Any | None = None

    def __post_init__(self) -> None:
        self.permission_queue = PermissionQueue(self.config.always_allow.patterns)
        self.speaker.set_volume(self.config.sound.volume)
        self.speaker.set_muted(self.config.sound.muted)

    @property
    def yolo(self) -> YoloConfig:
        cfg = self.config.yolo
        return YoloConfig(
            active=cfg.active,
            allow=list(cfg.allow),
            deny=list(cfg.deny),
            notify_auto_allow=cfg.notify_auto_allow,
            auto_allow_log=cfg.auto_allow_log,
        )

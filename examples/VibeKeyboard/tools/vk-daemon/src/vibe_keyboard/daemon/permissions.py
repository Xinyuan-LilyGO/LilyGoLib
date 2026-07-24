"""Fail-closed permission queue and YOLO rule evaluation."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import StrEnum
from fnmatch import fnmatchcase

from ..core import PermissionAction


@dataclass(slots=True)
class YoloConfig:
    active: bool = False
    allow: list[str] = field(default_factory=lambda: ["Read(*)", "Glob(*)", "Grep(*)"])
    deny: list[str] = field(
        default_factory=lambda: ["Bash(git push*)", "Bash(rm -rf*)", "Bash(sudo*)"]
    )
    notify_auto_allow: bool = True
    auto_allow_log: bool = True


class YoloDecision(StrEnum):
    AUTO_ALLOW = "auto_allow"
    AUTO_DENY = "auto_deny"
    ASK_USER = "ask_user"


def _matches_rule(rule: str, tool_name: str, tool_input: str) -> bool:
    if "(" not in rule:
        return fnmatchcase(tool_name, rule)
    tool_pattern, input_pattern = rule.split("(", 1)
    input_pattern = input_pattern.removesuffix(")")
    return fnmatchcase(tool_name, tool_pattern) and fnmatchcase(tool_input, input_pattern)


def evaluate_yolo(config: YoloConfig, tool_name: str, tool_input: str) -> YoloDecision:
    if not config.active:
        return YoloDecision.ASK_USER
    if any(_matches_rule(rule, tool_name, tool_input) for rule in config.deny):
        return YoloDecision.AUTO_DENY
    if any(_matches_rule(rule, tool_name, tool_input) for rule in config.allow):
        return YoloDecision.AUTO_ALLOW
    return YoloDecision.ASK_USER


@dataclass(slots=True)
class PendingPermission:
    session_id: int
    tool_name: str
    tool_input: str


class PermissionQueue:
    MAX_PENDING = 100

    def __init__(self, always_allow: list[str] | None = None) -> None:
        self._pending: list[PendingPermission] = []
        self._always_allow = list(dict.fromkeys(always_allow or []))

    def push(self, permission: PendingPermission) -> bool:
        if len(self._pending) >= self.MAX_PENDING:
            return False
        self._pending.append(permission)
        return True

    def resolve(self, session_id: int, action: PermissionAction) -> PendingPermission | None:
        index = next(
            (index for index, item in enumerate(self._pending) if item.session_id == session_id),
            None,
        )
        if index is None:
            return None
        permission = self._pending.pop(index)
        if action is PermissionAction.ALWAYS:
            self.add_always_allow(f"{permission.tool_name}({permission.tool_input})")
        return permission

    def is_always_allowed(self, tool_name: str, tool_input: str) -> bool:
        return f"{tool_name}({tool_input})" in self._always_allow

    @property
    def always_allow_list(self) -> tuple[str, ...]:
        return tuple(self._always_allow)

    def add_always_allow(self, pattern: str) -> None:
        if pattern not in self._always_allow:
            self._always_allow.append(pattern)

    @property
    def current(self) -> PendingPermission | None:
        return self._pending[0] if self._pending else None

    @property
    def pending_list(self) -> tuple[PendingPermission, ...]:
        return tuple(self._pending)

    def pending_for_session(self, session_id: int) -> bool:
        return any(item.session_id == session_id for item in self._pending)

    def __len__(self) -> int:
        return len(self._pending)

    def __bool__(self) -> bool:
        return bool(self._pending)


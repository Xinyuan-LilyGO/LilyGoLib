"""Validated macOS focus strategies."""

from __future__ import annotations

import re
import subprocess
import sys
from collections.abc import Sequence
from typing import Protocol

from .session import DaemonSession


class FocusError(RuntimeError):
    pass


_TTY_PATTERN = re.compile(r"^/dev/(?:ttys\d+|tty\w+|pts/\d+)$")
_BUNDLE_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9.-]{1,254}$")


def validate_tty(value: str) -> bool:
    return bool(_TTY_PATTERN.fullmatch(value))


def validate_bundle_id(value: str) -> bool:
    return bool(_BUNDLE_PATTERN.fullmatch(value)) and ".." not in value


def escape_jxa_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def escape_applescript_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", " ")


def _run_jxa(script: str) -> str:
    if sys.platform != "darwin":
        raise FocusError("window focus is only available on macOS")
    try:
        result = subprocess.run(
            ["osascript", "-l", "JavaScript", "-e", script],
            check=True,
            capture_output=True,
            text=True,
            timeout=8,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise FocusError(str(error)) from error
    return result.stdout.strip()


class FocusStrategy(Protocol):
    name: str

    def can_focus(self, session: DaemonSession) -> bool: ...

    def activate(self, session: DaemonSession) -> None: ...

    def is_focused(self, session: DaemonSession) -> bool: ...


class BundleFocus:
    name = "generic"
    bundle_ids: tuple[str, ...] = ()

    def can_focus(self, session: DaemonSession) -> bool:
        return not self.bundle_ids or session.info.bundle_id in self.bundle_ids

    def activate(self, session: DaemonSession) -> None:
        bundle_id = session.info.bundle_id
        if not validate_bundle_id(bundle_id):
            raise FocusError(f"invalid bundle id: {bundle_id!r}")
        script = (
            'ObjC.import("AppKit"); '
            f'const apps=$.NSRunningApplication.runningApplicationsWithBundleIdentifier("{escape_jxa_string(bundle_id)}"); '
            "if (apps.count === 0) throw new Error('application not running'); "
            "apps.objectAtIndex(0).activateWithOptions(3);"
        )
        _run_jxa(script)

    def is_focused(self, session: DaemonSession) -> bool:
        bundle_id = session.info.bundle_id
        if not validate_bundle_id(bundle_id):
            return False
        script = (
            'ObjC.import("AppKit"); '
            f'$.NSWorkspace.sharedWorkspace.frontmostApplication.bundleIdentifier.js === "{escape_jxa_string(bundle_id)}"'
        )
        try:
            return _run_jxa(script).lower() == "true"
        except FocusError:
            return False


class ITermFocus(BundleFocus):
    name = "iterm2"
    bundle_ids = ("com.googlecode.iterm2",)

    def activate(self, session: DaemonSession) -> None:
        tty = session.info.session_tty
        if not validate_tty(tty):
            raise FocusError(f"invalid iTerm TTY: {tty!r}")
        escaped = escape_jxa_string(tty)
        script = f"""
const app = Application('iTerm2'); app.activate();
for (const window of app.windows()) {{
  for (const tab of window.tabs()) {{
    for (const pane of tab.sessions()) {{
      if (pane.tty() === \"{escaped}\") {{ pane.select(); window.select(); true; }}
    }}
  }}
}}
"""
        _run_jxa(script)


class GhosttyFocus(BundleFocus):
    name = "ghostty"
    bundle_ids = ("com.mitchellh.ghostty",)


class WarpFocus(BundleFocus):
    name = "warp"
    bundle_ids = ("dev.warp.Warp-Stable",)


class VsCodeFocus(BundleFocus):
    name = "vscode"
    bundle_ids = (
        "com.microsoft.VSCode",
        "com.microsoft.VSCodeInsiders",
        "com.todesktop.230313mzl4w4u92",
        "com.codeium.windsurf",
    )


class GenericMacFocus(BundleFocus):
    name = "generic"


def default_strategies() -> list[FocusStrategy]:
    return [ITermFocus(), GhosttyFocus(), WarpFocus(), VsCodeFocus(), GenericMacFocus()]


def activate_with_strategies(
    strategies: Sequence[FocusStrategy], session: DaemonSession
) -> str:
    for strategy in strategies:
        if strategy.can_focus(session):
            strategy.activate(session)
            return strategy.name
    raise FocusError(f"no focus strategy for session {session.id}")


def is_session_focused(strategies: Sequence[FocusStrategy], session: DaemonSession) -> bool:
    return any(strategy.can_focus(session) and strategy.is_focused(session) for strategy in strategies)


def activate_window(session: DaemonSession) -> str:
    return activate_with_strategies(default_strategies(), session)


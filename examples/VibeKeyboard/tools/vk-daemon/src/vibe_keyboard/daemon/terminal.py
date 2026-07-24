"""Terminal emulator detection."""

from __future__ import annotations

import os
from collections.abc import Mapping
from dataclasses import dataclass
from enum import StrEnum
from typing import Protocol


class TerminalType(StrEnum):
    ITERM2 = "iterm2"
    GHOSTTY = "ghostty"
    WARP = "warp"
    VSCODE = "vscode"
    CURSOR = "cursor"
    WINDSURF = "windsurf"
    WEZTERM = "wezterm"
    ZED = "zed"
    APPLE_TERMINAL = "apple_terminal"
    UNKNOWN = "unknown"


@dataclass(frozen=True, slots=True)
class TerminalInfo:
    terminal_type: TerminalType
    bundle_id: str
    session_tty: str = ""


class TerminalDetector(Protocol):
    @property
    def name(self) -> str: ...

    def detect(self, env: Mapping[str, str]) -> TerminalInfo: ...


class MacTerminalDetector:
    name = "MacTerminalDetector"

    def detect(self, env: Mapping[str, str]) -> TerminalInfo:
        tty = env.get("PEON_SESSION_TTY", env.get("TTY", ""))
        program = env.get("TERM_PROGRAM", "")
        if program == "iTerm.app":
            result = (TerminalType.ITERM2, "com.googlecode.iterm2")
        elif program == "ghostty":
            result = (TerminalType.GHOSTTY, "com.mitchellh.ghostty")
        elif program == "WarpTerminal":
            result = (TerminalType.WARP, "dev.warp.Warp-Stable")
        elif program == "Apple_Terminal":
            result = (TerminalType.APPLE_TERMINAL, "com.apple.Terminal")
        elif program == "WezTerm":
            result = (TerminalType.WEZTERM, "com.github.wez.wezterm")
        elif program == "zed":
            result = (TerminalType.ZED, "dev.zed.Zed")
        elif program == "vscode":
            bundle = env.get("__CFBundleIdentifier", "").lower()
            if "cursor" in bundle or "todesktop" in bundle:
                result = (TerminalType.CURSOR, "com.todesktop.230313mzl4w4u92")
            elif "windsurf" in bundle:
                result = (TerminalType.WINDSURF, "com.codeium.windsurf")
            else:
                result = (TerminalType.VSCODE, "com.microsoft.VSCode")
        elif program in {"tmux", "screen"} and "GHOSTTY_RESOURCES_DIR" in env:
            result = (TerminalType.GHOSTTY, "com.mitchellh.ghostty")
        elif program in {"tmux", "screen"} and "ITERM_SESSION_ID" in env:
            result = (TerminalType.ITERM2, "com.googlecode.iterm2")
        else:
            result = (TerminalType.UNKNOWN, "com.googlecode.iterm2")
        return TerminalInfo(*result, session_tty=tty)


def default_detector() -> TerminalDetector:
    return MacTerminalDetector()


def detect_current_terminal() -> TerminalInfo:
    return default_detector().detect(os.environ)


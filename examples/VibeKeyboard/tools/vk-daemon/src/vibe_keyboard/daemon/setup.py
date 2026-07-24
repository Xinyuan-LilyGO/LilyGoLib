"""Detect and configure supported AI coding tools."""

from __future__ import annotations

import asyncio
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

from .macos_permissions import event_post_access_authorized, microphone_authorization


@dataclass(slots=True)
class AiToolStatus:
    id: str
    name: str
    detected: bool
    hook_installed: bool
    detail: str = ""


@dataclass(slots=True)
class RecommendedTool:
    id: str
    name: str
    installed: bool
    description: str


@dataclass(slots=True)
class SystemStatus:
    accessibility: bool
    daemon_reachable: bool
    device_connected: bool
    microphone_authorization: str = "unknown"


@dataclass(slots=True)
class SetupStatus:
    ai_tools: list[AiToolStatus]
    recommended_tools: list[RecommendedTool]
    system: SystemStatus

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


_HOOK_EVENTS = (
    "SessionStart",
    "SessionEnd",
    "PreToolUse",
    "PostToolUse",
    "UserPromptSubmit",
    "Stop",
    "Notification",
)
_MARKER = "vibe_keyboard.daemon.hook_adapter"
_BREW_ALLOWLIST = {"terminal-notifier", "iterm2"}


def _atomic_write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temp = Path(name)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            handle.write(content)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temp, path)
    finally:
        temp.unlink(missing_ok=True)


def _claude_settings_path() -> Path:
    return Path.home() / ".claude" / "settings.json"


def _codex_config_path() -> Path:
    return Path.home() / ".codex" / "config.toml"


def _codex_hooks_path() -> Path:
    return Path.home() / ".codex" / "hooks.json"


def _hook_args(port: int, event: str, *, codex: bool = False) -> list[str]:
    args = [sys.executable, "-m", _MARKER]
    args.extend(
        (
        "--port",
        str(port),
        "--event",
        event,
        )
    )
    if codex:
        args.append("--codex")
    return args


def _hook_command(port: int, event: str) -> str:
    return " ".join(shlex.quote(arg) for arg in _hook_args(port, event))


def _load_json_object(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
        return value if isinstance(value, dict) else {}
    except (OSError, json.JSONDecodeError):
        return {}


def install_claude_hook(port: int) -> None:
    path = _claude_settings_path()
    settings = _load_json_object(path)
    hooks = settings.setdefault("hooks", {})
    if not isinstance(hooks, dict):
        raise RuntimeError("Claude settings 'hooks' must be an object")
    for event in _HOOK_EVENTS:
        existing = hooks.get(event, [])
        if not isinstance(existing, list):
            existing = []
        existing = [item for item in existing if _MARKER not in json.dumps(item)]
        existing.append({"matcher": "", "hooks": [{"type": "command", "command": _hook_command(port, event)}]})
        hooks[event] = existing
    _atomic_write(path, json.dumps(settings, indent=2, ensure_ascii=False) + "\n")


def uninstall_claude_hook() -> None:
    path = _claude_settings_path()
    settings = _load_json_object(path)
    hooks = settings.get("hooks")
    if not isinstance(hooks, dict):
        return
    for event in list(hooks):
        value = hooks[event]
        if isinstance(value, list):
            filtered = [item for item in value if _MARKER not in json.dumps(item)]
            if filtered:
                hooks[event] = filtered
            else:
                hooks.pop(event, None)
    _atomic_write(path, json.dumps(settings, indent=2, ensure_ascii=False) + "\n")


def _codex_notify_line(port: int) -> str:
    values = _hook_args(port, "Stop", codex=True)
    return "notify = [" + ", ".join(json.dumps(value) for value in values) + "]"


def _codex_hook_command(port: int, event: str) -> str:
    return _hook_command(port, event) + " --codex"


def install_codex_hook(port: int) -> None:
    path = _codex_config_path()
    content = path.read_text(encoding="utf-8") if path.exists() else ""
    backup = path.with_suffix(".toml.vk-backup")
    if not backup.exists() and path.exists():
        _atomic_write(backup, content)
    lines = [line for line in content.splitlines() if not line.lstrip().startswith("notify =")]
    lines.insert(0, _codex_notify_line(port))
    _atomic_write(path, "\n".join(lines).rstrip() + "\n")

    hooks_path = _codex_hooks_path()
    settings = _load_json_object(hooks_path)
    hooks = settings.setdefault("hooks", {})
    if not isinstance(hooks, dict):
        raise RuntimeError("Codex hooks.json 'hooks' must be an object")
    existing = hooks.get("PermissionRequest", [])
    if not isinstance(existing, list):
        existing = []
    existing = [item for item in existing if _MARKER not in json.dumps(item)]
    existing.append(
        {
            "matcher": "",
            "hooks": [
                {
                    "type": "command",
                    "command": _codex_hook_command(port, "PermissionRequest"),
                    "timeout": 310,
                    "statusMessage": "Waiting for Vibe Keyboard approval",
                }
            ],
        }
    )
    hooks["PermissionRequest"] = existing
    _atomic_write(hooks_path, json.dumps(settings, indent=2, ensure_ascii=False) + "\n")


def uninstall_codex_hook() -> None:
    path = _codex_config_path()
    backup = path.with_suffix(".toml.vk-backup")
    if backup.exists():
        _atomic_write(path, backup.read_text(encoding="utf-8"))
        backup.unlink()
    elif path.exists():
        lines = [line for line in path.read_text().splitlines() if _MARKER not in line]
        _atomic_write(path, "\n".join(lines).rstrip() + "\n")

    hooks_path = _codex_hooks_path()
    settings = _load_json_object(hooks_path)
    hooks = settings.get("hooks")
    if not isinstance(hooks, dict):
        return
    value = hooks.get("PermissionRequest")
    if isinstance(value, list):
        filtered = [item for item in value if _MARKER not in json.dumps(item)]
        if filtered:
            hooks["PermissionRequest"] = filtered
        else:
            hooks.pop("PermissionRequest", None)
    _atomic_write(hooks_path, json.dumps(settings, indent=2, ensure_ascii=False) + "\n")


def _claude_hook_active() -> bool:
    return _MARKER in json.dumps(_load_json_object(_claude_settings_path()))


def _codex_hook_active() -> bool:
    hooks = _load_json_object(_codex_hooks_path()).get("hooks")
    if not isinstance(hooks, dict):
        return False
    return _MARKER in json.dumps(hooks.get("PermissionRequest", []))


def detect_all_sync(daemon_port: int, device_connected: bool = False) -> SetupStatus:
    daemon_reachable = False
    import urllib.request

    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{daemon_port}/health", timeout=0.4):
            daemon_reachable = True
    except OSError:
        pass
    accessibility = event_post_access_authorized()
    return SetupStatus(
        ai_tools=[
            AiToolStatus(
                "claude-code",
                "Claude Code",
                shutil.which("claude") is not None or (Path.home() / ".claude").exists(),
                _claude_hook_active(),
            ),
            AiToolStatus(
                "codex",
                "Codex",
                shutil.which("codex") is not None or (Path.home() / ".codex").exists(),
                _codex_hook_active(),
                "approval hook",
            ),
            AiToolStatus(
                "cursor",
                "Cursor",
                Path("/Applications/Cursor.app").exists() or shutil.which("cursor") is not None,
                False,
                "hook adapter not implemented",
            ),
        ],
        recommended_tools=[
            RecommendedTool("iterm2", "iTerm2", Path("/Applications/iTerm.app").exists(), "Precise TTY focus"),
            RecommendedTool("terminal-notifier", "terminal-notifier", shutil.which("terminal-notifier") is not None, "Clickable notifications"),
        ],
        system=SystemStatus(
            accessibility,
            daemon_reachable,
            device_connected,
            microphone_authorization().value,
        ),
    )


class SetupManager:
    @staticmethod
    async def detect_all(daemon_port: int, device_connected: bool = False) -> SetupStatus:
        return await asyncio.to_thread(detect_all_sync, daemon_port, device_connected)

    @staticmethod
    async def install_hook(tool_id: str, port: int) -> None:
        if tool_id == "claude-code":
            await asyncio.to_thread(install_claude_hook, port)
        elif tool_id == "codex":
            await asyncio.to_thread(install_codex_hook, port)
        else:
            raise ValueError(f"unsupported AI tool: {tool_id}")

    @staticmethod
    async def uninstall_hook(tool_id: str) -> None:
        if tool_id == "claude-code":
            await asyncio.to_thread(uninstall_claude_hook)
        elif tool_id == "codex":
            await asyncio.to_thread(uninstall_codex_hook)
        else:
            raise ValueError(f"unsupported AI tool: {tool_id}")

    @staticmethod
    async def brew_install(package: str) -> None:
        if package not in _BREW_ALLOWLIST:
            raise ValueError(f"package is not allowed: {package}")
        await asyncio.to_thread(subprocess.run, ["brew", "install", package], check=True)

    @staticmethod
    async def brew_uninstall(package: str) -> None:
        if package not in _BREW_ALLOWLIST:
            raise ValueError(f"package is not allowed: {package}")
        await asyncio.to_thread(subprocess.run, ["brew", "uninstall", package], check=True)

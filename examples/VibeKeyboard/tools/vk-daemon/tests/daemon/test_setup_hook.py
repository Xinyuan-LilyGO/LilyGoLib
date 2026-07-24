from __future__ import annotations

import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from vibe_keyboard.daemon.hook_adapter import build_payload, format_response
from vibe_keyboard.daemon.setup import (
    install_claude_hook,
    install_codex_hook,
    uninstall_claude_hook,
    uninstall_codex_hook,
)


class HookAdapterTests(unittest.TestCase):
    def test_build_payload_normalizes_nested_tool_input(self) -> None:
        payload = build_payload(
            {"session_id": "abc", "tool_name": "Write", "tool_input": {"path": "a.py"}},
            "PreToolUse",
        )
        self.assertEqual(payload["type"], "PreToolUse")
        self.assertEqual(payload["session_id"], "abc")
        self.assertEqual(json.loads(payload["tool_input"]), {"path": "a.py"})

    def test_codex_payload_uses_thread_id(self) -> None:
        payload = build_payload({"thread-id": "thread-1"}, "Stop", codex=True)
        self.assertEqual(payload["session_id"], "thread-1")
        self.assertEqual(payload["source"], "codex")

    def test_codex_payload_preserves_permission_request(self) -> None:
        payload = build_payload(
            {
                "hook_event_name": "PermissionRequest",
                "session_id": "thread-1",
                "tool_name": "Bash",
                "tool_input": {"command": "git push"},
            },
            "PermissionRequest",
            codex=True,
        )
        self.assertEqual(payload["type"], "PermissionRequest")
        self.assertEqual(payload["tool_name"], "Bash")
        self.assertEqual(json.loads(payload["tool_input"]), {"command": "git push"})

    def test_codex_permission_response_adds_hook_event_name(self) -> None:
        response = {"hookSpecificOutput": {"decision": {"behavior": "allow"}}}
        formatted = format_response(response, {"type": "PermissionRequest"}, codex=True)
        self.assertEqual(
            formatted["hookSpecificOutput"]["hookEventName"], "PermissionRequest"
        )


class SetupTests(unittest.TestCase):
    def test_claude_install_and_uninstall_preserves_other_hooks(self) -> None:
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, {"HOME": directory}):
            settings = Path(directory) / ".claude" / "settings.json"
            settings.parent.mkdir()
            settings.write_text(json.dumps({"hooks": {"Stop": [{"command": "other"}]}}))
            install_claude_hook(19280)
            installed = json.loads(settings.read_text())
            self.assertIn("vibe_keyboard.daemon.hook_adapter", json.dumps(installed))
            uninstall_claude_hook()
            uninstalled = json.loads(settings.read_text())
            self.assertIn("other", json.dumps(uninstalled))
            self.assertNotIn("vibe_keyboard.daemon.hook_adapter", json.dumps(uninstalled))

    def test_codex_install_restores_backup(self) -> None:
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, {"HOME": directory}):
            config = Path(directory) / ".codex" / "config.toml"
            config.parent.mkdir()
            original = 'model = "gpt-5"\n'
            config.write_text(original)
            install_codex_hook(19280)
            self.assertIn("vibe_keyboard.daemon.hook_adapter", config.read_text())
            hooks = json.loads((config.parent / "hooks.json").read_text())
            permission_hooks = hooks["hooks"]["PermissionRequest"]
            self.assertIn("vibe_keyboard.daemon.hook_adapter", json.dumps(permission_hooks))
            uninstall_codex_hook()
            self.assertEqual(config.read_text(), original)
            hooks = json.loads((config.parent / "hooks.json").read_text())
            self.assertNotIn("PermissionRequest", hooks["hooks"])


if __name__ == "__main__":
    unittest.main()

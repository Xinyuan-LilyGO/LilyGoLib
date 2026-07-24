from __future__ import annotations

import unittest
from unittest.mock import patch

from vibe_keyboard.daemon.cli import _prepare_voice_permissions
from vibe_keyboard.daemon.config import DaemonConfig
from vibe_keyboard.daemon.logging import LOGGER_NAME
from vibe_keyboard.daemon.macos_permissions import MicrophoneAuthorization


class VoicePermissionTests(unittest.IsolatedAsyncioTestCase):
    @patch(
        "vibe_keyboard.daemon.cli.request_microphone_access",
        return_value=MicrophoneAuthorization.DENIED,
    )
    @patch("vibe_keyboard.daemon.cli.request_event_post_access", return_value=False)
    async def test_dictation_requests_host_permissions(
        self, request_event_access, request_microphone_access
    ) -> None:
        with self.assertLogs(f"{LOGGER_NAME}.cli", level="WARNING") as captured:
            await _prepare_voice_permissions(DaemonConfig())

        request_microphone_access.assert_called_once_with()
        request_event_access.assert_called_once_with()
        self.assertTrue(any("Microphone" in message for message in captured.output))
        self.assertTrue(any("Accessibility" in message for message in captured.output))

    @patch("vibe_keyboard.daemon.cli.request_microphone_access")
    @patch("vibe_keyboard.daemon.cli.request_event_post_access")
    async def test_custom_voice_macro_does_not_request_host_permissions(
        self, request_event_access, request_microphone_access
    ) -> None:
        config = DaemonConfig()
        config.macros.voice = "fn"

        await _prepare_voice_permissions(config)

        request_microphone_access.assert_not_called()
        request_event_access.assert_not_called()


if __name__ == "__main__":
    unittest.main()

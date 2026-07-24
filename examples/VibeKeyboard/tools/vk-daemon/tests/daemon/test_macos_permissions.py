from __future__ import annotations

import unittest
from unittest.mock import MagicMock, patch

from vibe_keyboard.daemon.macos_permissions import (
    MicrophoneAuthorization,
    event_post_access_authorized,
    microphone_authorization,
    request_event_post_access,
    request_microphone_access,
)


class MacosPermissionTests(unittest.TestCase):
    @patch("vibe_keyboard.daemon.macos_permissions.sys.platform", "darwin")
    @patch("vibe_keyboard.daemon.macos_permissions._event_post_access_functions")
    def test_event_post_access_request_prompts_after_failed_preflight(self, functions) -> None:
        preflight = MagicMock(return_value=False)
        request = MagicMock(return_value=True)
        functions.return_value = (preflight, request)

        self.assertTrue(request_event_post_access())

        preflight.assert_called_once_with()
        request.assert_called_once_with()

    @patch("vibe_keyboard.daemon.macos_permissions.sys.platform", "darwin")
    @patch("vibe_keyboard.daemon.macos_permissions._event_post_access_functions")
    def test_event_post_access_reports_preflight_result(self, functions) -> None:
        preflight = MagicMock(return_value=True)
        functions.return_value = (preflight, MagicMock())

        self.assertTrue(event_post_access_authorized())

        preflight.assert_called_once_with()

    @patch("vibe_keyboard.daemon.macos_permissions.sys.platform", "linux")
    def test_microphone_is_not_applicable_off_macos(self) -> None:
        self.assertEqual(
            microphone_authorization(), MicrophoneAuthorization.NOT_APPLICABLE
        )

    @patch("vibe_keyboard.daemon.macos_permissions.sys.platform", "darwin")
    @patch(
        "vibe_keyboard.daemon.macos_permissions._authorization_status_code",
        return_value=3,
    )
    def test_microphone_authorization_maps_avfoundation_status(self, status_code) -> None:
        self.assertEqual(
            microphone_authorization(), MicrophoneAuthorization.AUTHORIZED
        )
        status_code.assert_called_once_with()

    @patch("vibe_keyboard.daemon.macos_permissions.sys.platform", "darwin")
    @patch(
        "vibe_keyboard.daemon.macos_permissions._authorization_status_code",
        side_effect=OSError,
    )
    def test_microphone_authorization_is_unknown_when_framework_check_fails(self, status) -> None:
        self.assertEqual(microphone_authorization(), MicrophoneAuthorization.UNKNOWN)
        status.assert_called_once_with()

    @patch(
        "vibe_keyboard.daemon.macos_permissions.microphone_authorization",
        return_value=MicrophoneAuthorization.DENIED,
    )
    def test_request_does_not_retry_a_previous_denial(self, authorization) -> None:
        self.assertEqual(request_microphone_access(), MicrophoneAuthorization.DENIED)
        authorization.assert_called_once_with()


if __name__ == "__main__":
    unittest.main()

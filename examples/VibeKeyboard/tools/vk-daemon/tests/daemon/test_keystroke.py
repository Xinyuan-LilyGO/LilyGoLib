from __future__ import annotations

import unittest
from unittest.mock import MagicMock, call, patch

from vibe_keyboard.daemon.keystroke import (
    KeystrokeError,
    MacKeystrokeInjector,
    _post_fn_modifier,
    _send_dictation_shortcut,
)
from vibe_keyboard.daemon.macos_permissions import MicrophoneAuthorization


class MacKeystrokeInjectorTests(unittest.TestCase):
    @patch("vibe_keyboard.daemon.keystroke._send_dictation_shortcut")
    @patch("vibe_keyboard.daemon.keystroke.subprocess.run")
    def test_dictation_uses_native_double_fn_shortcut(self, run, shortcut) -> None:
        MacKeystrokeInjector().send_key("dictation")

        shortcut.assert_called_once_with()
        run.assert_not_called()

    @patch("vibe_keyboard.daemon.keystroke.time.sleep")
    @patch("vibe_keyboard.daemon.keystroke._post_fn_modifier")
    @patch(
        "vibe_keyboard.daemon.keystroke.microphone_authorization",
        return_value=MicrophoneAuthorization.AUTHORIZED,
    )
    def test_native_dictation_shortcut_posts_two_fn_taps(
        self, microphone_authorization, post_fn, sleep
    ) -> None:
        _send_dictation_shortcut()

        microphone_authorization.assert_called_once_with()
        self.assertEqual(
            post_fn.call_args_list,
            [call(True), call(False), call(True), call(False)],
        )
        self.assertEqual(sleep.call_args_list, [call(0.05), call(0.12), call(0.05)])

    @patch("vibe_keyboard.daemon.keystroke._post_fn_modifier")
    @patch(
        "vibe_keyboard.daemon.keystroke.microphone_authorization",
        return_value=MicrophoneAuthorization.DENIED,
    )
    def test_dictation_reports_denied_microphone_permission(
        self, microphone_authorization, post_fn
    ) -> None:
        with self.assertRaisesRegex(KeystrokeError, "app running vk-daemon"):
            _send_dictation_shortcut()

        microphone_authorization.assert_called_once_with()
        post_fn.assert_not_called()

    @patch("vibe_keyboard.daemon.keystroke._core_graphics_functions")
    def test_native_fn_event_uses_flags_changed_modifier(self, functions) -> None:
        create_event = MagicMock(return_value=123)
        set_event_type = MagicMock()
        set_event_flags = MagicMock()
        post_event = MagicMock()
        release_event = MagicMock()
        functions.return_value = (
            create_event,
            set_event_type,
            set_event_flags,
            post_event,
            release_event,
        )

        _post_fn_modifier(True)

        create_event.assert_called_once_with(None, 0x3F, True)
        set_event_type.assert_called_once_with(123, 12)
        set_event_flags.assert_called_once_with(123, 0x00800000)
        post_event.assert_called_once_with(0, 123)
        release_event.assert_called_once_with(123)

    @patch.object(MacKeystrokeInjector, "send_key")
    def test_dictation_press_and_release_both_toggle(self, send_key) -> None:
        injector = MacKeystrokeInjector()

        injector.send_key_down("dictation")
        injector.send_key_up("dictation")

        self.assertEqual(send_key.call_args_list[0].args, ("dictation",))
        self.assertEqual(send_key.call_args_list[1].args, ("dictation",))

    def test_unknown_action_is_rejected(self) -> None:
        with self.assertRaisesRegex(KeystrokeError, "unsupported key action"):
            MacKeystrokeInjector().send_key("unknown")


if __name__ == "__main__":
    unittest.main()

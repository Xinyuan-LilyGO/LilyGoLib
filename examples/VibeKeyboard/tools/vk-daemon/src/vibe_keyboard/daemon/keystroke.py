"""Platform keystroke injection with a narrow action allowlist."""

from __future__ import annotations

import ctypes
import ctypes.util
import subprocess
import sys
import time
from functools import cache
from typing import Any, Protocol

from .macos_permissions import MicrophoneAuthorization, microphone_authorization


class KeystrokeError(RuntimeError):
    pass


class KeystrokeInjector(Protocol):
    platform: str

    def send_key(self, action: str) -> None: ...

    def send_key_down(self, action: str) -> None: ...

    def send_key_up(self, action: str) -> None: ...


_KEY_ACTIONS: dict[str, tuple[str, ...]] = {
    "enter": ("key code 36",),
    "return": ("key code 36",),
    "escape": ("key code 53",),
    "backspace": ("key code 51",),
    "delete": ("key code 51",),
    "space": ("key code 49",),
    "tab": ("key code 48",),
    "up": ("key code 126",),
    "down": ("key code 125",),
    "left": ("key code 123",),
    "right": ("key code 124",),
    "ctrl_u": ('keystroke "u" using control down',),
    "cmd_enter": ("key code 36 using command down",),
    "fn": ("key code 63",),
}

_CG_HID_EVENT_TAP = 0
_CG_EVENT_FLAGS_CHANGED = 12
_CG_EVENT_FLAG_MASK_SECONDARY_FN = 0x00800000
_MAC_VIRTUAL_KEY_FUNCTION = 0x3F
_DICTATION_KEY_HOLD_SECONDS = 0.05
_DICTATION_TAP_INTERVAL_SECONDS = 0.12


@cache
def _core_graphics_functions() -> tuple[Any, Any, Any, Any, Any]:
    application_services_path = ctypes.util.find_library("ApplicationServices")
    core_foundation_path = ctypes.util.find_library("CoreFoundation")
    if application_services_path is None or core_foundation_path is None:
        raise KeystrokeError("macOS CoreGraphics frameworks are unavailable")

    application_services = ctypes.CDLL(application_services_path)
    core_foundation = ctypes.CDLL(core_foundation_path)
    preflight_event_access = application_services.CGPreflightPostEventAccess
    preflight_event_access.restype = ctypes.c_bool
    if not preflight_event_access():
        raise KeystrokeError(
            "macOS Accessibility permission is required to control Dictation"
        )
    create_event = application_services.CGEventCreateKeyboardEvent
    create_event.argtypes = [ctypes.c_void_p, ctypes.c_uint16, ctypes.c_bool]
    create_event.restype = ctypes.c_void_p
    set_event_type = application_services.CGEventSetType
    set_event_type.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    set_event_flags = application_services.CGEventSetFlags
    set_event_flags.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
    post_event = application_services.CGEventPost
    post_event.argtypes = [ctypes.c_uint32, ctypes.c_void_p]
    release_event = core_foundation.CFRelease
    release_event.argtypes = [ctypes.c_void_p]
    return create_event, set_event_type, set_event_flags, post_event, release_event


def _post_fn_modifier(pressed: bool) -> None:
    create_event, set_event_type, set_event_flags, post_event, release_event = (
        _core_graphics_functions()
    )
    event = create_event(None, _MAC_VIRTUAL_KEY_FUNCTION, pressed)
    if not event:
        raise KeystrokeError("failed to create macOS Fn event")
    try:
        set_event_type(event, _CG_EVENT_FLAGS_CHANGED)
        set_event_flags(event, _CG_EVENT_FLAG_MASK_SECONDARY_FN if pressed else 0)
        post_event(_CG_HID_EVENT_TAP, event)
    finally:
        release_event(event)


def _send_dictation_shortcut() -> None:
    microphone = microphone_authorization()
    if microphone in {
        MicrophoneAuthorization.RESTRICTED,
        MicrophoneAuthorization.DENIED,
    }:
        raise KeystrokeError(
            "macOS Microphone permission is required for the app running vk-daemon; "
            "enable it in System Settings > Privacy & Security > Microphone"
        )
    for tap_index in range(2):
        _post_fn_modifier(True)
        time.sleep(_DICTATION_KEY_HOLD_SECONDS)
        _post_fn_modifier(False)
        if tap_index == 0:
            time.sleep(_DICTATION_TAP_INTERVAL_SECONDS)


class MacKeystrokeInjector:
    platform = "macos"

    def _run(self, statements: tuple[str, ...]) -> None:
        arguments = ["osascript", "-e", 'tell application "System Events"']
        for statement in statements:
            arguments.extend(("-e", statement))
        arguments.extend(("-e", "end tell"))
        try:
            subprocess.run(
                arguments,
                check=True,
                timeout=5,
                capture_output=True,
            )
        except (OSError, subprocess.SubprocessError) as error:
            raise KeystrokeError(str(error)) from error

    def send_key(self, action: str) -> None:
        normalized = action.lower()
        if normalized == "dictation":
            _send_dictation_shortcut()
            return
        try:
            statements = _KEY_ACTIONS[normalized]
        except KeyError as error:
            raise KeystrokeError(f"unsupported key action: {action}") from error
        self._run(statements)

    def send_key_down(self, action: str) -> None:
        # Dictation is a toggle shortcut, so both physical edges invoke it.
        self.send_key(action)

    def send_key_up(self, action: str) -> None:
        normalized = action.lower()
        if normalized == "dictation":
            self.send_key(normalized)
        elif normalized not in _KEY_ACTIONS:
            raise KeystrokeError(f"unsupported key action: {action}")


class NullKeystrokeInjector:
    platform = "null"

    def send_key(self, action: str) -> None:
        raise KeystrokeError(f"keystroke injection unavailable: {action}")

    send_key_down = send_key
    send_key_up = send_key


def default_injector() -> KeystrokeInjector:
    return MacKeystrokeInjector() if sys.platform == "darwin" else NullKeystrokeInjector()


def execute_button_action(action: str) -> None:
    default_injector().send_key(action)


def execute_key_down(action: str) -> None:
    default_injector().send_key_down(action)


def execute_key_up(action: str) -> None:
    default_injector().send_key_up(action)

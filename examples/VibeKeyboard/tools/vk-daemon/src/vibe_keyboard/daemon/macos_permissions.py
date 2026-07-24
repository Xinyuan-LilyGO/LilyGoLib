"""macOS privacy authorization checks and user-consent requests."""

from __future__ import annotations

import ctypes
import ctypes.util
import sys
import threading
from enum import StrEnum
from functools import cache
from typing import Any, ClassVar


class MicrophoneAuthorization(StrEnum):
    NOT_APPLICABLE = "not_applicable"
    NOT_DETERMINED = "not_determined"
    RESTRICTED = "restricted"
    DENIED = "denied"
    AUTHORIZED = "authorized"
    UNKNOWN = "unknown"


_BlockInvoke = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_bool)


class _BlockDescriptor(ctypes.Structure):
    _fields_: ClassVar[list[tuple[str, Any]]] = [
        ("reserved", ctypes.c_ulong),
        ("size", ctypes.c_ulong),
    ]


class _BlockLiteral(ctypes.Structure):
    _fields_: ClassVar[list[tuple[str, Any]]] = [
        ("isa", ctypes.c_void_p),
        ("flags", ctypes.c_int),
        ("reserved", ctypes.c_int),
        ("invoke", _BlockInvoke),
        ("descriptor", ctypes.POINTER(_BlockDescriptor)),
    ]


# AVFoundation may retain and invoke the completion block after our wait times out.
_PENDING_REQUEST_BLOCKS: list[tuple[Any, ...]] = []
_BLOCK_IS_GLOBAL = 1 << 28


@cache
def _event_post_access_functions() -> tuple[Any, Any]:
    application_services_path = ctypes.util.find_library("ApplicationServices")
    if application_services_path is None:
        raise OSError("macOS ApplicationServices framework is unavailable")
    application_services = ctypes.CDLL(application_services_path)
    preflight = application_services.CGPreflightPostEventAccess
    preflight.restype = ctypes.c_bool
    request = application_services.CGRequestPostEventAccess
    request.restype = ctypes.c_bool
    return preflight, request


def event_post_access_authorized() -> bool:
    if sys.platform != "darwin":
        return True
    try:
        preflight, _ = _event_post_access_functions()
        return bool(preflight())
    except (OSError, TypeError, ValueError):
        return False


def request_event_post_access() -> bool:
    if event_post_access_authorized():
        return True
    if sys.platform != "darwin":
        return True
    try:
        _, request = _event_post_access_functions()
        return bool(request())
    except (OSError, TypeError, ValueError):
        return False


@cache
def _avfoundation_functions() -> tuple[Any, Any, int, int, int]:
    avfoundation_path = ctypes.util.find_library("AVFoundation")
    objc_path = ctypes.util.find_library("objc")
    system_path = ctypes.util.find_library("System")
    if avfoundation_path is None or objc_path is None or system_path is None:
        raise OSError("macOS AVFoundation frameworks are unavailable")

    avfoundation = ctypes.CDLL(avfoundation_path)
    objc = ctypes.CDLL(objc_path)
    system = ctypes.CDLL(system_path)
    get_class = objc.objc_getClass
    get_class.argtypes = [ctypes.c_char_p]
    get_class.restype = ctypes.c_void_p
    register_selector = objc.sel_registerName
    register_selector.argtypes = [ctypes.c_char_p]
    register_selector.restype = ctypes.c_void_p
    send_message_address = ctypes.cast(objc.objc_msgSend, ctypes.c_void_p).value
    audio_media_type = ctypes.c_void_p.in_dll(avfoundation, "AVMediaTypeAudio").value
    global_block = ctypes.c_void_p.in_dll(system, "_NSConcreteGlobalBlock").value
    if send_message_address is None or audio_media_type is None or global_block is None:
        raise OSError("macOS microphone authorization symbols are unavailable")
    return (
        get_class,
        register_selector,
        send_message_address,
        audio_media_type,
        global_block,
    )


def _authorization_status_code() -> int:
    get_class, register_selector, send_message_address, audio_media_type, _ = (
        _avfoundation_functions()
    )
    capture_device = get_class(b"AVCaptureDevice")
    authorization_selector = register_selector(b"authorizationStatusForMediaType:")
    if not capture_device or not authorization_selector:
        raise OSError("macOS capture authorization API is unavailable")
    send_message = ctypes.CFUNCTYPE(
        ctypes.c_long,
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_void_p,
    )(send_message_address)
    return int(send_message(capture_device, authorization_selector, audio_media_type))


def microphone_authorization() -> MicrophoneAuthorization:
    if sys.platform != "darwin":
        return MicrophoneAuthorization.NOT_APPLICABLE
    try:
        status = _authorization_status_code()
    except (OSError, TypeError, ValueError):
        return MicrophoneAuthorization.UNKNOWN
    return {
        0: MicrophoneAuthorization.NOT_DETERMINED,
        1: MicrophoneAuthorization.RESTRICTED,
        2: MicrophoneAuthorization.DENIED,
        3: MicrophoneAuthorization.AUTHORIZED,
    }.get(status, MicrophoneAuthorization.UNKNOWN)


def request_microphone_access(timeout_seconds: float = 30.0) -> MicrophoneAuthorization:
    current = microphone_authorization()
    if current is not MicrophoneAuthorization.NOT_DETERMINED:
        return current

    try:
        get_class, register_selector, send_message_address, audio_media_type, global_block = (
            _avfoundation_functions()
        )
        capture_device = get_class(b"AVCaptureDevice")
        request_selector = register_selector(b"requestAccessForMediaType:completionHandler:")
        if not capture_device or not request_selector:
            return MicrophoneAuthorization.UNKNOWN

        completed = threading.Event()
        result: list[bool] = []

        @_BlockInvoke
        def completion(_block: int, granted: bool) -> None:
            result.append(bool(granted))
            completed.set()

        descriptor = _BlockDescriptor(0, ctypes.sizeof(_BlockLiteral))
        block = _BlockLiteral(
            global_block,
            _BLOCK_IS_GLOBAL,
            0,
            completion,
            ctypes.pointer(descriptor),
        )
        _PENDING_REQUEST_BLOCKS.append((completion, descriptor, block))
        send_request = ctypes.CFUNCTYPE(
            None,
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_void_p,
        )(send_message_address)
        send_request(
            capture_device,
            request_selector,
            audio_media_type,
            ctypes.byref(block),
        )
        if completed.wait(timeout_seconds) and result:
            return (
                MicrophoneAuthorization.AUTHORIZED
                if result[0]
                else MicrophoneAuthorization.DENIED
            )
    except (OSError, TypeError, ValueError):
        return MicrophoneAuthorization.UNKNOWN
    return microphone_authorization()

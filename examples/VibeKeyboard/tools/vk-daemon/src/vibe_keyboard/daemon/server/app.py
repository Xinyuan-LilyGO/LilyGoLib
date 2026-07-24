"""HTTP route contract and daemon business orchestration."""

from __future__ import annotations

import asyncio
import json
import logging
import re
import time
from collections.abc import Mapping
from contextlib import suppress
from copy import deepcopy
from dataclasses import asdict, dataclass, field
from datetime import datetime
from email.parser import BytesParser
from email.policy import default as email_policy
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, unquote, urlsplit

from ...core import (
    ButtonId,
    Direction,
    LedColor,
    NotificationInfo,
    PermissionAction,
    SessionInfo,
    SessionStatus,
    SoundType,
)
from ...protocol import (
    ButtonPress as UplinkButtonPress,
)
from ...protocol import (
    ButtonRelease as UplinkButtonRelease,
)
from ...protocol import (
    DismissPermission,
    DownlinkMessage,
    NotificationListUpdate,
    PlaySound,
    SessionListClear,
    SessionListUpdate,
    SessionRemove,
    SessionSwitch,
    SessionUpsert,
    SetKnobRing,
    SetLed,
    SetupActionRequest,
    SetupActionResult,
    SetupStatusRequest,
    SetupStatusUpdate,
    SetupToolStatus,
    TimeSync,
    TimeSyncRequest,
    UplinkMessage,
    YoloConfigRequest,
    YoloConfigSet,
    YoloConfigUpdate,
    encode_downlink,
)
from ...protocol import (
    KnobPress as UplinkKnobPress,
)
from ...protocol import (
    KnobRelease as UplinkKnobRelease,
)
from ...protocol import (
    KnobRotate as UplinkKnobRotate,
)
from ...protocol import (
    PermissionRequest as DownlinkPermissionRequest,
)
from ...protocol import (
    PermissionResponse as UplinkPermissionResponse,
)
from ...transport import TransportError
from ..config import YoloFileConfig, default_config_path, save_config, set_config_value
from ..focus import FocusError, activate_window
from ..logging import LOGGER_NAME
from ..notifications import Notification
from ..permissions import PendingPermission, YoloDecision, evaluate_yolo
from ..session import DaemonSession, HookEvent, SessionEvent, SessionEventKind, parse_hook_event
from ..setup import SetupManager
from .state import DaemonState

LOGGER = logging.getLogger(f"{LOGGER_NAME}.server")
# GATT Write Request payloads hit platform/peripheral limits well below the
# firmware queue size. Keep the session snapshot under the practical 512-byte
# attribute-write ceiling so CoreBluetooth/NimBLE accepts it.
BLE_SESSION_LIST_MAX_BYTES = 500
BLE_SESSION_NAME_MAX_BYTES = 35
BLE_SESSION_SOURCE_MAX_BYTES = 23
BLE_SESSION_MODEL_MAX_BYTES = 23
BLE_SESSION_PROMPT_MAX_BYTES = 128
BLE_NOTIFICATION_LIST_MAX_BYTES = 500
BLE_NOTIFICATION_MAX_ITEMS = 32
BLE_NOTIFICATION_NAME_MAX_BYTES = 35
BLE_NOTIFICATION_DESCRIPTION_MAX_BYTES = 160
BLE_PERMISSION_REQUEST_MAX_BYTES = 500


def _current_time_sync() -> TimeSync:
    now = datetime.now().astimezone()
    offset = now.utcoffset()
    offset_minutes = round(offset.total_seconds() / 60) if offset is not None else 0
    return TimeSync(int(now.timestamp() * 1000), offset_minutes)


def _yolo_config_update(config: YoloFileConfig) -> YoloConfigUpdate:
    return YoloConfigUpdate(
        active=config.active,
        notify_auto_allow=config.notify_auto_allow,
        allow=config.allow,
        deny=config.deny,
    )


@dataclass(slots=True)
class AppResponse:
    status: int
    body: bytes = b""
    headers: dict[str, str] = field(default_factory=dict)


def _json_response(value: Any, status: int = 200) -> AppResponse:
    return AppResponse(
        status,
        json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode(),
        {"Content-Type": "application/json; charset=utf-8"},
    )


def _status_value(status: SessionStatus) -> str:
    return str(status.value)


def session_to_api(session: SessionInfo) -> dict[str, Any]:
    """Stable HTTP DTO; intentionally differs from the domain field name."""
    data = asdict(session)
    data["status"] = _status_value(session.status)
    data["has_permission"] = data.pop("has_permission_request")
    return data


def notification_to_api(notification: Notification) -> dict[str, Any]:
    data = asdict(notification)
    data["status"] = _status_value(notification.status)
    return data


def _truncate_utf8(value: str, max_bytes: int) -> str:
    raw = value.encode("utf-8")
    if len(raw) <= max_bytes:
        return value
    return raw[:max_bytes].decode("utf-8", errors="ignore")


def _compact_ble_session(session: SessionInfo) -> SessionInfo:
    prompt = session.last_message or session.last_ai_output
    return SessionInfo(
        id=session.id,
        name=_truncate_utf8(session.name, BLE_SESSION_NAME_MAX_BYTES),
        status=session.status,
        has_permission_request=session.has_permission_request,
        source=_truncate_utf8(session.source, BLE_SESSION_SOURCE_MAX_BYTES),
        model=_truncate_utf8(session.model, BLE_SESSION_MODEL_MAX_BYTES),
        tokens_in=session.tokens_in,
        tokens_out=session.tokens_out,
        cost_usd=session.cost_usd,
        context_pct=session.context_pct,
        last_message=_truncate_utf8(prompt, BLE_SESSION_PROMPT_MAX_BYTES),
        started_at=session.started_at,
        last_activity=session.last_activity,
    )


def _compact_ble_notification(notification: NotificationInfo) -> NotificationInfo:
    return notification.copy(
        session_name=_truncate_utf8(notification.session_name, BLE_NOTIFICATION_NAME_MAX_BYTES),
        description=_truncate_utf8(
            notification.description, BLE_NOTIFICATION_DESCRIPTION_MAX_BYTES
        ),
    )


def _ble_notification_list_update(
    notifications: list[NotificationInfo],
) -> NotificationListUpdate:
    included: list[NotificationInfo] = []
    for notification in notifications[:BLE_NOTIFICATION_MAX_ITEMS]:
        candidate = [*included, _compact_ble_notification(notification)]
        update = NotificationListUpdate(candidate)
        if len(encode_downlink(update)) > BLE_NOTIFICATION_LIST_MAX_BYTES:
            break
        included = candidate
    return NotificationListUpdate(included)


def _ble_permission_request(session_id: int, description: str) -> DownlinkPermissionRequest:
    # tag:u8 + session_id:u16 + string_length:u16 leave 495 bytes for UTF-8 text.
    max_description_bytes = BLE_PERMISSION_REQUEST_MAX_BYTES - 5
    return DownlinkPermissionRequest(
        session_id,
        _truncate_utf8(description, max_description_bytes),
    )


def _ble_session_list_update(
    sessions: list[SessionInfo], active_session_id: int | None
) -> SessionListUpdate:
    compact = [_compact_ble_session(session) for session in sessions]
    included: list[SessionInfo] = []
    active_index = 0
    for session in compact:
        candidate = [*included, session]
        candidate_ids = [item.id for item in candidate]
        candidate_active = (
            candidate_ids.index(active_session_id)
            if active_session_id in candidate_ids
            else min(active_index, max(0, len(candidate) - 1))
        )
        size = len(encode_downlink(SessionListUpdate(candidate, candidate_active)))
        if size > BLE_SESSION_LIST_MAX_BYTES:
            break
        included = candidate
        active_index = candidate_active
    if active_session_id is not None and active_session_id not in [item.id for item in included]:
        active = next((item for item in compact if item.id == active_session_id), None)
        if active is not None:
            while included:
                candidate = [*included, active]
                size = len(encode_downlink(SessionListUpdate(candidate, len(candidate) - 1)))
                if size <= BLE_SESSION_LIST_MAX_BYTES:
                    included = candidate
                    active_index = len(candidate) - 1
                    break
                included.pop()
            if active.id not in [item.id for item in included]:
                size = len(encode_downlink(SessionListUpdate([active], 0)))
                if size <= BLE_SESSION_LIST_MAX_BYTES:
                    included = [active]
                    active_index = 0
    if not included:
        return SessionListUpdate([], 0)
    return SessionListUpdate(included, active_index)


def _ble_session_incremental_update(
    sessions: list[SessionInfo],
    active_session_id: int | None,
    previous: dict[int, bytes] | None,
) -> tuple[list[DownlinkMessage], dict[int, bytes]]:
    active_id = active_session_id or 0
    upserts = [
        SessionUpsert(_compact_ble_session(session), session.id == active_session_id)
        for session in sessions
    ]
    current = {message.session.id: encode_downlink(message) for message in upserts}
    if previous is None:
        return [SessionListClear(active_id), *upserts], current
    messages: list[DownlinkMessage] = [
        SessionRemove(session_id) for session_id in previous.keys() - current.keys()
    ]
    messages.extend(
        message
        for message in upserts
        if previous.get(message.session.id) != current[message.session.id]
    )
    return messages, current


class DaemonApplication:
    JSON_LIMIT = 1_048_576

    def __init__(
        self,
        state: DaemonState | None = None,
        *,
        config_path: Path | None = None,
    ) -> None:
        self.state = state or DaemonState()
        self.config_path = config_path
        self._ble_session_cache: dict[int, bytes] | None = None
        self._ble_session_cache_transport: object | None = None
        self._device_sync_lock = asyncio.Lock()

    def _config_path(self) -> Path:
        return self.config_path or default_config_path()

    async def set_transport(self, transport: object | None) -> None:
        async with self._device_sync_lock:
            async with self.state.lock:
                self.state.transport = transport
            if transport is not None:
                LOGGER.info(
                    "device.transport connected transport=%s connected=%s",
                    type(transport).__name__,
                    getattr(transport, "connected", None),
                )
                if type(transport).__name__ == "BleTransport":
                    self._ble_session_cache = None
                    self._ble_session_cache_transport = transport
                    await asyncio.sleep(0.5)
                    await self._send_downlink_unlocked(_current_time_sync())
                await self._sync_device()

    async def clear_transport(self, transport: object | None) -> None:
        cleared = False
        async with self._device_sync_lock, self.state.lock:
            if transport is None or self.state.transport is transport:
                self.state.transport = None
                cleared = True
                if transport is None or self._ble_session_cache_transport is transport:
                    self._ble_session_cache = None
                    self._ble_session_cache_transport = None
                LOGGER.info(
                    "device.transport cleared transport=%s",
                    type(transport).__name__ if transport is not None else "none",
                )
        if cleared:
            await self.release_held_inputs()

    async def release_held_inputs(self) -> None:
        async with self.state.lock:
            held = tuple(self.state.held_keys)
        for marker in held:
            _button, separator, action = marker.partition(":")
            if not separator:
                continue
            try:
                await asyncio.to_thread(self.state.injector.send_key_up, action)
            except Exception as error:
                LOGGER.warning("input.release failed marker=%s error=%s", marker, error)
                continue
            async with self.state.lock:
                self.state.held_keys.discard(marker)

    async def _send_downlink_unlocked(self, message: DownlinkMessage) -> bool:
        transport = self.state.transport
        if transport is None or not transport.connected:
            LOGGER.debug(
                "device.downlink skipped message=%s reason=%s",
                type(message).__name__,
                "no_transport" if transport is None else "transport_disconnected",
            )
            return False
        try:
            await transport.send_downlink(message)
            LOGGER.debug(
                "device.downlink sent transport=%s message=%s",
                type(transport).__name__,
                type(message).__name__,
            )
            return True
        except TransportError as error:
            LOGGER.warning(
                "device.downlink failed transport=%s message=%s error=%s",
                type(transport).__name__,
                type(message).__name__,
                error,
            )
            if not transport.connected:
                async with self.state.lock:
                    if self.state.transport is transport:
                        self.state.transport = None
            return False

    async def _send_downlinks(self, *messages: DownlinkMessage) -> None:
        async with self._device_sync_lock:
            for message in messages:
                await self._send_downlink_unlocked(message)

    async def sync_device(self) -> None:
        async with self._device_sync_lock:
            await self._sync_device()

    async def _sync_device(self) -> None:
        async with self.state.lock:
            sessions = self.state.store.to_protocol_list()
            ids = [session.id for session in sessions]
            active_session_id = self.state.active_session_id
            try:
                active_index = ids.index(active_session_id) if active_session_id is not None else 0
            except ValueError:
                active_index = 0
            permissions = tuple(self.state.permission_queue.pending_list)
            notifications = [item.to_protocol() for item in self.state.notification_queue.all()]
            transport = self.state.transport
        is_ble_transport = type(transport).__name__ == "BleTransport"
        if is_ble_transport:
            previous = (
                self._ble_session_cache if self._ble_session_cache_transport is transport else None
            )
            session_updates, next_ble_cache = _ble_session_incremental_update(
                sessions, active_session_id, previous
            )
            notification_update = _ble_notification_list_update(notifications)
        else:
            session_updates = [SessionListUpdate(sessions, active_index)]
            next_ble_cache = None
            notification_update = NotificationListUpdate(notifications)
        session_update_bytes = sum(len(encode_downlink(message)) for message in session_updates)
        if transport is None or not transport.connected:
            LOGGER.info(
                "device.sync skipped reason=%s sessions=%d permissions=%d notifications=%d",
                "no_transport" if transport is None else "transport_disconnected",
                len(sessions),
                len(permissions),
                len(notifications),
            )
        else:
            LOGGER.info(
                "device.sync transport=%s sessions=%d sent_sessions=%d active_index=%d bytes=%d permissions=%d notifications=%d",
                type(transport).__name__,
                len(sessions),
                len(sessions),
                active_index,
                session_update_bytes,
                len(permissions),
                len(notifications),
            )
        session_sync_succeeded = True
        for session_update in session_updates:
            sent = await self._send_downlink_unlocked(session_update)
            session_sync_succeeded = sent and session_sync_succeeded
        if (
            is_ble_transport
            and transport is not None
            and transport.connected
            and session_sync_succeeded
        ):
            self._ble_session_cache = next_ble_cache
            self._ble_session_cache_transport = transport
        await self._send_downlink_unlocked(notification_update)
        await self._send_downlink_unlocked(_yolo_config_update(self.state.config.yolo))
        for permission in permissions:
            description = f"{permission.tool_name}({permission.tool_input})"
            message = (
                _ble_permission_request(permission.session_id, description)
                if is_ble_transport
                else DownlinkPermissionRequest(permission.session_id, description)
            )
            await self._send_downlink_unlocked(message)

    async def dispatch(
        self,
        method: str,
        target: str,
        headers: Mapping[str, str],
        body: bytes,
    ) -> AppResponse:
        split = urlsplit(target)
        path = split.path
        query = parse_qs(split.query)
        origin = headers.get("origin", "")
        if origin and not re.fullmatch(
            r"https?://(?:127\.0\.0\.1|localhost|\[::1\])(?::\d+)?", origin
        ):
            return _json_response({"error": "cross-origin request denied"}, 403)
        response = await self._dispatch_route(method, path, query, headers, body)
        if origin:
            response.headers.setdefault("Access-Control-Allow-Origin", origin)
            response.headers.setdefault("Vary", "Origin")
        return response

    async def _dispatch_route(
        self,
        method: str,
        path: str,
        query: dict[str, list[str]],
        headers: Mapping[str, str],
        body: bytes,
    ) -> AppResponse:
        if method == "OPTIONS":
            return AppResponse(
                204,
                headers={
                    "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
                    "Access-Control-Allow-Headers": "Content-Type, X-Vibe-Keyboard",
                    "Access-Control-Max-Age": "600",
                },
            )
        if method == "GET" and path == "/health":
            return _json_response({"status": "ok"})
        if method == "GET" and path == "/device/state":
            return await self._device_state()
        if method == "GET" and path == "/sessions":
            async with self.state.lock:
                return _json_response(
                    [session_to_api(item.info) for item in self.state.store.list()]
                )
        if method == "POST" and path == "/event":
            payload = self._json_body(body)
            return await self._handle_event(payload)
        if method == "GET" and path == "/log":
            return _json_response(list(self.state.activity_log))
        if method == "GET" and path == "/yolo":
            return _json_response(asdict(self.state.yolo))
        if method == "GET" and path == "/button/state":
            return _json_response({"held": sorted(self.state.held_keys)})
        if method == "POST" and path == "/button":
            return await self._handle_button(self._json_body(body))
        if method == "POST" and path == "/knob":
            return await self._handle_knob(self._json_body(body))
        if method == "GET" and path == "/config":
            return _json_response(self.state.config.to_dict())
        if method == "POST" and path == "/config":
            return await self._handle_config(self._json_body(body))
        if method == "POST" and path == "/notify/test":
            async with self.state.lock:
                session_id = self.state.active_session_id or 0
                self.state.notification_queue.push(
                    session_id, "Vibe Keyboard", SessionStatus.DONE, "Test notification"
                )
            await self.sync_device()
            return _json_response({"ok": True})
        if method == "GET" and path == "/notifications":
            return _json_response(
                [notification_to_api(item) for item in self.state.notification_queue.all()]
            )
        if method == "POST" and path.startswith("/permissions/"):
            return await self._handle_permission_response(
                path.removeprefix("/permissions/"), self._json_body(body)
            )
        if method == "POST" and path.startswith("/focus/"):
            return await self._handle_focus(path.removeprefix("/focus/"))
        if method == "GET" and path == "/setup/status":
            result = await SetupManager.detect_all(self.state.config.general.hook_port, False)
            return _json_response(result.to_dict())
        if method == "POST" and path.startswith("/setup/install/"):
            return await self._setup_action(
                "install", unquote(path.removeprefix("/setup/install/"))
            )
        if method == "POST" and path.startswith("/setup/uninstall/"):
            return await self._setup_action(
                "uninstall", unquote(path.removeprefix("/setup/uninstall/"))
            )
        if method == "POST" and path.startswith("/setup/brew-install/"):
            return await self._setup_action(
                "brew-install", unquote(path.removeprefix("/setup/brew-install/"))
            )
        if method == "POST" and path.startswith("/setup/brew-uninstall/"):
            return await self._setup_action(
                "brew-uninstall", unquote(path.removeprefix("/setup/brew-uninstall/"))
            )
        if method == "GET" and path == "/sounds":
            return _json_response(self._list_sounds())
        if method == "POST" and path == "/sounds/play":
            return await self._play_sound(self._json_body(body))
        if method == "POST" and path == "/sounds/upload":
            return self._upload_sound(headers, body, query)
        return _json_response({"error": "not found"}, 404)

    def _json_body(self, body: bytes) -> dict[str, Any]:
        if len(body) > self.JSON_LIMIT:
            raise ValueError("JSON body too large")
        try:
            value = json.loads(body or b"{}")
        except (json.JSONDecodeError, UnicodeDecodeError) as error:
            raise ValueError("invalid JSON") from error
        if not isinstance(value, dict):
            raise ValueError("JSON body must be an object")
        return value

    async def _device_state(self) -> AppResponse:
        async with self.state.lock:
            transport = self.state.transport
            return _json_response(
                {
                    "active_session_id": self.state.active_session_id,
                    "ble_connected": bool(
                        transport is not None
                        and type(transport).__name__ == "BleTransport"
                        and transport.connected
                    ),
                    "sessions": [
                        session_to_api(item.info) for item in self.state.store.list()
                    ],
                    "notifications": [
                        notification_to_api(item)
                        for item in self.state.notification_queue.all()
                    ],
                    "pending_permissions": [
                        asdict(item) for item in self.state.permission_queue.pending_list
                    ],
                    "held_keys": sorted(self.state.held_keys),
                    "yolo": asdict(self.state.yolo),
                }
            )

    async def _handle_permission_response(
        self, raw_session_id: str, payload: Mapping[str, Any]
    ) -> AppResponse:
        try:
            session_id = int(raw_session_id)
        except ValueError:
            return _json_response({"error": "invalid session id"}, 400)
        try:
            action = PermissionAction(str(payload.get("action", "")).lower())
        except ValueError:
            return _json_response({"error": "invalid permission action"}, 400)
        try:
            resolved = await self.resolve_permission(session_id, action)
        except (OSError, TypeError, ValueError) as error:
            LOGGER.warning(
                "permission.persist_failed session_id=%s action=%s error=%s",
                session_id,
                action.value,
                error,
            )
            return _json_response({"error": f"failed to persist permission: {error}"}, 503)
        if not resolved:
            return _json_response({"error": "permission not found"}, 404)
        return _json_response({"ok": True})

    async def _handle_event(self, payload: Mapping[str, Any]) -> AppResponse:
        hook = HookEvent.from_dict(payload)
        LOGGER.info(
            "hook.received type=%s session_id=%s source=%s tty=%s",
            hook.event_type,
            hook.session_id,
            hook.source,
            hook.session_tty,
        )
        is_permission = hook.event_type in {
            "permission",
            "permission_request",
            "PreToolUse",
            "PermissionRequest",
        }
        if is_permission:
            async with self.state.lock:
                if self.state.permission_queue.is_always_allowed(hook.tool_name, hook.tool_input):
                    LOGGER.info(
                        "hook.permission always_allow session_id=%s tool=%s",
                        hook.session_id,
                        hook.tool_name,
                    )
                    return _json_response(self._permission_body("allow"))
                decision = evaluate_yolo(self.state.yolo, hook.tool_name, hook.tool_input)
            if decision is YoloDecision.AUTO_ALLOW:
                LOGGER.info(
                    "hook.permission yolo_allow session_id=%s tool=%s",
                    hook.session_id,
                    hook.tool_name,
                )
                if self.state.config.yolo.auto_allow_log:
                    self._log(f"YOLO allow {hook.tool_name}({hook.tool_input})")
                if self.state.config.yolo.notify_auto_allow:
                    await self._notify_yolo_auto_allow(hook)
                return _json_response(self._permission_body("allow"))
            if decision is YoloDecision.AUTO_DENY:
                LOGGER.info(
                    "hook.permission yolo_deny session_id=%s tool=%s",
                    hook.session_id,
                    hook.tool_name,
                )
                self._log(f"YOLO deny {hook.tool_name}({hook.tool_input})")
                return _json_response(self._permission_body("deny"))
            event: SessionEvent | None = SessionEvent(
                SessionEventKind.PERMISSION_REQUEST,
                hook.session_id,
                tool_name=hook.tool_name,
                tool_input=hook.tool_input,
            )
        else:
            event = parse_hook_event(hook)
            if event is None:
                LOGGER.warning(
                    "hook.unrecognized type=%s session_id=%s",
                    hook.event_type,
                    hook.session_id,
                )
                return _json_response({"error": "unknown event type"}, 400)
        assert event is not None
        LOGGER.info(
            "hook.parsed event=%s session_id=%s",
            event.kind.value,
            event.session_id,
        )
        numeric_id = await self.process_session_event(event, hook, sync_device=not is_permission)
        if not is_permission:
            return _json_response({})
        if numeric_id is None:
            return _json_response(self._permission_body("deny"))
        loop = asyncio.get_running_loop()
        future: asyncio.Future[PermissionAction] = loop.create_future()
        async with self.state.lock:
            previous = self.state.permission_responses.get(numeric_id)
            if previous is not None and not previous.done():
                LOGGER.warning(
                    "permission.duplicate numeric_id=%s hook_id=%s",
                    numeric_id,
                    hook.session_id,
                )
                return _json_response(self._permission_body("deny"))
            self.state.permission_responses[numeric_id] = future
        await self.sync_device()
        try:
            action = await asyncio.wait_for(future, timeout=self.state.permission_timeout)
            behavior = "deny" if action is PermissionAction.DENY else "allow"
            return _json_response(self._permission_body(behavior))
        except TimeoutError:
            await self.resolve_permission(numeric_id, PermissionAction.DENY)
            return _json_response(self._permission_body("deny"))
        except asyncio.CancelledError:
            await asyncio.shield(self.resolve_permission(numeric_id, PermissionAction.DENY))
            raise
        finally:
            async with self.state.lock:
                if self.state.permission_responses.get(numeric_id) is future:
                    self.state.permission_responses.pop(numeric_id, None)

    @staticmethod
    def _permission_body(behavior: str) -> dict[str, Any]:
        return {"hookSpecificOutput": {"decision": {"behavior": behavior}}}

    async def _notify_yolo_auto_allow(self, hook: HookEvent) -> None:
        async with self.state.lock:
            numeric_id = self.state.session_id_map.get(hook.session_id)
            if numeric_id is None:
                numeric_id = self.state.store.allocate_id()
                self.state.session_id_map[hook.session_id] = numeric_id
                name = Path(hook.cwd).name if hook.cwd else f"S-{hook.session_id[:8]}"
                self.state.store.update(
                    DaemonSession(
                        SessionInfo(
                            id=numeric_id,
                            name=name,
                            status=SessionStatus.TOOL_USE,
                            source=hook.source,
                            cwd=hook.cwd,
                            bundle_id=hook.bundle_id,
                            session_tty=hook.session_tty,
                            started_at=int(time.time()),
                            last_activity=int(time.time()),
                        )
                    )
                )
            session = self.state.store.get(numeric_id)
            assert session is not None
            self.state.notification_queue.push(
                numeric_id,
                session.name,
                SessionStatus.TOOL_USE,
                f"YOLO allowed {hook.tool_name}({hook.tool_input})",
            )
        await self.sync_device()

    async def process_session_event(
        self,
        event: SessionEvent,
        hook: HookEvent | None = None,
        *,
        sync_device: bool = True,
    ) -> int | None:
        permission_added = False
        async with self.state.lock:
            numeric_id = self.state.session_id_map.get(event.session_id)
            if event.kind is SessionEventKind.STARTED:
                if numeric_id is None:
                    numeric_id = self.state.store.allocate_id()
                    self.state.session_id_map[event.session_id] = numeric_id
                existing = self.state.store.get(numeric_id)
                info = existing.info if existing else SessionInfo()
                info.id = numeric_id
                info.name = event.name
                info.status = SessionStatus.IDLE
                info.source = event.source
                info.cwd = event.cwd
                info.bundle_id = event.bundle_id
                info.session_tty = event.session_tty
                info.started_at = info.started_at or int(time.time())
                info.last_activity = int(time.time())
                self.state.store.update(
                    DaemonSession(info, existing.window_info if existing else None)
                )
                self.state.active_session_id = self.state.active_session_id or numeric_id
                LOGGER.info(
                    "session.started numeric_id=%s hook_id=%s name=%s source=%s",
                    numeric_id,
                    event.session_id,
                    info.name,
                    info.source,
                )
            elif event.kind is SessionEventKind.ENDED:
                if numeric_id is not None:
                    self.state.store.remove(numeric_id)
                    self.state.session_id_map.pop(event.session_id, None)
                    self.state.notification_queue.remove_by_session(numeric_id)
                    if self.state.active_session_id == numeric_id:
                        sessions = self.state.store.list()
                        self.state.active_session_id = sessions[0].id if sessions else None
                    LOGGER.info(
                        "session.ended numeric_id=%s hook_id=%s",
                        numeric_id,
                        event.session_id,
                    )
                else:
                    LOGGER.warning("session.end_unknown hook_id=%s", event.session_id)
            else:
                if numeric_id is None:
                    numeric_id = self.state.store.allocate_id()
                    self.state.session_id_map[event.session_id] = numeric_id
                    name = Path(hook.cwd).name if hook and hook.cwd else f"S-{event.session_id[:8]}"
                    self.state.store.update(
                        DaemonSession(
                            SessionInfo(
                                id=numeric_id,
                                name=name,
                                status=SessionStatus.IDLE,
                                source=hook.source if hook else "",
                                cwd=hook.cwd if hook else "",
                                bundle_id=hook.bundle_id if hook else "",
                                session_tty=hook.session_tty if hook else "",
                                started_at=int(time.time()),
                                last_activity=int(time.time()),
                            )
                        )
                    )
                session = self.state.store.get(numeric_id)
                assert session is not None
                if event.kind is SessionEventKind.STATUS_CHANGED and event.status is not None:
                    old_status = session.info.status
                    session.info.status = event.status
                    session.info.last_activity = int(time.time())
                    LOGGER.info(
                        "session.status numeric_id=%s hook_id=%s old=%s new=%s",
                        numeric_id,
                        event.session_id,
                        old_status.value,
                        event.status.value,
                    )
                    if (
                        event.status in {SessionStatus.DONE, SessionStatus.ERROR}
                        and old_status is not event.status
                    ):
                        description = (
                            "Task complete"
                            if event.status is SessionStatus.DONE
                            else (hook.error if hook else "Error")
                        )
                        self.state.notification_queue.push(
                            numeric_id, session.name, event.status, description
                        )
                elif event.kind is SessionEventKind.PERMISSION_REQUEST:
                    session.info.status = SessionStatus.PERMISSION_NEEDED
                    session.info.has_permission_request = True
                    session.info.last_activity = int(time.time())
                    if not self.state.permission_queue.pending_for_session(numeric_id):
                        permission_added = self.state.permission_queue.push(
                            PendingPermission(numeric_id, event.tool_name, event.tool_input)
                        )
                        if permission_added:
                            self.state.notification_queue.push(
                                numeric_id,
                                session.name,
                                SessionStatus.PERMISSION_NEEDED,
                                f"{event.tool_name}({event.tool_input})",
                            )
                    self.state.active_session_id = numeric_id
                    LOGGER.info(
                        "permission.queued numeric_id=%s hook_id=%s tool=%s",
                        numeric_id,
                        event.session_id,
                        event.tool_name,
                    )
        effects: list[DownlinkMessage] = []
        if (
            numeric_id is not None
            and event.kind is SessionEventKind.STATUS_CHANGED
            and event.status is not None
        ):
            if event.status is SessionStatus.DONE:
                effects.extend(
                    (
                        PlaySound(SoundType.SESSION_COMPLETE),
                        SetKnobRing(LedColor(0, 100, 255)),
                    )
                )
            elif event.status is SessionStatus.ERROR:
                effects.extend(
                    (
                        PlaySound(SoundType.ERROR),
                        SetLed(ButtonId.SESSION, LedColor.RED, True),
                    )
                )
        elif permission_added:
            effects.extend(
                (
                    PlaySound(SoundType.PERMISSION_ALERT),
                    SetLed(ButtonId.SEND, LedColor.AMBER, True),
                )
            )
        if sync_device:
            async with self._device_sync_lock:
                await self._sync_device()
                for message in effects:
                    await self._send_downlink_unlocked(message)
        elif effects:
            await self._send_downlinks(*effects)
        return numeric_id

    async def resolve_permission(self, session_id: int, action: PermissionAction) -> bool:
        async with self.state.lock:
            permission = next(
                (
                    item
                    for item in self.state.permission_queue.pending_list
                    if item.session_id == session_id
                ),
                None,
            )
            if permission is None:
                return False
            if action is PermissionAction.ALWAYS:
                pattern = f"{permission.tool_name}({permission.tool_input})"
                candidate = deepcopy(self.state.config)
                candidate.always_allow.patterns = list(
                    dict.fromkeys(
                        (*self.state.permission_queue.always_allow_list, pattern)
                    )
                )
                await asyncio.to_thread(save_config, self._config_path(), candidate)

            resolved = self.state.permission_queue.resolve(session_id, action)
            assert resolved is permission
            self.state.notification_queue.remove_permission_by_session(session_id)
            session = self.state.store.get(session_id)
            if session:
                session.info.has_permission_request = False
                session.info.status = (
                    SessionStatus.THINKING
                    if action is not PermissionAction.DENY
                    else SessionStatus.IDLE
                )
            if action is PermissionAction.ALWAYS:
                self.state.config.always_allow.patterns = list(
                    self.state.permission_queue.always_allow_list
                )
            future = self.state.permission_responses.get(session_id)
            if future and not future.done():
                future.set_result(action)
        effects: list[DownlinkMessage] = [DismissPermission(session_id)]
        if not self.state.permission_queue:
            effects.append(SetLed(ButtonId.SEND, LedColor.OFF, False))
        async with self._device_sync_lock:
            for message in effects:
                await self._send_downlink_unlocked(message)
            await self._sync_device()
        return True

    async def handle_uplink(self, message: UplinkMessage) -> None:
        if isinstance(message, UplinkButtonPress):
            action = (
                "down"
                if message.button in {ButtonId.DELETE, ButtonId.VOICE}
                else "click"
            )
            await self._handle_button({"id": message.button.value, "action": action})
        elif isinstance(message, UplinkButtonRelease):
            await self._handle_button({"id": message.button.value, "action": "up"})
        elif isinstance(message, UplinkKnobRotate):
            action = "cw" if message.direction is Direction.CLOCKWISE else "ccw"
            await self._handle_knob({"action": action, "steps": message.steps})
        elif isinstance(message, UplinkKnobPress):
            await self._handle_knob({"action": "press", "steps": 1})
        elif isinstance(message, UplinkKnobRelease):
            return
        elif isinstance(message, UplinkPermissionResponse):
            await self.resolve_permission(message.session_id, message.action)
        elif isinstance(message, SessionSwitch):
            self.state.active_session_id = message.session_id
            await self._handle_focus(str(message.session_id))
        elif isinstance(message, SetupActionRequest):
            port = message.daemon_port or self.state.config.general.hook_port
            success = True
            try:
                if message.command == "install_hook":
                    await SetupManager.install_hook(message.tool, port)
                elif message.command == "uninstall_hook":
                    await SetupManager.uninstall_hook(message.tool)
                elif message.command == "install_tool":
                    await SetupManager.brew_install(message.tool)
                elif message.command == "uninstall_tool":
                    await SetupManager.brew_uninstall(message.tool)
                else:
                    success = False
            except Exception:
                success = False
            await self._send_downlinks(SetupActionResult(message.request_id, success))
        elif isinstance(message, SetupStatusRequest):
            port = message.daemon_port or self.state.config.general.hook_port
            status = await SetupManager.detect_all(port, self.state.transport is not None)
            tools = [
                SetupToolStatus(
                    id=item.id,
                    name=item.name,
                    detected=item.detected,
                    hook_installed=item.hook_installed,
                    detail=item.detail,
                )
                for item in status.ai_tools
            ]
            await self._send_downlinks(SetupStatusUpdate(message.request_id, tools))
        elif isinstance(message, TimeSyncRequest):
            await self._send_downlinks(_current_time_sync())
        elif isinstance(message, YoloConfigRequest):
            await self._send_downlinks(_yolo_config_update(self.state.config.yolo))
        elif isinstance(message, YoloConfigSet):
            async with self.state.lock:
                config = self.state.config.yolo
                previous = (
                    config.active,
                    config.notify_auto_allow,
                    config.allow,
                    config.deny,
                )
                config.active = message.active
                config.notify_auto_allow = message.notify_auto_allow
                config.allow = list(message.allow)
                config.deny = list(message.deny)
                try:
                    await asyncio.to_thread(
                        save_config, self._config_path(), self.state.config
                    )
                except (OSError, TypeError, ValueError) as error:
                    (
                        config.active,
                        config.notify_auto_allow,
                        config.allow,
                        config.deny,
                    ) = previous
                    LOGGER.warning("yolo.update rejected error=%s", error)
            await self._send_downlinks(_yolo_config_update(self.state.config.yolo))

    async def _handle_button(self, payload: Mapping[str, Any]) -> AppResponse:
        try:
            button = ButtonId(str(payload.get("id", "")).lower())
        except ValueError:
            return _json_response({"error": "invalid button"}, 400)
        action_name = str(payload.get("action", "click"))
        if action_name not in {"click", "down", "up", "toggle"}:
            return _json_response({"error": "invalid action"}, 400)
        pending = self.state.permission_queue.current
        if pending and action_name in {"click", "toggle"}:
            if button is ButtonId.SEND:
                await self.resolve_permission(pending.session_id, PermissionAction.ALLOW)
                return _json_response({"ok": True})
            if button is ButtonId.CANCEL:
                await self.resolve_permission(pending.session_id, PermissionAction.DENY)
                return _json_response({"ok": True})
        if button is ButtonId.MODE and action_name in {"click", "toggle"}:
            self.state.config.yolo.active = not self.state.config.yolo.active
            await asyncio.to_thread(save_config, self._config_path(), self.state.config)
        elif button is ButtonId.SESSION and action_name in {"click", "toggle"}:
            await self._cycle_session(1)
        elif button in {ButtonId.SEND, ButtonId.CANCEL} and action_name in {"click", "toggle"}:
            key_action = "enter" if button is ButtonId.SEND else "escape"
            try:
                if self.state.active_session_id is not None:
                    async with self.state.lock:
                        session = self.state.store.get(self.state.active_session_id)
                    if session is not None:
                        with suppress(FocusError):
                            await asyncio.to_thread(activate_window, session)
                await asyncio.to_thread(self.state.injector.send_key, key_action)
            except Exception as error:
                self._log(f"{button.value} failed: {error}")
                return _json_response({"error": str(error)}, 503)
        elif button in {ButtonId.DELETE, ButtonId.VOICE}:
            macro = (
                self.state.config.macros.delete
                if button is ButtonId.DELETE
                else self.state.config.macros.voice
            )
            marker = f"{button.value}:{macro}"
            try:
                if (
                    button is ButtonId.VOICE
                    and action_name in {"click", "down", "toggle"}
                    and self.state.active_session_id is not None
                ):
                    async with self.state.lock:
                        session = self.state.store.get(self.state.active_session_id)
                    if session is not None:
                        with suppress(FocusError):
                            await asyncio.to_thread(activate_window, session)
                if action_name == "down":
                    if marker in self.state.held_keys:
                        return _json_response({"ok": True})
                    await asyncio.to_thread(self.state.injector.send_key_down, macro)
                    self.state.held_keys.add(marker)
                elif action_name == "up":
                    if marker not in self.state.held_keys:
                        return _json_response({"ok": True})
                    await asyncio.to_thread(self.state.injector.send_key_up, macro)
                    self.state.held_keys.discard(marker)
                else:
                    await asyncio.to_thread(self.state.injector.send_key, macro)
            except Exception as error:
                self._log(f"{button.value} macro failed: {error}")
                LOGGER.warning(
                    "input.button failed button=%s action=%s macro=%s error=%s",
                    button.value,
                    action_name,
                    macro,
                    error,
                )
                return _json_response({"error": str(error)}, 503)
            LOGGER.info(
                "input.button handled button=%s action=%s macro=%s",
                button.value,
                action_name,
                macro,
            )
        self._log(f"button {button.value} {action_name}")
        await self.sync_device()
        return _json_response({"ok": True})

    async def _handle_knob(self, payload: Mapping[str, Any]) -> AppResponse:
        action = str(payload.get("action", ""))
        try:
            steps = int(payload.get("steps", 1))
        except (TypeError, ValueError):
            return _json_response({"error": "invalid steps"}, 400)
        if not 1 <= steps <= 255:
            return _json_response({"error": "invalid steps"}, 400)
        if action == "cw":
            await self._cycle_session(steps)
        elif action == "ccw":
            await self._cycle_session(-steps)
        elif action == "press":
            if self.state.active_session_id is not None:
                return await self._handle_focus(str(self.state.active_session_id))
        else:
            return _json_response({"error": "invalid knob action"}, 400)
        return _json_response({"ok": True})

    async def _cycle_session(self, steps: int) -> None:
        async with self.state.lock:
            sessions = self.state.store.list()
            if not sessions:
                return
            ids = [session.id for session in sessions]
            try:
                index = (
                    ids.index(self.state.active_session_id)
                    if self.state.active_session_id is not None
                    else 0
                )
            except ValueError:
                index = 0
            self.state.active_session_id = ids[(index + steps) % len(ids)]
        await self.sync_device()

    async def _handle_focus(self, raw_session_id: str) -> AppResponse:
        try:
            session_id = int(raw_session_id)
        except ValueError:
            return _json_response({"error": "invalid session id"}, 400)
        async with self.state.lock:
            session = self.state.store.get(session_id)
        if session is None:
            return _json_response({"error": "session not found"}, 404)
        try:
            strategy = await asyncio.to_thread(activate_window, session)
        except FocusError as error:
            return _json_response({"error": str(error)}, 503)
        self.state.active_session_id = session_id
        return _json_response({"ok": True, "strategy": strategy})

    async def _handle_config(self, payload: Mapping[str, Any]) -> AppResponse:
        key = str(payload.get("key", ""))
        value = str(payload.get("value", ""))
        async with self.state.lock:
            previous = deepcopy(self.state.config)
            try:
                set_config_value(self.state.config, key, value)
                await asyncio.to_thread(
                    save_config, self._config_path(), self.state.config
                )
            except (KeyError, TypeError, ValueError, OSError) as error:
                self.state.config = previous
                return _json_response({"error": str(error)}, 400)
        if key == "sound.volume":
            self.state.speaker.set_volume(self.state.config.sound.volume)
        elif key == "sound.muted":
            self.state.speaker.set_muted(self.state.config.sound.muted)
        if key.startswith("yolo."):
            await self.sync_device()
        return _json_response({"ok": True})

    async def _setup_action(self, action: str, item: str) -> AppResponse:
        try:
            if action == "install":
                await SetupManager.install_hook(item, self.state.config.general.hook_port)
            elif action == "uninstall":
                await SetupManager.uninstall_hook(item)
            elif action == "brew-install":
                await SetupManager.brew_install(item)
            else:
                await SetupManager.brew_uninstall(item)
        except Exception as error:
            return _json_response({"error": str(error)}, 400)
        return _json_response({"ok": True})

    def _custom_sounds_dir(self) -> Path:
        return self._config_path().parent / "sounds"

    def _list_sounds(self) -> dict[str, Any]:
        custom_dir = self._custom_sounds_dir()
        custom = [path.stem for path in custom_dir.glob("*.wav")] if custom_dir.is_dir() else []
        return {
            "builtin": ["builtin:alert", "builtin:ding", "builtin:buzz", "builtin:click"],
            "custom": [f"custom:{name}" for name in sorted(custom)],
        }

    async def _play_sound(self, payload: Mapping[str, Any]) -> AppResponse:
        sound_id = str(payload.get("id", payload.get("sound_id", "")))
        try:
            if sound_id.startswith("custom:"):
                name = self._safe_sound_name(sound_id.removeprefix("custom:"))
                await asyncio.to_thread(
                    self.state.speaker.play_file, self._custom_sounds_dir() / f"{name}.wav"
                )
            else:
                await asyncio.to_thread(self.state.speaker.play_by_id, sound_id)
        except Exception as error:
            return _json_response({"error": str(error)}, 400)
        return _json_response({"ok": True})

    def _upload_sound(
        self, headers: Mapping[str, str], body: bytes, query: Mapping[str, list[str]]
    ) -> AppResponse:
        filename = headers.get("x-filename", "") or (query.get("name", [""])[0])
        content = body
        content_type = headers.get("content-type", "")
        if content_type.startswith("multipart/form-data"):
            message = BytesParser(policy=email_policy).parsebytes(
                f"Content-Type: {content_type}\r\nMIME-Version: 1.0\r\n\r\n".encode() + body
            )
            part = next(message.iter_attachments(), None)
            if part is None:
                return _json_response({"error": "missing sound file"}, 400)
            filename = part.get_filename() or filename
            payload = part.get_payload(decode=True)
            content = payload if isinstance(payload, bytes) else b""
        if (
            not (44 <= len(content) <= 5 * 1024 * 1024)
            or content[:4] != b"RIFF"
            or content[8:12] != b"WAVE"
        ):
            return _json_response({"error": "file must be a valid WAV under 5 MiB"}, 400)
        try:
            name = self._safe_sound_name(Path(filename).stem)
        except ValueError as error:
            return _json_response({"error": str(error)}, 400)
        target = self._custom_sounds_dir() / f"{name}.wav"
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = target.with_suffix(".tmp")
        temporary.write_bytes(content)
        temporary.replace(target)
        return _json_response({"id": f"custom:{name}"}, 201)

    @staticmethod
    def _safe_sound_name(value: str) -> str:
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,63}", value):
            raise ValueError("invalid sound name")
        return value

    def _log(self, message: str) -> None:
        self.state.activity_log.append(f"[{datetime.now().strftime('%H:%M:%S')}] {message}")

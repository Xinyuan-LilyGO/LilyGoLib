from __future__ import annotations

import asyncio
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from vibe_keyboard.core import ButtonId, PermissionAction, SessionInfo, SessionStatus
from vibe_keyboard.daemon.config import DaemonConfig, load_config
from vibe_keyboard.daemon.discovery import DiscoveredSession
from vibe_keyboard.daemon.logging import LOGGER_NAME
from vibe_keyboard.daemon.permissions import PendingPermission
from vibe_keyboard.daemon.scanner import run_transcript_scanner
from vibe_keyboard.daemon.server import DaemonApplication, DaemonState, HookServer
from vibe_keyboard.daemon.session import DaemonSession
from vibe_keyboard.daemon.setup import AiToolStatus, SetupStatus, SystemStatus
from vibe_keyboard.protocol import (
    ButtonPress,
    ButtonRelease,
    DismissPermission,
    NotificationListUpdate,
    PermissionRequest,
    SessionListClear,
    SessionListUpdate,
    SessionRemove,
    SessionUpsert,
    SetupStatusRequest,
    SetupStatusUpdate,
    SetupToolStatus,
    TimeSync,
    TimeSyncRequest,
    YoloConfigRequest,
    YoloConfigSet,
    YoloConfigUpdate,
    encode_downlink,
)
from vibe_keyboard.transport import TransportError


class RecordingInjector:
    platform = "test"

    def __init__(self) -> None:
        self.events: list[tuple[str, str]] = []

    def send_key(self, action: str) -> None:
        self.events.append(("click", action))

    def send_key_down(self, action: str) -> None:
        self.events.append(("down", action))

    def send_key_up(self, action: str) -> None:
        self.events.append(("up", action))


class RecordingTransport:
    connected = True

    def __init__(self) -> None:
        self.downlinks: list[object] = []

    async def send_downlink(self, message: object) -> None:
        self.downlinks.append(message)


class BleTransport(RecordingTransport):
    def __init__(self) -> None:
        super().__init__()
        self.fail_message_type: type[object] | None = None
        self.block_message_type: type[object] | None = None
        self.write_started = asyncio.Event()
        self.allow_write = asyncio.Event()

    async def send_downlink(self, message: object) -> None:
        if self.block_message_type is not None and isinstance(
            message, self.block_message_type
        ):
            self.block_message_type = None
            self.write_started.set()
            await self.allow_write.wait()
        if self.fail_message_type is not None and isinstance(message, self.fail_message_type):
            self.fail_message_type = None
            raise TransportError("temporary write backpressure")
        await super().send_downlink(message)


class StaticDiscovery:
    name = "static"

    def __init__(self, sessions: list[DiscoveredSession]) -> None:
        self._sessions = sessions

    def discover(self) -> list[DiscoveredSession]:
        return self._sessions


class ApplicationTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self) -> None:
        self.state = DaemonState(config=DaemonConfig(), injector=RecordingInjector())
        self.app = DaemonApplication(self.state)

    async def dispatch(self, method: str, path: str, value: object | None = None):
        body = b"" if value is None else json.dumps(value).encode()
        return await self.app.dispatch(method, path, {"content-type": "application/json"}, body)

    async def json(self, method: str, path: str, value: object | None = None):
        response = await self.dispatch(method, path, value)
        return response, json.loads(response.body or b"{}")

    async def start_session(self, session_id: str = "abc") -> None:
        response, _ = await self.json(
            "POST",
            "/event",
            {"type": "SessionStart", "session_id": session_id, "name": "Agent", "source": "test"},
        )
        self.assertEqual(response.status, 200)

    async def test_health_root_and_unknown_route(self) -> None:
        response, body = await self.json("GET", "/health")
        self.assertEqual(response.status, 200)
        self.assertEqual(body, {"status": "ok"})
        response, body = await self.json("GET", "/")
        self.assertEqual(response.status, 404)
        self.assertEqual(body, {"error": "not found"})
        response, _ = await self.json("GET", "/missing")
        self.assertEqual(response.status, 404)

    async def test_device_state_returns_complete_daemon_snapshot(self) -> None:
        self.state.active_session_id = 7
        self.state.transport = BleTransport()
        self.state.store.update(
            DaemonSession(
                SessionInfo(
                    id=7,
                    name="Codex task",
                    status=SessionStatus.THINKING,
                    has_permission_request=True,
                    source="codex",
                    cwd="/tmp/project",
                    permission_mode="default",
                    model="gpt-5.5",
                    tokens_in=120,
                    tokens_out=30,
                    cost_usd=0.25,
                    context_pct=15,
                    last_message="Implement the daemon API",
                    last_ai_output="Working",
                    bundle_id="com.example.Terminal",
                    session_tty="/dev/ttys001",
                    started_at=1_700_000_000,
                    last_activity=1_700_000_100,
                )
            )
        )
        with patch("vibe_keyboard.daemon.notifications.time.time", return_value=1_700_000_200):
            self.state.notification_queue.push(
                7, "Codex task", SessionStatus.DONE, "Implementation complete"
            )
        self.state.permission_queue.push(PendingPermission(7, "Write", "src/app.py"))
        self.state.held_keys.update({"voice:dictation", "delete:ctrl_u"})
        self.state.config.yolo.active = True
        self.state.config.yolo.allow = ["Read(src/*)"]
        self.state.config.yolo.deny = ["Bash(git push*)"]
        self.state.config.yolo.notify_auto_allow = False
        self.state.config.yolo.auto_allow_log = False

        response, body = await self.json("GET", "/device/state")

        self.assertEqual(response.status, 200)
        self.assertEqual(
            body,
            {
                "active_session_id": 7,
                "ble_connected": True,
                "sessions": [
                    {
                        "id": 7,
                        "name": "Codex task",
                        "status": "thinking",
                        "has_permission": True,
                        "source": "codex",
                        "cwd": "/tmp/project",
                        "permission_mode": "default",
                        "model": "gpt-5.5",
                        "tokens_in": 120,
                        "tokens_out": 30,
                        "cost_usd": 0.25,
                        "context_pct": 15,
                        "last_message": "Implement the daemon API",
                        "last_ai_output": "Working",
                        "bundle_id": "com.example.Terminal",
                        "session_tty": "/dev/ttys001",
                        "started_at": 1_700_000_000,
                        "last_activity": 1_700_000_100,
                    }
                ],
                "notifications": [
                    {
                        "id": 1,
                        "session_id": 7,
                        "session_name": "Codex task",
                        "status": "done",
                        "description": "Implementation complete",
                        "timestamp": 1_700_000_200,
                        "read": False,
                    }
                ],
                "pending_permissions": [
                    {"session_id": 7, "tool_name": "Write", "tool_input": "src/app.py"}
                ],
                "held_keys": ["delete:ctrl_u", "voice:dictation"],
                "yolo": {
                    "active": True,
                    "allow": ["Read(src/*)"],
                    "deny": ["Bash(git push*)"],
                    "notify_auto_allow": False,
                    "auto_allow_log": False,
                },
            },
        )

    async def test_session_lifecycle_and_explicit_dto(self) -> None:
        await self.start_session()
        await self.json(
            "POST", "/event", {"type": "status", "session_id": "abc", "status": "thinking"}
        )
        response, sessions = await self.json("GET", "/sessions")
        self.assertEqual(response.status, 200)
        self.assertEqual(sessions[0]["status"], "thinking")
        self.assertIn("has_permission", sessions[0])
        self.assertNotIn("has_permission_request", sessions[0])
        await self.json("POST", "/event", {"type": "SessionEnd", "session_id": "abc"})
        _, sessions = await self.json("GET", "/sessions")
        self.assertEqual(sessions, [])

    async def test_session_end_emits_daemon_logs(self) -> None:
        await self.start_session()
        with self.assertLogs(f"{LOGGER_NAME}.server", level="INFO") as captured:
            await self.json("POST", "/event", {"type": "SessionEnd", "session_id": "abc"})
        output = "\n".join(captured.output)
        self.assertIn("hook.received type=SessionEnd session_id=abc", output)
        self.assertIn("hook.parsed event=ended session_id=abc", output)
        self.assertIn("session.ended numeric_id=1 hook_id=abc", output)
        self.assertIn("device.sync skipped reason=no_transport sessions=0", output)

    async def test_transport_sync_emits_daemon_log(self) -> None:
        transport = RecordingTransport()
        with self.assertLogs(f"{LOGGER_NAME}.server", level="INFO") as captured:
            await self.app.set_transport(transport)
        output = "\n".join(captured.output)
        self.assertIn(
            "device.transport connected transport=RecordingTransport connected=True", output
        )
        self.assertIn("device.sync transport=RecordingTransport sessions=0", output)
        self.assertTrue(transport.downlinks)

    async def test_codex_discovery_syncs_session_list_to_transport(self) -> None:
        transport = RecordingTransport()
        await self.app.set_transport(transport)
        stop = asyncio.Event()
        with tempfile.TemporaryDirectory() as directory:
            transcript = Path(directory) / "codex.jsonl"
            transcript.write_text(
                json.dumps({"type": "session_meta", "payload": {"session_id": "codex-1"}})
                + "\n"
                + json.dumps({"type": "turn_context", "payload": {"model": "gpt-5.5"}})
                + "\n"
                + json.dumps(
                    {
                        "type": "event_msg",
                        "payload": {
                            "type": "user_message",
                            "message": "render the prompt on ESP32",
                        },
                    }
                )
                + "\n"
                + json.dumps(
                    {
                        "type": "event_msg",
                        "payload": {
                            "type": "token_count",
                            "info": {
                                "total_token_usage": {"input_tokens": 20000, "output_tokens": 700},
                                "model_context_window": 100000,
                            },
                        },
                    }
                )
                + "\n",
                encoding="utf-8",
            )
            discovery = StaticDiscovery(
                [DiscoveredSession("codex-1", "Codex Task", transcript, "/tmp/project", "codex")]
            )
            task = asyncio.create_task(run_transcript_scanner(self.app, stop, discovery=discovery))
            try:
                for _ in range(100):
                    updates = [
                        item for item in transport.downlinks if isinstance(item, SessionListUpdate)
                    ]
                    if updates and updates[-1].sessions and updates[-1].sessions[0].model:
                        break
                    await asyncio.sleep(0)
                self.assertTrue(updates)
                self.assertEqual(updates[-1].sessions[0].name, "Codex Task")
                self.assertEqual(updates[-1].sessions[0].source, "codex")
                self.assertEqual(updates[-1].sessions[0].model, "gpt-5.5")
                self.assertEqual(updates[-1].sessions[0].last_message, "render the prompt on ESP32")
                self.assertEqual(updates[-1].sessions[0].tokens_in, 20000)
                self.assertEqual(updates[-1].sessions[0].tokens_out, 700)
                self.assertEqual(updates[-1].sessions[0].context_pct, 20)
            finally:
                stop.set()
                await task

    async def test_ble_session_sync_sends_incremental_packets_under_firmware_limit(self) -> None:
        transport = BleTransport()
        for index in range(40):
            self.state.store.update(
                DaemonSession(
                    SessionInfo(
                        id=index + 1,
                        name=f"Very long session name {index} " + ("名字" * 20),
                        source="external-simulator-" + ("来源" * 40),
                        cwd="/Users/example/projects/" + ("deep/" * 30),
                        model="claude-opus-4-6-with-extra-long-suffix",
                        last_message="prompt " + ("内容" * 200),
                        last_ai_output="output " + ("内容" * 200),
                    )
                )
            )
        self.state.active_session_id = 40
        await self.app.set_transport(transport)

        clear_messages = [
            item for item in transport.downlinks if isinstance(item, SessionListClear)
        ]
        upserts = [item for item in transport.downlinks if isinstance(item, SessionUpsert)]
        self.assertEqual(clear_messages, [SessionListClear(40)])
        self.assertEqual(len(upserts), 40)
        self.assertTrue(upserts[-1].active)
        self.assertLessEqual(max(len(encode_downlink(item)) for item in upserts), 500)
        self.assertLessEqual(len(upserts[-1].session.name.encode()), 35)
        self.assertLessEqual(len(upserts[-1].session.source.encode()), 23)
        self.assertLessEqual(len(upserts[-1].session.last_message.encode()), 128)
        self.assertTrue(upserts[-1].session.last_message.startswith("prompt "))
        self.assertEqual(upserts[-1].session.id, 40)

        start = len(transport.downlinks)
        self.state.store.update(DaemonSession(SessionInfo(id=2, name="Changed", source="codex")))
        await self.app.sync_device()
        delta = transport.downlinks[start:]
        changed = [item for item in delta if isinstance(item, SessionUpsert)]
        self.assertEqual([item.session.id for item in changed], [2])
        self.assertFalse(any(isinstance(item, SessionListClear) for item in delta))

        start = len(transport.downlinks)
        self.state.store.remove(3)
        await self.app.sync_device()
        delta = transport.downlinks[start:]
        self.assertEqual(
            [item for item in delta if isinstance(item, SessionRemove)],
            [SessionRemove(3)],
        )
        self.assertFalse(any(isinstance(item, SessionListClear) for item in delta))

    async def test_ble_failed_session_update_does_not_commit_incremental_cache(self) -> None:
        transport = BleTransport()
        transport.fail_message_type = SessionUpsert
        self.state.store.update(
            DaemonSession(SessionInfo(id=1, name="Retry me", source="codex"))
        )

        await self.app.set_transport(transport)

        self.assertIs(self.state.transport, transport)
        start = len(transport.downlinks)
        await self.app.sync_device()
        retried = transport.downlinks[start:]
        self.assertTrue(any(isinstance(item, SessionListClear) for item in retried))
        self.assertTrue(any(isinstance(item, SessionUpsert) for item in retried))

    async def test_concurrent_ble_sync_keeps_newest_session_snapshot(self) -> None:
        transport = BleTransport()
        transport.block_message_type = SessionUpsert
        self.state.transport = transport
        self.state.store.update(
            DaemonSession(SessionInfo(id=1, name="Old name", source="codex"))
        )

        first = asyncio.create_task(self.app.sync_device())
        await transport.write_started.wait()
        self.state.store.update(
            DaemonSession(SessionInfo(id=1, name="New name", source="codex"))
        )
        second = asyncio.create_task(self.app.sync_device())
        await asyncio.sleep(0)
        self.assertFalse(second.done())

        transport.allow_write.set()
        await asyncio.gather(first, second)

        upserts = [item for item in transport.downlinks if isinstance(item, SessionUpsert)]
        self.assertEqual(upserts[-1].session.name, "New name")

    async def test_permission_resolution_cannot_be_overtaken_by_old_sync(self) -> None:
        transport = BleTransport()
        transport.block_message_type = PermissionRequest
        self.state.transport = transport
        self.state.permission_queue.push(PendingPermission(1, "Write", "src/app.py"))

        old_sync = asyncio.create_task(self.app.sync_device())
        await transport.write_started.wait()
        resolution = asyncio.create_task(
            self.app.resolve_permission(1, PermissionAction.ALLOW)
        )
        await asyncio.sleep(0)
        self.assertFalse(resolution.done())

        transport.allow_write.set()
        await old_sync
        self.assertTrue(await resolution)

        permission_messages = [
            item
            for item in transport.downlinks
            if isinstance(item, (PermissionRequest, DismissPermission))
        ]
        self.assertEqual(
            [type(item) for item in permission_messages],
            [PermissionRequest, DismissPermission],
        )

    async def test_ble_notification_sync_fits_firmware_packet_and_rows(self) -> None:
        transport = BleTransport()
        for index in range(6):
            self.state.notification_queue.push(
                index + 1,
                f"Long session {index} " + ("名字" * 30),
                SessionStatus.DONE,
                "result " + ("内容" * 200),
            )

        await self.app.set_transport(transport)

        updates = [item for item in transport.downlinks if isinstance(item, NotificationListUpdate)]
        self.assertTrue(updates)
        self.assertEqual(len(updates[-1].notifications), 2)
        self.assertLessEqual(len(encode_downlink(updates[-1])), 500)
        self.assertLessEqual(len(updates[-1].notifications[0].session_name.encode()), 35)
        self.assertLessEqual(len(updates[-1].notifications[0].description.encode()), 160)

    async def test_ble_notification_sync_includes_short_rows_beyond_two(self) -> None:
        transport = BleTransport()
        for index in range(6):
            self.state.notification_queue.push(
                index + 1,
                f"Session {index + 1}",
                SessionStatus.DONE,
                f"Notification {index + 1}",
            )

        await self.app.set_transport(transport)

        updates = [item for item in transport.downlinks if isinstance(item, NotificationListUpdate)]
        self.assertEqual(len(updates[-1].notifications), 6)
        self.assertLessEqual(len(encode_downlink(updates[-1])), 500)

    async def test_ble_notification_sync_caps_rows_at_32(self) -> None:
        transport = BleTransport()
        for index in range(40):
            self.state.notification_queue.push(
                index + 1,
                "Session",
                SessionStatus.DONE,
                "Notification",
            )

        with patch(
            "vibe_keyboard.daemon.server.app.BLE_NOTIFICATION_LIST_MAX_BYTES", 10_000
        ):
            await self.app.set_transport(transport)

        updates = [item for item in transport.downlinks if isinstance(item, NotificationListUpdate)]
        self.assertEqual(len(updates[-1].notifications), 32)

    async def test_ble_permission_request_fits_single_gatt_write(self) -> None:
        transport = BleTransport()
        self.state.permission_queue.push(
            PendingPermission(1, "Bash", '{"command":"' + ("内容" * 1_000) + '"}')
        )

        await self.app.set_transport(transport)

        requests = [item for item in transport.downlinks if isinstance(item, PermissionRequest)]
        self.assertEqual(len(requests), 1)
        self.assertLessEqual(len(encode_downlink(requests[0])), 500)
        self.assertTrue(requests[0].action_desc.startswith('Bash({"command":"'))
        requests[0].action_desc.encode("utf-8").decode("utf-8")

    async def test_notify_test_immediately_syncs_transport(self) -> None:
        transport = RecordingTransport()
        self.state.transport = transport

        response, body = await self.json("POST", "/notify/test")

        self.assertEqual(response.status, 200)
        self.assertTrue(body["ok"])
        updates = [item for item in transport.downlinks if isinstance(item, NotificationListUpdate)]
        self.assertEqual(len(updates), 1)
        self.assertEqual(updates[0].notifications[0].description, "Test notification")

    async def test_ble_connection_sends_time_sync_before_session_snapshot(self) -> None:
        transport = BleTransport()
        expected = TimeSync(1_700_000_000_123, 480)

        with patch("vibe_keyboard.daemon.server.app._current_time_sync", return_value=expected):
            await self.app.set_transport(transport)

        self.assertEqual(transport.downlinks[0], expected)
        self.assertIsInstance(transport.downlinks[1], SessionListClear)

    async def test_time_sync_request_returns_current_daemon_time(self) -> None:
        transport = RecordingTransport()
        self.state.transport = transport
        expected = TimeSync(1_700_000_000_123, -420)

        with patch("vibe_keyboard.daemon.server.app._current_time_sync", return_value=expected):
            await self.app.handle_uplink(TimeSyncRequest())

        self.assertEqual(transport.downlinks, [expected])

    async def test_setup_status_request_returns_ai_tool_hook_state(self) -> None:
        transport = RecordingTransport()
        self.state.transport = transport
        status = SetupStatus(
            ai_tools=[
                AiToolStatus("claude-code", "Claude Code", True, True),
                AiToolStatus("codex", "Codex", True, False, "notify events only"),
            ],
            recommended_tools=[],
            system=SystemStatus(True, True, True),
        )
        with patch("vibe_keyboard.daemon.server.app.SetupManager.detect_all", return_value=status):
            await self.app.handle_uplink(SetupStatusRequest(0x12345678, 19280))

        self.assertEqual(
            transport.downlinks,
            [
                SetupStatusUpdate(
                    0x12345678,
                    [
                        SetupToolStatus("claude-code", "Claude Code", True, True),
                        SetupToolStatus("codex", "Codex", True, False, "notify events only"),
                    ],
                )
            ],
        )

    async def test_yolo_allow_and_deny(self) -> None:
        self.state.config.yolo.active = True
        allow_response, allow = await self.json(
            "POST",
            "/event",
            {"type": "permission", "session_id": "a", "tool_name": "Read", "tool_input": "a.py"},
        )
        deny_response, deny = await self.json(
            "POST",
            "/event",
            {
                "type": "permission",
                "session_id": "a",
                "tool_name": "Bash",
                "tool_input": "sudo rm x",
            },
        )
        self.assertEqual(allow_response.status, 200)
        self.assertEqual(allow["hookSpecificOutput"]["decision"]["behavior"], "allow")
        self.assertEqual(deny_response.status, 200)
        self.assertEqual(deny["hookSpecificOutput"]["decision"]["behavior"], "deny")

    async def test_yolo_config_query_and_set_over_device_protocol(self) -> None:
        transport = RecordingTransport()
        await self.app.set_transport(transport)
        self.assertTrue(any(isinstance(item, YoloConfigUpdate) for item in transport.downlinks))
        transport.downlinks.clear()

        await self.app.handle_uplink(YoloConfigRequest())
        self.assertEqual(
            transport.downlinks,
            [
                YoloConfigUpdate(
                    False,
                    True,
                    ["Read(*)", "Glob(*)", "Grep(*)"],
                    ["Bash(git push*)", "Bash(rm -rf*)", "Bash(sudo*)"],
                )
            ],
        )

        transport.downlinks.clear()
        with patch("vibe_keyboard.daemon.server.app.save_config") as save:
            await self.app.handle_uplink(
                YoloConfigSet(True, False, ["Read(src/*)"], ["Bash(git push*)"])
            )
        self.assertTrue(self.state.config.yolo.active)
        self.assertFalse(self.state.config.yolo.notify_auto_allow)
        self.assertEqual(self.state.config.yolo.allow, ["Read(src/*)"])
        self.assertEqual(self.state.config.yolo.deny, ["Bash(git push*)"])
        save.assert_called_once()
        self.assertEqual(
            transport.downlinks,
            [YoloConfigUpdate(True, False, ["Read(src/*)"], ["Bash(git push*)"])],
        )

    async def test_oversized_device_yolo_config_is_rejected_without_mutation(self) -> None:
        transport = BleTransport()
        self.state.transport = transport
        original_allow = list(self.state.config.yolo.allow)
        original_deny = list(self.state.config.yolo.deny)
        oversized = [f"Read(path-{index}-{'x' * 48})" for index in range(10)]

        await self.app.handle_uplink(
            YoloConfigSet(True, False, oversized, original_deny)
        )

        self.assertFalse(self.state.config.yolo.active)
        self.assertEqual(self.state.config.yolo.allow, original_allow)
        self.assertEqual(self.state.config.yolo.deny, original_deny)
        self.assertIsInstance(transport.downlinks[-1], YoloConfigUpdate)
        self.assertLessEqual(len(encode_downlink(transport.downlinks[-1])), 500)

    async def test_yolo_auto_allow_notification_honors_switch(self) -> None:
        transport = BleTransport()
        await self.app.set_transport(transport)
        transport.downlinks.clear()
        self.state.config.yolo.active = True
        self.state.config.yolo.notify_auto_allow = True
        _, allowed = await self.json(
            "POST",
            "/event",
            {
                "type": "permission",
                "session_id": "notify-me",
                "tool_name": "Read",
                "tool_input": "a.py",
            },
        )
        self.assertEqual(allowed["hookSpecificOutput"]["decision"]["behavior"], "allow")
        self.assertEqual(self.state.notification_queue.unread_count, 1)
        self.assertIn("YOLO allowed Read(a.py)", self.state.notification_queue.unread()[0].description)
        updates = [item for item in transport.downlinks if isinstance(item, NotificationListUpdate)]
        self.assertTrue(updates)
        self.assertIn("YOLO allowed Read(a.py)", updates[-1].notifications[0].description)

        self.state.config.yolo.notify_auto_allow = False
        await self.json(
            "POST",
            "/event",
            {
                "type": "permission",
                "session_id": "silent",
                "tool_name": "Read",
                "tool_input": "b.py",
            },
        )
        self.assertEqual(self.state.notification_queue.unread_count, 1)

    async def test_permission_blocks_then_resolves(self) -> None:
        transport = BleTransport()
        self.state.transport = transport
        await self.start_session()
        task = asyncio.create_task(
            self.json(
                "POST",
                "/event",
                {
                    "type": "permission",
                    "session_id": "abc",
                    "tool_name": "Write",
                    "tool_input": "a.py",
                },
            )
        )
        for _ in range(100):
            if self.state.permission_queue.current and self.state.permission_responses:
                break
            await asyncio.sleep(0)
        session_id = self.state.permission_queue.current.session_id
        self.assertEqual(self.state.notification_queue.unread_count, 1)
        self.assertTrue(await self.app.resolve_permission(session_id, PermissionAction.ALLOW))
        self.assertEqual(self.state.notification_queue.unread_count, 0)
        updates = [
            item
            for item in transport.downlinks
            if isinstance(item, NotificationListUpdate)
        ]
        self.assertTrue(updates)
        self.assertEqual(updates[-1].notifications, ())
        _, body = await task
        self.assertEqual(body["hookSpecificOutput"]["decision"]["behavior"], "allow")

    async def test_codex_permission_request_blocks_then_resolves(self) -> None:
        await self.start_session()
        task = asyncio.create_task(
            self.json(
                "POST",
                "/event",
                {
                    "type": "PermissionRequest",
                    "session_id": "abc",
                    "tool_name": "Bash",
                    "tool_input": "{\"command\":\"git push\"}",
                },
            )
        )
        for _ in range(100):
            if self.state.permission_queue.current and self.state.permission_responses:
                break
            await asyncio.sleep(0)
        session_id = self.state.permission_queue.current.session_id
        self.assertTrue(await self.app.resolve_permission(session_id, PermissionAction.ALLOW))
        _, body = await task
        self.assertEqual(body["hookSpecificOutput"]["decision"]["behavior"], "allow")

    async def test_permission_timeout_is_deny(self) -> None:
        await self.start_session()
        self.state.permission_timeout = 0.01
        _, body = await self.json(
            "POST",
            "/event",
            {"type": "permission", "session_id": "abc", "tool_name": "Write", "tool_input": "a.py"},
        )
        self.assertEqual(body["hookSpecificOutput"]["decision"]["behavior"], "deny")
        self.assertFalse(self.state.permission_responses)

    async def test_duplicate_permission_for_session_fails_closed_without_replacing_first(
        self,
    ) -> None:
        await self.start_session()
        first = asyncio.create_task(
            self.json(
                "POST",
                "/event",
                {
                    "type": "PermissionRequest",
                    "session_id": "abc",
                    "tool_name": "Write",
                    "tool_input": "first.py",
                },
            )
        )
        for _ in range(100):
            if self.state.permission_responses:
                break
            await asyncio.sleep(0)
        numeric_id = self.state.session_id_map["abc"]
        original = self.state.permission_responses[numeric_id]

        _, duplicate = await self.json(
            "POST",
            "/event",
            {
                "type": "PermissionRequest",
                "session_id": "abc",
                "tool_name": "Bash",
                "tool_input": "second command",
            },
        )

        self.assertEqual(duplicate["hookSpecificOutput"]["decision"]["behavior"], "deny")
        self.assertIs(self.state.permission_responses[numeric_id], original)
        self.assertEqual(self.state.permission_queue.current.tool_input, "first.py")
        self.assertTrue(
            await self.app.resolve_permission(numeric_id, PermissionAction.ALLOW)
        )
        _, resolved = await first
        self.assertEqual(resolved["hookSpecificOutput"]["decision"]["behavior"], "allow")

    async def test_permission_http_route_accepts_all_actions(self) -> None:
        for session_id, action in enumerate(PermissionAction, start=20):
            with self.subTest(action=action.value):
                self.state.permission_queue.push(
                    PendingPermission(session_id, "Write", f"src/{action.value}.py")
                )
                future: asyncio.Future[PermissionAction] = (
                    asyncio.get_running_loop().create_future()
                )
                self.state.permission_responses[session_id] = future

                with patch("vibe_keyboard.daemon.server.app.save_config"):
                    response, body = await self.json(
                        "POST", f"/permissions/{session_id}", {"action": action.value}
                    )

                self.assertEqual(response.status, 200)
                self.assertEqual(body, {"ok": True})
                self.assertEqual(future.result(), action)
                self.assertFalse(self.state.permission_queue.pending_for_session(session_id))
                if action is PermissionAction.ALWAYS:
                    self.assertTrue(
                        self.state.permission_queue.is_always_allowed(
                            "Write", f"src/{action.value}.py"
                        )
                    )

    async def test_always_permission_persistence_failure_can_be_retried(self) -> None:
        session_id = 40
        permission = PendingPermission(session_id, "Write", "src/retry.py")
        self.state.permission_queue.push(permission)
        future: asyncio.Future[PermissionAction] = (
            asyncio.get_running_loop().create_future()
        )
        self.state.permission_responses[session_id] = future
        original_patterns = list(self.state.config.always_allow.patterns)

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.toml"
            self.app = DaemonApplication(self.state, config_path=path)
            with patch(
                "vibe_keyboard.daemon.server.app.save_config",
                side_effect=OSError("disk full"),
            ):
                response, body = await self.json(
                    "POST", f"/permissions/{session_id}", {"action": "always"}
                )

            self.assertEqual(response.status, 503)
            self.assertEqual(body, {"error": "failed to persist permission: disk full"})
            self.assertIs(self.state.permission_queue.current, permission)
            self.assertEqual(self.state.permission_queue.always_allow_list, ())
            self.assertEqual(self.state.config.always_allow.patterns, original_patterns)
            self.assertFalse(future.done())
            self.assertFalse(path.exists())

            response, body = await self.json(
                "POST", f"/permissions/{session_id}", {"action": "always"}
            )

            pattern = "Write(src/retry.py)"
            self.assertEqual(response.status, 200)
            self.assertEqual(body, {"ok": True})
            self.assertEqual(future.result(), PermissionAction.ALWAYS)
            self.assertFalse(self.state.permission_queue.pending_for_session(session_id))
            self.assertEqual(self.state.permission_queue.always_allow_list, (pattern,))
            self.assertEqual(self.state.config.always_allow.patterns, [pattern])
            self.assertEqual(load_config(path).always_allow.patterns, [pattern])

    async def test_permission_http_route_rejects_invalid_or_unknown_requests(self) -> None:
        self.state.permission_queue.push(PendingPermission(40, "Write", "src/app.py"))

        response, body = await self.json(
            "POST", "/permissions/40", {"action": "sometimes"}
        )
        self.assertEqual(response.status, 400)
        self.assertIn("error", body)
        self.assertTrue(self.state.permission_queue.pending_for_session(40))

        response, body = await self.json(
            "POST", "/permissions/999", {"action": "allow"}
        )
        self.assertEqual(response.status, 404)
        self.assertIn("error", body)

    async def test_send_and_cancel_buttons_resolve_permissions(self) -> None:
        await self.start_session()
        task = asyncio.create_task(
            self.json(
                "POST",
                "/event",
                {
                    "type": "PreToolUse",
                    "session_id": "abc",
                    "tool_name": "Edit",
                    "tool_input": "a.py",
                },
            )
        )
        for _ in range(100):
            if self.state.permission_queue.current:
                break
            await asyncio.sleep(0)
        response, _ = await self.json("POST", "/button", {"id": "send", "action": "click"})
        self.assertEqual(response.status, 200)
        _, body = await task
        self.assertEqual(body["hookSpecificOutput"]["decision"]["behavior"], "allow")

    async def test_macro_button_records_down_and_up(self) -> None:
        await self.json("POST", "/button", {"id": "delete", "action": "down"})
        await self.json("POST", "/button", {"id": "delete", "action": "up"})
        self.assertEqual(self.state.injector.events, [("down", "ctrl_u"), ("up", "ctrl_u")])
        self.assertEqual(self.state.held_keys, set())

    async def test_uplink_macro_buttons_preserve_press_and_release(self) -> None:
        await self.app.handle_uplink(ButtonPress(ButtonId.DELETE))
        await self.app.handle_uplink(ButtonRelease(ButtonId.DELETE))
        await self.app.handle_uplink(ButtonPress(ButtonId.VOICE))
        await self.app.handle_uplink(ButtonRelease(ButtonId.VOICE))

        self.assertEqual(
            self.state.injector.events,
            [
                ("down", "ctrl_u"),
                ("up", "ctrl_u"),
                ("down", "dictation"),
                ("up", "dictation"),
            ],
        )
        self.assertEqual(self.state.held_keys, set())

    async def test_duplicate_voice_edges_are_idempotent(self) -> None:
        await self.app.handle_uplink(ButtonPress(ButtonId.VOICE))
        await self.app.handle_uplink(ButtonPress(ButtonId.VOICE))
        await self.app.handle_uplink(ButtonRelease(ButtonId.VOICE))
        await self.app.handle_uplink(ButtonRelease(ButtonId.VOICE))

        self.assertEqual(
            self.state.injector.events,
            [("down", "dictation"), ("up", "dictation")],
        )
        self.assertEqual(self.state.held_keys, set())

    async def test_transport_disconnect_releases_voice(self) -> None:
        transport = RecordingTransport()
        await self.app.set_transport(transport)
        await self.app.handle_uplink(ButtonPress(ButtonId.VOICE))

        await self.app.clear_transport(transport)

        self.assertEqual(
            self.state.injector.events,
            [("down", "dictation"), ("up", "dictation")],
        )
        self.assertEqual(self.state.held_keys, set())

    async def test_origin_guard(self) -> None:
        response = await self.app.dispatch(
            "POST", "/button", {"origin": "https://evil.example"}, b"{}"
        )
        self.assertEqual(response.status, 403)

    async def test_loopback_origin_cors_headers(self) -> None:
        origin = "http://localhost:5173"
        response = await self.app.dispatch(
            "OPTIONS",
            "/device/state",
            {
                "origin": origin,
                "access-control-request-method": "GET",
                "access-control-request-headers": "content-type",
            },
            b"",
        )
        self.assertEqual(response.status, 204)
        self.assertEqual(response.headers["Access-Control-Allow-Origin"], origin)
        self.assertEqual(
            response.headers["Access-Control-Allow-Methods"], "GET, POST, OPTIONS"
        )
        self.assertIn("Content-Type", response.headers["Access-Control-Allow-Headers"])

        response = await self.app.dispatch(
            "GET", "/device/state", {"origin": origin}, b""
        )
        self.assertEqual(response.status, 200)
        self.assertEqual(response.headers["Access-Control-Allow-Origin"], origin)

    async def test_knob_rejects_steps_outside_wire_range(self) -> None:
        for steps in (0, 256):
            with self.subTest(steps=steps):
                response, body = await self.json(
                    "POST", "/knob", {"action": "cw", "steps": steps}
                )
                self.assertEqual(response.status, 400)
                self.assertEqual(body, {"error": "invalid steps"})

    async def test_config_route_validates_and_persists(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.toml"
            app = DaemonApplication(self.state, config_path=path)
            response = await app.dispatch(
                "POST",
                "/config",
                {"content-type": "application/json"},
                json.dumps({"key": "sound.volume", "value": "55"}).encode(),
            )
            body = json.loads(response.body)
            self.assertEqual(response.status, 200)
            self.assertTrue(body["ok"])
            self.assertEqual(self.state.config.sound.volume, 55)
            self.assertTrue(path.exists())

    async def test_config_route_rolls_back_when_persistence_fails(self) -> None:
        with patch(
            "vibe_keyboard.daemon.server.app.save_config",
            side_effect=OSError("disk full"),
        ):
            response, body = await self.json(
                "POST", "/config", {"key": "sound.volume", "value": "55"}
            )

        self.assertEqual(response.status, 400)
        self.assertEqual(body, {"error": "disk full"})
        self.assertEqual(self.state.config.sound.volume, 80)

    async def test_http_yolo_config_change_syncs_connected_device(self) -> None:
        transport = RecordingTransport()
        self.state.transport = transport
        with tempfile.TemporaryDirectory() as directory:
            app = DaemonApplication(
                self.state,
                config_path=Path(directory) / "config.toml",
            )
            response = await app.dispatch(
                "POST",
                "/config",
                {"content-type": "application/json"},
                json.dumps({"key": "yolo.active", "value": "true"}).encode(),
            )

        self.assertEqual(response.status, 200)
        updates = [item for item in transport.downlinks if isinstance(item, YoloConfigUpdate)]
        self.assertTrue(updates[-1].active)


class HttpServerTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self) -> None:
        self.app = DaemonApplication(DaemonState())
        self.server = HookServer(self.app, port=0)
        await self.server.start()
        socket = self.server.sockets[0]
        self.port = socket.getsockname()[1]

    async def asyncTearDown(self) -> None:
        await self.server.close()

    async def request(self, request: bytes) -> bytes:
        reader, writer = await asyncio.open_connection("127.0.0.1", self.port)
        writer.write(request)
        await writer.drain()
        response = await reader.read()
        writer.close()
        await writer.wait_closed()
        return response

    async def test_real_health_request(self) -> None:
        response = await self.request(b"GET /health HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
        self.assertIn(b"HTTP/1.1 200 OK", response)
        self.assertIn(b'{"status":"ok"}', response)

    async def test_real_device_state_request(self) -> None:
        self.app.state.store.update(
            DaemonSession(SessionInfo(id=5, name="Socket session", source="codex"))
        )
        self.app.state.active_session_id = 5

        response = await self.request(
            b"GET /device/state HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
        )
        head, _, payload = response.partition(b"\r\n\r\n")
        self.assertIn(b"HTTP/1.1 200 OK", head)
        body = json.loads(payload)
        self.assertEqual(body["active_session_id"], 5)
        self.assertFalse(body["ble_connected"])
        self.assertEqual([item["id"] for item in body["sessions"]], [5])

    async def test_rejects_transfer_encoding(self) -> None:
        response = await self.request(
            b"POST /event HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n"
        )
        self.assertIn(b"HTTP/1.1 400 Bad Request", response)


if __name__ == "__main__":
    unittest.main()

from __future__ import annotations

import asyncio
import logging
import sys
from collections.abc import Callable
from types import SimpleNamespace
from unittest.mock import AsyncMock

import pytest

from vibe_keyboard.core import ButtonId, SoundType
from vibe_keyboard.protocol import ButtonPress, FrameData, PlaySound, encode_downlink, encode_uplink
from vibe_keyboard.transport import BleTransport, TransportDisconnected, TransportError
from vibe_keyboard.transport import ble as ble_module


class FakeClient:
    def __init__(self) -> None:
        self.is_connected = True
        self.writes: list[tuple[str, bytes, bool]] = []
        self.write_attempts = 0
        self.write_errors: list[Exception] = []
        self.disconnected = False
        self.stopped = False
        self.fail_writes = False

    async def write_gatt_char(self, uuid: str, value: bytes, *, response: bool) -> None:
        self.write_attempts += 1
        if self.write_errors:
            raise self.write_errors.pop(0)
        if self.fail_writes:
            raise OSError("not connected")
        self.writes.append((uuid, value, response))

    async def stop_notify(self, _uuid: str) -> None:
        self.stopped = True

    async def disconnect(self) -> None:
        self.disconnected = True
        self.is_connected = False


@pytest.mark.asyncio
async def test_ble_module_does_not_require_bleak_until_connect(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setitem(sys.modules, "bleak", None)
    with pytest.raises(TransportError, match="optional dependency"):
        await BleTransport.connect(0.01)


@pytest.mark.asyncio
async def test_ble_connect_scans_service_and_subscribes_event_characteristic(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    device = SimpleNamespace(name="ExternalSimulator")
    advertisement = SimpleNamespace(service_uuids=[ble_module.VK_SERVICE_UUID.upper()])
    calls: dict[str, object] = {}

    class Scanner:
        @classmethod
        async def find_device_by_filter(
            cls,
            predicate: Callable[[object, object], bool],
            **kwargs: float,
        ) -> object:
            calls["timeout"] = kwargs["timeout"]
            calls["matched"] = predicate(device, advertisement)
            return device

    class ConnectingClient(FakeClient):
        def __init__(self, found: object, *, disconnected_callback: object) -> None:
            super().__init__()
            calls["device"] = found
            calls["disconnect_callback"] = disconnected_callback

        async def connect(self) -> None:
            calls["connected"] = True

        async def start_notify(self, uuid: str, callback: object) -> None:
            calls["notify"] = (uuid, callback)

    monkeypatch.setitem(
        sys.modules,
        "bleak",
        SimpleNamespace(BleakClient=ConnectingClient, BleakScanner=Scanner),
    )

    transport = await BleTransport.connect(1.25)

    assert calls["matched"] is True
    assert calls["timeout"] == 1.25
    assert calls["device"] is device
    assert calls["connected"] is True
    assert calls["notify"] == (ble_module.VK_EVENT_CHAR_UUID, transport._notification)
    await transport.aclose()


@pytest.mark.asyncio
async def test_ble_connect_recovers_corebluetooth_device_when_scan_is_empty(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    device = SimpleNamespace(name="VibeKeyboard", address="cached-id")
    calls: dict[str, object] = {}

    class Scanner:
        @classmethod
        async def find_device_by_filter(cls, *_args: object, **_kwargs: float) -> None:
            return None

    class ConnectingClient(FakeClient):
        def __init__(self, found: object, *, disconnected_callback: object) -> None:
            super().__init__()
            calls["device"] = found
            calls["disconnect_callback"] = disconnected_callback

        async def connect(self) -> None:
            calls["connected"] = True

        async def start_notify(self, uuid: str, callback: object) -> None:
            calls["notify"] = (uuid, callback)

    retrieve = AsyncMock(return_value=device)
    monkeypatch.setattr(ble_module, "_retrieve_corebluetooth_device", retrieve)
    monkeypatch.setitem(
        sys.modules,
        "bleak",
        SimpleNamespace(BleakClient=ConnectingClient, BleakScanner=Scanner),
    )

    transport = await BleTransport.connect(0.1)

    retrieve.assert_awaited_once_with()
    assert calls["device"] is device
    assert calls["connected"] is True
    assert calls["notify"] == (ble_module.VK_EVENT_CHAR_UUID, transport._notification)
    await transport.aclose()


@pytest.mark.asyncio
async def test_ble_decodes_notifications_and_writes_downlinks(
    caplog: pytest.LogCaptureFixture,
) -> None:
    client = FakeClient()
    transport = BleTransport(client)
    uplink = ButtonPress(ButtonId.SEND)
    with caplog.at_level(logging.INFO, logger="vibe_keyboard.daemon.ble"):
        transport._notification(None, bytearray(encode_uplink(uplink)))
    assert await transport.recv_uplink() == uplink
    assert "ble.uplink notification tag=0x01 bytes=2 prefix=01 04 message=ButtonPress" in (
        caplog.text
    )

    downlink = PlaySound(SoundType.CLICK)
    await transport.send_downlink(downlink)
    assert client.writes[0][1] == encode_downlink(downlink)
    assert client.writes[0][2] is True


@pytest.mark.asyncio
async def test_ble_drops_frame_data_and_closes_cleanly() -> None:
    client = FakeClient()
    transport = BleTransport(client)
    await transport.send_downlink(FrameData(1, 1, b"\x00\x00"))
    assert client.writes == []
    await transport.aclose()
    assert client.stopped
    assert client.disconnected
    assert not transport.connected


@pytest.mark.asyncio
async def test_ble_disconnect_wakes_pending_receive() -> None:
    client = FakeClient()
    transport = BleTransport(client)
    receive = asyncio.create_task(transport.recv_uplink())

    await asyncio.sleep(0)
    client.is_connected = False
    transport._mark_disconnected()

    with pytest.raises(TransportDisconnected):
        await receive
    assert not transport.connected


@pytest.mark.asyncio
async def test_ble_receive_polls_connection_state(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(ble_module, "DISCONNECT_POLL_SECONDS", 0.01)
    client = FakeClient()
    transport = BleTransport(client)
    receive = asyncio.create_task(transport.recv_uplink())

    await asyncio.sleep(0)
    client.is_connected = False

    with pytest.raises(TransportDisconnected):
        await receive
    assert not transport.connected


@pytest.mark.asyncio
async def test_ble_write_failure_marks_disconnected() -> None:
    client = FakeClient()
    client.fail_writes = True
    transport = BleTransport(client)

    with pytest.raises(TransportError, match="BLE write failed"):
        await transport.send_downlink(PlaySound(SoundType.CLICK))
    assert not transport.connected


@pytest.mark.asyncio
async def test_ble_retries_insufficient_resource_without_disconnecting(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(ble_module, "WRITE_RETRY_DELAY_SECONDS", 0)
    client = FakeClient()
    client.write_errors = [
        OSError(17, "GATT Protocol Error: Insufficient Resource"),
        OSError(17, "GATT Protocol Error: Insufficient Resource"),
    ]
    transport = BleTransport(client)

    await transport.send_downlink(PlaySound(SoundType.CLICK))

    assert client.write_attempts == 3
    assert len(client.writes) == 1
    assert transport.connected


@pytest.mark.asyncio
async def test_ble_exhausted_resource_retries_preserve_connection(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(ble_module, "WRITE_RETRY_DELAY_SECONDS", 0)
    client = FakeClient()
    client.write_errors = [
        OSError(17, "GATT Protocol Error: Insufficient Resource")
        for _ in range(ble_module.WRITE_RETRY_ATTEMPTS)
    ]
    transport = BleTransport(client)

    with pytest.raises(TransportError, match="Insufficient Resource"):
        await transport.send_downlink(PlaySound(SoundType.CLICK))

    assert client.write_attempts == ble_module.WRITE_RETRY_ATTEMPTS
    assert transport.connected


@pytest.mark.asyncio
async def test_ble_serializes_concurrent_writes() -> None:
    class ConcurrentClient(FakeClient):
        def __init__(self) -> None:
            super().__init__()
            self.active_writes = 0
            self.max_active_writes = 0

        async def write_gatt_char(
            self, uuid: str, value: bytes, *, response: bool
        ) -> None:
            self.active_writes += 1
            self.max_active_writes = max(self.max_active_writes, self.active_writes)
            try:
                await asyncio.sleep(0)
                await super().write_gatt_char(uuid, value, response=response)
            finally:
                self.active_writes -= 1

    client = ConcurrentClient()
    transport = BleTransport(client)

    await asyncio.gather(
        transport.send_downlink(PlaySound(SoundType.CLICK)),
        transport.send_downlink(PlaySound(SoundType.ERROR)),
    )

    assert client.max_active_writes == 1
    assert len(client.writes) == 2


@pytest.mark.asyncio
async def test_ble_rejects_unsupported_directions() -> None:
    transport = BleTransport(FakeClient())
    with pytest.raises(TransportError):
        await transport.send_uplink(ButtonPress(ButtonId.SEND))
    with pytest.raises(TransportError):
        await transport.recv_downlink()
    await transport.aclose()

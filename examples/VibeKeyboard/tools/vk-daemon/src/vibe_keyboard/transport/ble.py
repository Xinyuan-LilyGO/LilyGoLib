"""Optional BLE GATT transport, imported without requiring bleak at startup."""

from __future__ import annotations

import asyncio
import importlib
import logging
import sys
from contextlib import suppress
from typing import Any

from vibe_keyboard.protocol import (
    DownlinkMessage,
    FrameData,
    UplinkMessage,
    decode_uplink,
    encode_downlink,
)

from .base import TransportDisconnected, TransportEncodingError, TransportError

VK_SERVICE_UUID = "5a5f5b5e-1234-5678-abcd-000000000001"
VK_CMD_CHAR_UUID = "5a5f5b5e-1234-5678-abcd-000000000002"
VK_EVENT_CHAR_UUID = "5a5f5b5e-1234-5678-abcd-000000000003"
VK_DEVICE_NAME = "VibeKeyboard"
DISCONNECT_POLL_SECONDS = 0.5
WRITE_RETRY_ATTEMPTS = 4
WRITE_RETRY_DELAY_SECONDS = 0.05
LOGGER = logging.getLogger("vibe_keyboard.daemon.ble")


async def _retrieve_corebluetooth_device() -> Any | None:
    if sys.platform != "darwin":
        return None
    try:
        core_bluetooth = importlib.import_module("CoreBluetooth")
        scanner_module = importlib.import_module("bleak.backends.corebluetooth.scanner")
        device_module = importlib.import_module("bleak.backends.device")
        scanner = scanner_module.BleakScannerCoreBluetooth(
            None,
            [VK_SERVICE_UUID],
            "active",
            cb={},
        )
        await scanner._manager.wait_until_ready()
        service = core_bluetooth.CBUUID.UUIDWithString_(VK_SERVICE_UUID)
        peripherals = scanner._manager.central_manager.retrieveConnectedPeripheralsWithServices_(
            [service]
        )
        peripheral = next(
            (item for item in peripherals if item.name() == VK_DEVICE_NAME),
            next(iter(peripherals), None),
        )
        if peripheral is None:
            return None
        identifier = str(peripheral.identifier().UUIDString())
        LOGGER.info(
            "ble.recovery retrieved name=%s identifier=%s",
            peripheral.name(),
            identifier,
        )
        return device_module.BLEDevice(
            identifier,
            peripheral.name(),
            (peripheral, scanner._manager),
        )
    except Exception as error:
        LOGGER.debug("ble.recovery unavailable error=%s", error)
        return None


class BleTransport:
    """Daemon-side GATT transport for a Vibe Keyboard peripheral."""

    def __init__(self, client: Any) -> None:
        self._client = client
        self._uplink_rx: asyncio.Queue[UplinkMessage | TransportError] = asyncio.Queue(64)
        self._closed = False
        self._loop = asyncio.get_running_loop()
        self._disconnected = asyncio.Event()
        self._write_lock = asyncio.Lock()

    @classmethod
    async def connect(cls, scan_timeout: float = 5.0) -> BleTransport:
        """Scan, connect, and subscribe; bleak remains an optional dependency."""

        try:
            from bleak import BleakClient, BleakScanner
        except ImportError as error:
            raise TransportError(
                "BLE support requires the optional dependency: install vibe-keyboard[ble]"
            ) from error

        def matches(device: Any, advertisement: Any) -> bool:
            uuids = {value.lower() for value in (advertisement.service_uuids or [])}
            return device.name == VK_DEVICE_NAME or VK_SERVICE_UUID in uuids

        device = await BleakScanner.find_device_by_filter(matches, timeout=scan_timeout)
        if device is None:
            device = await _retrieve_corebluetooth_device()
        if device is None:
            raise TransportError(f"no {VK_DEVICE_NAME!r} BLE device found")
        transport: BleTransport | None = None

        def disconnected_callback(_client: Any) -> None:
            if transport is not None:
                transport._mark_disconnected()

        client = BleakClient(device, disconnected_callback=disconnected_callback)
        try:
            await client.connect()
            transport = cls(client)
            await client.start_notify(VK_EVENT_CHAR_UUID, transport._notification)
        except Exception as error:
            with suppress(Exception):
                await client.disconnect()
            raise TransportError(f"BLE connection failed: {error}") from error
        return transport

    @property
    def connected(self) -> bool:
        return (
            not self._closed
            and not self._disconnected.is_set()
            and bool(self._client.is_connected)
        )

    def is_connected(self) -> bool:
        return self.connected

    def _notification(self, _characteristic: Any, data: bytearray) -> None:
        payload = bytes(data)
        tag = f"0x{payload[0]:02x}" if payload else "none"
        prefix = payload[:8].hex(" ") or "empty"
        try:
            item: UplinkMessage | TransportError = decode_uplink(payload)
        except Exception as error:
            item = TransportEncodingError(f"invalid BLE uplink: {error}")
            LOGGER.warning(
                "ble.uplink invalid tag=%s bytes=%d prefix=%s error=%s",
                tag,
                len(payload),
                prefix,
                error,
            )
        else:
            LOGGER.info(
                "ble.uplink notification tag=%s bytes=%d prefix=%s message=%s",
                tag,
                len(payload),
                prefix,
                type(item).__name__,
            )
        if self._uplink_rx.full():
            LOGGER.warning(
                "ble.uplink queue_full dropped_oldest incoming=%s",
                type(item).__name__,
            )
            with suppress(asyncio.QueueEmpty):
                self._uplink_rx.get_nowait()
        self._uplink_rx.put_nowait(item)

    async def send_uplink(self, message: UplinkMessage) -> None:
        del message
        raise TransportError("daemon-side BLE transport cannot send uplink messages")

    async def send_downlink(self, message: DownlinkMessage) -> None:
        if not self.connected:
            self._mark_disconnected()
            raise TransportDisconnected("BLE transport is disconnected")
        if isinstance(message, FrameData):
            return
        payload = encode_downlink(message)
        async with self._write_lock:
            for attempt in range(1, WRITE_RETRY_ATTEMPTS + 1):
                if not self.connected:
                    self._mark_disconnected()
                    raise TransportDisconnected("BLE transport is disconnected")
                try:
                    LOGGER.info(
                        "ble.downlink write tag=0x%02x bytes=%d response=True",
                        payload[0],
                        len(payload),
                    )
                    await self._client.write_gatt_char(
                        VK_CMD_CHAR_UUID, payload, response=True
                    )
                    return
                except Exception as error:
                    retryable = self.connected and _is_insufficient_resource(error)
                    if retryable and attempt < WRITE_RETRY_ATTEMPTS:
                        delay = WRITE_RETRY_DELAY_SECONDS * (2 ** (attempt - 1))
                        LOGGER.info(
                            "ble.downlink retry tag=0x%02x attempt=%d/%d delay=%.2f error=%s",
                            payload[0],
                            attempt + 1,
                            WRITE_RETRY_ATTEMPTS,
                            delay,
                            error,
                        )
                        await asyncio.sleep(delay)
                        continue
                    if not retryable:
                        self._mark_disconnected()
                    raise TransportError(f"BLE write failed: {error}") from error

    async def recv_uplink(self) -> UplinkMessage:
        while True:
            if not self._uplink_rx.empty():
                item = self._uplink_rx.get_nowait()
                if isinstance(item, TransportError):
                    raise item
                return item
            if not self.connected:
                raise TransportDisconnected("BLE transport is disconnected")
            get_task = asyncio.create_task(self._uplink_rx.get())
            disconnect_task = asyncio.create_task(self._disconnected.wait())
            try:
                done, _ = await asyncio.wait(
                    {get_task, disconnect_task},
                    timeout=DISCONNECT_POLL_SECONDS,
                    return_when=asyncio.FIRST_COMPLETED,
                )
                if get_task in done:
                    item = await get_task
                    if isinstance(item, TransportError):
                        raise item
                    return item
                if disconnect_task in done or not self.connected:
                    raise TransportDisconnected("BLE transport is disconnected")
            finally:
                for task in (get_task, disconnect_task):
                    if not task.done():
                        task.cancel()
                        with suppress(asyncio.CancelledError):
                            await task

    async def recv_downlink(self) -> DownlinkMessage:
        raise TransportError("daemon-side BLE transport cannot receive downlink messages")

    def _mark_disconnected(self) -> None:
        try:
            loop = asyncio.get_running_loop()
        except RuntimeError:
            self._loop.call_soon_threadsafe(self._disconnected.set)
        else:
            if loop is self._loop:
                self._disconnected.set()
            else:
                self._loop.call_soon_threadsafe(self._disconnected.set)

    async def aclose(self) -> None:
        if self._closed:
            return
        self._closed = True
        self._mark_disconnected()
        with suppress(Exception):
            await self._client.stop_notify(VK_EVENT_CHAR_UUID)
        with suppress(Exception):
            await self._client.disconnect()

    async def __aenter__(self) -> BleTransport:
        return self

    async def __aexit__(self, *_exc: object) -> None:
        await self.aclose()


def _is_insufficient_resource(error: Exception) -> bool:
    current: BaseException | None = error
    while current is not None:
        text = str(current).lower()
        if "insufficient resource" in text or "insufficient resources" in text:
            return True
        current = current.__cause__ or current.__context__
    return False


__all__ = [
    "DISCONNECT_POLL_SECONDS",
    "VK_CMD_CHAR_UUID",
    "VK_DEVICE_NAME",
    "VK_EVENT_CHAR_UUID",
    "VK_SERVICE_UUID",
    "WRITE_RETRY_ATTEMPTS",
    "WRITE_RETRY_DELAY_SECONDS",
    "BleTransport",
]

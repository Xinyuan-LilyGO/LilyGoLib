"""Common asynchronous transport contract and errors."""

from __future__ import annotations

from typing import Protocol, runtime_checkable

from vibe_keyboard.protocol import DownlinkMessage, UplinkMessage


class TransportError(Exception):
    """Base class for transport failures."""


class TransportDisconnected(TransportError):
    """The peer closed the transport or the transport was explicitly closed."""


class TransportTimeout(TransportError):
    """A transport operation exceeded its deadline."""


class TransportEncodingError(TransportError):
    """A wire message or transport frame was malformed."""


@runtime_checkable
class Transport(Protocol):
    """Bidirectional keyboard/daemon message transport."""

    async def send_uplink(self, message: UplinkMessage) -> None:
        """Send a keyboard-to-daemon message."""

    async def send_downlink(self, message: DownlinkMessage) -> None:
        """Send a daemon-to-keyboard message."""

    async def recv_uplink(self) -> UplinkMessage:
        """Wait for a keyboard-to-daemon message."""

    async def recv_downlink(self) -> DownlinkMessage:
        """Wait for a daemon-to-keyboard message."""

    @property
    def connected(self) -> bool:
        """Whether the transport can currently exchange messages."""

    def is_connected(self) -> bool:
        """Compatibility method matching the original Rust trait."""

    async def aclose(self) -> None:
        """Close the transport and release all underlying resources."""


__all__ = [
    "Transport",
    "TransportDisconnected",
    "TransportEncodingError",
    "TransportError",
    "TransportTimeout",
]

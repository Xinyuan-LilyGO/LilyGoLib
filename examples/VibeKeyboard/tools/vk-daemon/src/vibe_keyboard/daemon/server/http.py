"""Small HTTP/1.1 server for loopback hooks and device-control clients."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass, field
from http import HTTPStatus

from .app import DaemonApplication


@dataclass(slots=True)
class HttpResponse:
    status: int = 200
    body: bytes = b""
    headers: dict[str, str] = field(default_factory=dict)


class HookServer:
    MAX_HEADER_BYTES = 65_536
    MAX_BODY_BYTES = 5 * 1024 * 1024

    def __init__(
        self,
        application: DaemonApplication,
        host: str = "127.0.0.1",
        port: int = 19280,
    ) -> None:
        if host not in {"127.0.0.1", "::1", "localhost"}:
            raise ValueError("daemon HTTP server must bind to loopback")
        self.application = application
        self.host = host
        self.port = port
        self._server: asyncio.Server | None = None

    @property
    def sockets(self) -> tuple[object, ...]:
        return tuple(self._server.sockets or ()) if self._server else ()

    async def start(self) -> None:
        self._server = await asyncio.start_server(
            self._handle_client,
            self.host,
            self.port,
            limit=self.MAX_HEADER_BYTES + 1,
        )

    async def serve_forever(self) -> None:
        if self._server is None:
            await self.start()
        assert self._server is not None
        async with self._server:
            await self._server.serve_forever()

    async def close(self) -> None:
        if self._server is not None:
            self._server.close()
            await self._server.wait_closed()
            self._server = None

    async def _handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            response = await self._read_and_dispatch(reader)
        except asyncio.IncompleteReadError:
            response = HttpResponse(400, b'{"error":"incomplete request"}', {"Content-Type": "application/json"})
        except (ValueError, UnicodeError) as error:
            response = HttpResponse(
                400,
                (f'{{"error":"{str(error).replace(chr(34), chr(39))}"}}').encode(),
                {"Content-Type": "application/json"},
            )
        except Exception:
            response = HttpResponse(500, b'{"error":"internal server error"}', {"Content-Type": "application/json"})
        await self._write_response(writer, response)

    async def _read_and_dispatch(self, reader: asyncio.StreamReader) -> HttpResponse:
        request_line = await reader.readline()
        if not request_line or len(request_line) > 8192:
            raise ValueError("invalid request line")
        method, target, version = request_line.decode("ascii").rstrip("\r\n").split(" ", 2)
        if version not in {"HTTP/1.0", "HTTP/1.1"}:
            raise ValueError("unsupported HTTP version")
        headers: dict[str, str] = {}
        total = len(request_line)
        while True:
            line = await reader.readline()
            total += len(line)
            if total > self.MAX_HEADER_BYTES:
                return HttpResponse(431, b'{"error":"headers too large"}', {"Content-Type": "application/json"})
            if line in {b"\r\n", b"\n", b""}:
                break
            name, separator, value = line.decode("latin-1").partition(":")
            if not separator:
                raise ValueError("malformed header")
            headers[name.strip().lower()] = value.strip()
        if "transfer-encoding" in headers:
            return HttpResponse(400, b'{"error":"transfer encoding is unsupported"}', {"Content-Type": "application/json"})
        try:
            length = int(headers.get("content-length", "0"))
        except ValueError as error:
            raise ValueError("invalid content length") from error
        if length < 0 or length > self.MAX_BODY_BYTES:
            return HttpResponse(413, b'{"error":"request body too large"}', {"Content-Type": "application/json"})
        body = await reader.readexactly(length) if length else b""
        app_response = await self.application.dispatch(method.upper(), target, headers, body)
        return HttpResponse(app_response.status, app_response.body, app_response.headers)

    async def _write_response(self, writer: asyncio.StreamWriter, response: HttpResponse) -> None:
        try:
            phrase = HTTPStatus(response.status).phrase
        except ValueError:
            phrase = "Unknown"
        headers = {
            "Content-Length": str(len(response.body)),
            "Connection": "close",
            "Cache-Control": "no-store",
            **response.headers,
        }
        head = f"HTTP/1.1 {response.status} {phrase}\r\n" + "".join(
            f"{key}: {value}\r\n" for key, value in headers.items()
        ) + "\r\n"
        writer.write(head.encode("latin-1") + response.body)
        try:
            await writer.drain()
        finally:
            writer.close()
            await writer.wait_closed()

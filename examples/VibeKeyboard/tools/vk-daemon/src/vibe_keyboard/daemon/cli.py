"""Command-line interface for the Vibe Keyboard daemon."""

from __future__ import annotations

import argparse
import asyncio
import json
import logging
import shutil
import signal
import sys
import urllib.error
import urllib.request
from collections.abc import Awaitable, Callable, Sequence
from contextlib import suppress
from pathlib import Path
from typing import Any

from ..transport import BleTransport, Transport, TransportError
from .config import DaemonConfig, default_config_path, load_config, save_config, set_config_value
from .logging import LOGGER_NAME, configure_logging
from .macos_permissions import (
    MicrophoneAuthorization,
    request_event_post_access,
    request_microphone_access,
)
from .scanner import run_transcript_scanner
from .server import DaemonApplication, DaemonState, HookServer
from .setup import SetupManager

LOGGER = logging.getLogger(f"{LOGGER_NAME}.cli")


def _request(port: int, path: str, payload: dict[str, Any] | None = None) -> Any:
    body = json.dumps(payload).encode() if payload is not None else None
    request = urllib.request.Request(
        f"http://127.0.0.1:{port}{path}",
        data=body,
        headers={"Content-Type": "application/json", "X-Vibe-Keyboard": "cli"},
        method="POST" if payload is not None else "GET",
    )
    with urllib.request.urlopen(request, timeout=10) as response:
        content_type = response.headers.get("Content-Type", "")
        data = response.read()
    return json.loads(data or b"{}") if "json" in content_type else data


async def _prepare_voice_permissions(config: DaemonConfig) -> None:
    if config.macros.voice.lower() != "dictation":
        return
    microphone, event_post_access = await asyncio.gather(
        asyncio.to_thread(request_microphone_access),
        asyncio.to_thread(request_event_post_access),
    )
    if microphone in {
        MicrophoneAuthorization.DENIED,
        MicrophoneAuthorization.RESTRICTED,
    }:
        LOGGER.warning(
            "voice.microphone status=%s action=%s",
            microphone.value,
            "enable the host app in System Settings > Privacy & Security > Microphone",
        )
    elif microphone is not MicrophoneAuthorization.NOT_APPLICABLE:
        LOGGER.info("voice.microphone status=%s", microphone.value)
    if not event_post_access:
        LOGGER.warning(
            "voice.accessibility status=denied action=%s",
            "enable the host app in System Settings > Privacy & Security > Accessibility",
        )
    else:
        LOGGER.info("voice.accessibility status=authorized")


async def run_serve(
    config: DaemonConfig,
    host: str = "127.0.0.1",
    *,
    config_path: Path | None = None,
    stop_event: asyncio.Event | None = None,
) -> None:
    log_path = configure_logging(config)
    await _prepare_voice_permissions(config)
    state = DaemonState(config=config)
    application = DaemonApplication(state, config_path=config_path)
    server = HookServer(application, host, config.general.hook_port)
    await server.start()
    LOGGER.info("serve.started url=http://%s:%s", host, config.general.hook_port)
    LOGGER.info("serve.bluetooth enabled=%s", config.ble.enabled)
    LOGGER.info("serve.log_file path=%s", log_path)
    stop = stop_event or asyncio.Event()
    loop = asyncio.get_running_loop()
    for name in (signal.SIGINT, signal.SIGTERM):
        with suppress(NotImplementedError, RuntimeError):
            loop.add_signal_handler(name, stop.set)

    async def device_loop(
        label: str,
        connect: Callable[[], Awaitable[Transport]],
    ) -> None:
        while not stop.is_set():
            transport: Transport | None = None
            try:
                transport = await connect()
                LOGGER.info("device_loop.connected label=%s", label)
                await application.set_transport(transport)
                while transport.connected and not stop.is_set():
                    message = await transport.recv_uplink()
                    await application.handle_uplink(message)
            except asyncio.CancelledError:
                raise
            except TransportError as error:
                if not stop.is_set():
                    LOGGER.warning("device_loop.ended label=%s error=%s", label, error)
                    await asyncio.sleep(config.ble.reconnect_delay_seconds)
            except Exception:
                if not stop.is_set():
                    LOGGER.exception("device_loop.failed label=%s", label)
                    await asyncio.sleep(config.ble.reconnect_delay_seconds)
            finally:
                await application.clear_transport(transport)
                if transport is not None:
                    await transport.aclose()

    async def ble_connect() -> Transport:
        return await BleTransport.connect(config.ble.scan_timeout_seconds)

    async def scanner_loop() -> None:
        while not stop.is_set():
            try:
                await run_transcript_scanner(application, stop)
            except asyncio.CancelledError:
                raise
            except Exception:
                if not stop.is_set():
                    LOGGER.exception("transcript_scanner.failed")
                    await asyncio.sleep(2)

    tasks = [
        asyncio.create_task(scanner_loop(), name="vk-transcript-scanner"),
    ]
    if config.ble.enabled:
        tasks.append(asyncio.create_task(device_loop("BLE", ble_connect), name="vk-ble-central"))
    try:
        await stop.wait()
    finally:
        for task in tasks:
            task.cancel()
        for task in tasks:
            with suppress(asyncio.CancelledError):
                await task
        await application.release_held_inputs()
        await server.close()


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="vk-daemon", description="Vibe Keyboard daemon")
    parser.add_argument("--config", type=Path, default=default_config_path())
    commands = parser.add_subparsers(dest="command", required=True)

    serve = commands.add_parser("serve", help="run the HTTP server and optional BLE transport")
    serve.add_argument("--host", default="127.0.0.1")
    serve.add_argument("--port", type=int)
    serve.add_argument("--no-ble", action="store_true", help="disable Bluetooth LE scanning")
    serve.add_argument("--ble-scan-timeout", type=int)

    session = commands.add_parser("session", help="query sessions")
    session_commands = session.add_subparsers(dest="session_command", required=True)
    session_commands.add_parser("list")
    status = session_commands.add_parser("status")
    status.add_argument("id", type=int)
    focus = commands.add_parser("focus", help="focus a session")
    focus.add_argument("id", type=int)

    config = commands.add_parser("config", help="inspect or update configuration")
    config_commands = config.add_subparsers(dest="config_command", required=True)
    config_commands.add_parser("show")
    set_command = config_commands.add_parser("set")
    set_command.add_argument("key")
    set_command.add_argument("value")
    config_commands.add_parser("reset")

    setup = commands.add_parser("setup", help="manage AI tool hooks")
    setup_commands = setup.add_subparsers(dest="setup_command", required=True)
    setup_commands.add_parser("status")
    for action in ("install", "uninstall"):
        item = setup_commands.add_parser(action)
        item.add_argument("tool", choices=["claude-code", "codex"])
        item.add_argument("--port", type=int)

    notify = commands.add_parser("notify", help="notification tools")
    notify.add_argument("action", choices=["test"])
    parser.add_argument("--daemon-port", type=int, default=19280)
    return parser


def _print_json(value: Any) -> None:
    print(json.dumps(value, indent=2, ensure_ascii=False))


def _handle_remote(args: argparse.Namespace, port: int) -> int:
    try:
        if args.command == "session":
            sessions = _request(port, "/sessions")
            if args.session_command == "list":
                _print_json(sessions)
            elif args.session_command == "status":
                found = next((item for item in sessions if item.get("id") == args.id), None)
                if found is None:
                    print(f"session {args.id} not found", file=sys.stderr)
                    return 1
                _print_json(found)
        elif args.command == "focus":
            _print_json(_request(port, f"/focus/{args.id}", {}))
        elif args.command == "notify":
            _print_json(_request(port, "/notify/test", {}))
        return 0
    except (OSError, urllib.error.HTTPError, json.JSONDecodeError) as error:
        print(f"daemon request failed: {error}", file=sys.stderr)
        return 1


def main(argv: Sequence[str] | None = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)
    config = load_config(args.config)
    if args.command == "serve":
        if args.port is not None:
            config.general.hook_port = args.port
        if args.no_ble:
            config.ble.enabled = False
        if args.ble_scan_timeout is not None:
            config.ble.scan_timeout_seconds = args.ble_scan_timeout
        with suppress(KeyboardInterrupt):
            asyncio.run(run_serve(config, args.host, config_path=args.config))
        return 0
    if args.command == "config":
        if args.config_command == "show":
            _print_json(config.to_dict())
        elif args.config_command == "set":
            try:
                set_config_value(config, args.key, args.value)
                save_config(args.config, config)
            except (KeyError, OSError, TypeError, ValueError) as error:
                parser.error(str(error))
            print(f"updated {args.key}")
        else:
            if args.config.exists():
                backup = args.config.with_suffix(".toml.bak")
                shutil.copy2(args.config, backup)
            save_config(args.config, DaemonConfig())
            print(f"reset {args.config}")
        return 0
    if args.command == "setup":
        port = args.port if hasattr(args, "port") and args.port else config.general.hook_port
        try:
            if args.setup_command == "status":
                _print_json(asyncio.run(SetupManager.detect_all(port)).to_dict())
            elif args.setup_command == "install":
                asyncio.run(SetupManager.install_hook(args.tool, port))
                print(f"installed {args.tool} hook")
            else:
                asyncio.run(SetupManager.uninstall_hook(args.tool))
                print(f"uninstalled {args.tool} hook")
            return 0
        except (OSError, RuntimeError, ValueError) as error:
            print(f"setup failed: {error}", file=sys.stderr)
            return 1
    return _handle_remote(args, args.daemon_port)


if __name__ == "__main__":
    raise SystemExit(main())

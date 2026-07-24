# AGENTS.md

## Project Overview

Vibe Keyboard is a daemon-only AI coding-session controller implemented in Python 3.12+.
`vk-daemon` tracks Claude Code and Codex sessions, resolves permission requests, and exposes
loopback HTTP plus Bluetooth LE interfaces to external clients and devices.

## Architecture

- `vibe_keyboard.core`: dependency-free domain types
- `vibe_keyboard.protocol`: binary BLE wire messages and codec
- `vibe_keyboard.transport`: async transport contracts and optional BLE central
- `vibe_keyboard.daemon`: HTTP server, session management, permissions, hooks, and platform adapters

The daemon is the only runtime process and the authoritative state owner. Local clients use the
loopback HTTP API. Hardware and BLE simulators act as peripherals using the fixed service and
characteristic UUIDs documented in `docs/ble_protocol.md`.

## Scope Guardrails

- Do not add bundled client applications or process-local device transports.
- Keep the external simulator HTTP surface documented in `docs/http_api.md`.
- Keep the daemon HTTP listener restricted to loopback addresses.
- Preserve BLE message tags, field order, endianness, and fixed GATT UUIDs.

## Rules

- Python 3.12 or newer.
- Runtime code uses the standard library; BLE support uses the optional `bleak` extra.
- Async interfaces use `asyncio` and `typing.Protocol`.
- Every public HTTP route and wire-message variant needs tests.
- Every debuggable daemon module exposes a CLI command.
- User-facing conversation and documentation may be Chinese; code and commits use English.
- Use conventional commits: `feat`, `fix`, `refactor`, `docs`, `test`.

## Commands

```bash
uv sync --extra dev --extra ble
uv run pytest
uv run ruff check .
uv run mypy src
uv run vk-daemon serve
```

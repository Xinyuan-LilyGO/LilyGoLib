"""Background discovery and incremental transcript enrichment."""

from __future__ import annotations

import asyncio
from contextlib import suppress
from pathlib import Path

from .discovery import CompositeDiscovery, SessionDiscovery
from .server.app import DaemonApplication
from .session import HookEvent, SessionEvent, SessionEventKind
from .transcript import FileOffset, validate_transcript_path


async def run_transcript_scanner(
    application: DaemonApplication,
    stop: asyncio.Event,
    *,
    discovery: SessionDiscovery | None = None,
    interval: float = 2.0,
) -> None:
    backend = discovery or CompositeDiscovery()
    offsets: dict[str, FileOffset] = {}
    while not stop.is_set():
        sessions = await asyncio.to_thread(backend.discover)
        for discovered in sessions:
            numeric_id = application.state.session_id_map.get(discovered.session_id)
            if numeric_id is None:
                name = Path(discovered.cwd).name or discovered.project_dir
                if discovered.source == "codex" and discovered.project_dir:
                    name = discovered.project_dir
                hook = HookEvent(
                    event_type="SessionStart",
                    session_id=discovered.session_id,
                    name=name,
                    source=discovered.source,
                    cwd=discovered.cwd,
                    transcript_path=str(discovered.transcript_path),
                )
                event = SessionEvent(
                    SessionEventKind.STARTED,
                    discovered.session_id,
                    name=hook.name,
                    source=hook.source,
                    cwd=hook.cwd,
                    transcript_path=hook.transcript_path,
                )
                numeric_id = await application.process_session_event(event, hook)
            if numeric_id is None:
                continue
            path = discovered.transcript_path
            projects_root = getattr(backend, "projects_dir", None)
            if projects_root is not None and not validate_transcript_path(
                str(path), Path(projects_root)
            ):
                continue
            scanner = offsets.setdefault(discovered.session_id, FileOffset(path))
            changed = await asyncio.to_thread(scanner.scan)
            if not changed:
                continue
            async with application.state.lock:
                session = application.state.store.get(numeric_id)
                if session is None:
                    continue
                data = scanner.data
                session.info.model = data.model
                session.info.tokens_in = data.tokens_in
                session.info.tokens_out = data.tokens_out
                session.info.cost_usd = data.cost_usd
                session.info.context_pct = data.context_pct
                session.info.last_message = data.last_message
                session.info.last_ai_output = data.last_ai_output
            await application.sync_device()
        with suppress(TimeoutError):
            await asyncio.wait_for(stop.wait(), timeout=interval)


__all__ = ["run_transcript_scanner"]

"""Pluggable discovery of recent Claude Code transcripts."""

from __future__ import annotations

import json
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Protocol


@dataclass(frozen=True, slots=True)
class DiscoveredSession:
    session_id: str
    project_dir: str
    transcript_path: Path
    cwd: str
    source: str = "claude-code"


class SessionDiscovery(Protocol):
    @property
    def name(self) -> str: ...

    def discover(self) -> list[DiscoveredSession]: ...


def resolve_cwd_from_project_hash(value: str) -> str:
    parts = value.removeprefix("-").split("-")
    path = Path("/")
    index = 0
    while index < len(parts):
        end = len(parts)
        while end > index + 1 and not (path / "-".join(parts[index:end])).exists():
            end -= 1
        path /= "-".join(parts[index:end])
        index = end
    return str(path)


class FilesystemDiscovery:
    name = "filesystem"

    def __init__(self, projects_dir: Path | None = None, max_age_seconds: int = 86_400) -> None:
        self.projects_dir = projects_dir or Path.home() / ".claude" / "projects"
        self.max_age_seconds = max_age_seconds

    def discover(self) -> list[DiscoveredSession]:
        if not self.projects_dir.is_dir():
            return []
        cutoff = time.time() - self.max_age_seconds
        found: list[tuple[float, DiscoveredSession]] = []
        for project_dir in self.projects_dir.iterdir():
            if not project_dir.is_dir():
                continue
            cwd = resolve_cwd_from_project_hash(project_dir.name)
            for transcript in project_dir.glob("*.jsonl"):
                try:
                    modified = transcript.stat().st_mtime
                except OSError:
                    continue
                if modified < cutoff or transcript.stem == "subagents":
                    continue
                found.append(
                    (
                        modified,
                        DiscoveredSession(transcript.stem, project_dir.name, transcript, cwd),
                    )
                )
        found.sort(key=lambda item: item[0], reverse=True)
        return [session for _, session in found]


class CodexFilesystemDiscovery:
    name = "codex-filesystem"

    def __init__(
        self,
        sessions_dir: Path | None = None,
        index_path: Path | None = None,
        max_age_seconds: int = 86_400,
    ) -> None:
        codex_home = Path.home() / ".codex"
        self.sessions_dir = sessions_dir or codex_home / "sessions"
        self.index_path = index_path or codex_home / "session_index.jsonl"
        self.max_age_seconds = max_age_seconds

    def discover(self) -> list[DiscoveredSession]:
        if not self.sessions_dir.is_dir():
            return []
        names = self._thread_names()
        cutoff = time.time() - self.max_age_seconds
        found: list[tuple[float, DiscoveredSession]] = []
        for transcript in self.sessions_dir.glob("*/*/*/*.jsonl"):
            try:
                modified = transcript.stat().st_mtime
            except OSError:
                continue
            if modified < cutoff:
                continue
            session = self._session_from_file(transcript, names)
            if session is not None:
                found.append((modified, session))
        found.sort(key=lambda item: item[0], reverse=True)
        return [session for _, session in found]

    def _thread_names(self) -> dict[str, str]:
        names: dict[str, str] = {}
        try:
            with self.index_path.open("r", encoding="utf-8", errors="replace") as handle:
                for line in handle:
                    try:
                        raw = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if not isinstance(raw, dict):
                        continue
                    session_id = raw.get("id")
                    thread_name = raw.get("thread_name")
                    if isinstance(session_id, str) and isinstance(thread_name, str):
                        names[session_id] = thread_name
        except OSError:
            pass
        return names

    def _session_from_file(
        self, transcript: Path, names: dict[str, str]
    ) -> DiscoveredSession | None:
        try:
            with transcript.open("r", encoding="utf-8", errors="replace") as handle:
                raw = json.loads(handle.readline())
        except (OSError, json.JSONDecodeError):
            return None
        if not isinstance(raw, dict) or raw.get("type") != "session_meta":
            return None
        payload = raw.get("payload")
        if not isinstance(payload, dict):
            return None
        raw_session_id = payload.get("session_id", payload.get("id"))
        if not isinstance(raw_session_id, str) or not raw_session_id:
            return None
        cwd = payload.get("cwd")
        cwd_text = str(cwd) if cwd is not None else ""
        name = names.get(raw_session_id) or (Path(cwd_text).name if cwd_text else "")
        return DiscoveredSession(raw_session_id, name, transcript, cwd_text, "codex")


class CompositeDiscovery:
    name = "composite"

    def __init__(self, discoveries: list[SessionDiscovery] | None = None) -> None:
        self.discoveries = discoveries or [FilesystemDiscovery(), CodexFilesystemDiscovery()]

    def discover(self) -> list[DiscoveredSession]:
        sessions: list[DiscoveredSession] = []
        for discovery in self.discoveries:
            sessions.extend(discovery.discover())
        return sessions

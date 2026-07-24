"""Incremental Claude Code and Codex JSONL transcript parsing."""

from __future__ import annotations

import json
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .discovery import DiscoveredSession, FilesystemDiscovery


@dataclass(slots=True)
class TranscriptData:
    model: str = ""
    tokens_in: int = 0
    tokens_out: int = 0
    cost_usd: float = 0.0
    context_pct: int = 0
    context_window_tokens: int = 0
    last_message: str = ""
    last_ai_output: str = ""


_PRICES = {
    "opus": (15.0, 75.0),
    "sonnet": (3.0, 15.0),
    "haiku": (0.80, 4.0),
}


def context_window(model: str) -> int:
    return 1_000_000 if "1m" in model.lower() else 200_000


def calculate_cost(model: str, tokens_in: int, tokens_out: int) -> float:
    lowered = model.lower()
    input_price, output_price = next(
        (price for name, price in _PRICES.items() if name in lowered), (3.0, 15.0)
    )
    return tokens_in * input_price / 1_000_000 + tokens_out * output_price / 1_000_000


def _text_content(message: Mapping[str, Any]) -> str:
    content = message.get("content", "")
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        return "\n".join(
            str(item.get("text", ""))
            for item in content
            if isinstance(item, Mapping) and item.get("type") == "text"
        )
    return ""


def _int_value(value: Mapping[str, Any], key: str) -> int:
    try:
        raw = value.get(key, 0)
        return int(raw or 0)
    except (TypeError, ValueError):
        return 0


def parse_jsonl_line(line: str) -> tuple[str, dict[str, Any]] | None:
    try:
        raw = json.loads(line)
    except (json.JSONDecodeError, TypeError):
        return None
    if not isinstance(raw, dict):
        return None
    if raw.get("type") == "turn_context":
        payload = raw.get("payload")
        if not isinstance(payload, dict):
            return None
        model = payload.get("model")
        if not isinstance(model, str) or not model:
            return None
        return "metadata", {"model": model}
    if raw.get("type") == "event_msg":
        payload = raw.get("payload")
        if not isinstance(payload, dict):
            return None
        if payload.get("type") == "token_count":
            info = payload.get("info")
            if not isinstance(info, dict):
                return None
            total_usage = info.get("total_token_usage")
            if not isinstance(total_usage, dict):
                return None
            return "metadata", {
                "tokens_in": _int_value(total_usage, "input_tokens"),
                "tokens_out": _int_value(total_usage, "output_tokens"),
                "context_window": _int_value(info, "model_context_window"),
            }
        message = payload.get("message")
        if payload.get("type") == "user_message" and isinstance(message, str):
            return "user", {"content": message}
        if payload.get("type") == "agent_message" and isinstance(message, str):
            return "assistant", {
                "model": "",
                "tokens_in": 0,
                "tokens_out": 0,
                "content": message,
            }
    if raw.get("type") not in {"assistant", "user"}:
        return None
    message = raw.get("message")
    if not isinstance(message, dict):
        return None
    if raw["type"] == "user":
        return "user", {"content": _text_content(message)}
    raw_usage = raw.get("usage")
    if isinstance(raw_usage, dict):
        usage: Mapping[str, Any] = raw_usage
    else:
        message_usage = message.get("usage")
        usage = message_usage if isinstance(message_usage, dict) else {}
    tokens_in = sum(
        int(usage.get(key, 0) or 0)
        for key in ("input_tokens", "cache_creation_input_tokens", "cache_read_input_tokens")
    )
    return "assistant", {
        "model": str(message.get("model", raw.get("model", ""))),
        "tokens_in": tokens_in,
        "tokens_out": int(usage.get("output_tokens", 0) or 0),
        "content": _text_content(message),
    }


class FileOffset:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.offset = 0
        self.data = TranscriptData()

    def scan(self) -> bool:
        try:
            size = self.path.stat().st_size
            if size < self.offset:
                self.offset = 0
            changed = False
            with self.path.open("r", encoding="utf-8", errors="replace") as handle:
                handle.seek(self.offset)
                for line in handle:
                    parsed = parse_jsonl_line(line)
                    if parsed is None:
                        continue
                    changed = True
                    kind, entry = parsed
                    if kind == "user":
                        self.data.last_message = str(entry["content"])
                    elif kind == "assistant":
                        model = str(entry["model"])
                        if model:
                            self.data.model = model
                        self.data.tokens_in += int(entry["tokens_in"])
                        self.data.tokens_out += int(entry["tokens_out"])
                        output = str(entry["content"])
                        if output:
                            self.data.last_ai_output = output
                    elif kind == "metadata":
                        model = str(entry.get("model", ""))
                        if model:
                            self.data.model = model
                        if "tokens_in" in entry:
                            self.data.tokens_in = int(entry["tokens_in"])
                        if "tokens_out" in entry:
                            self.data.tokens_out = int(entry["tokens_out"])
                        if "context_window" in entry:
                            self.data.context_window_tokens = int(entry["context_window"])
                self.offset = handle.tell()
            self.data.cost_usd = calculate_cost(
                self.data.model, self.data.tokens_in, self.data.tokens_out
            )
            window = self.data.context_window_tokens or context_window(self.data.model)
            self.data.context_pct = min(100, self.data.tokens_in * 100 // window)
            return changed
        except OSError:
            return False


def find_transcript_path(session_id: str, projects_dir: Path | None = None) -> Path | None:
    root = projects_dir or Path.home() / ".claude" / "projects"
    if not root.is_dir() or "/" in session_id or "\\" in session_id:
        return None
    return next((path for path in root.glob(f"*/{session_id}.jsonl") if path.is_file()), None)


def discover_active_sessions() -> list[DiscoveredSession]:
    return FilesystemDiscovery().discover()


def validate_transcript_path(path: str, projects_dir: Path | None = None) -> bool:
    candidate = Path(path)
    root = projects_dir or Path.home() / ".claude" / "projects"
    if not candidate.is_absolute() or candidate.suffix != ".jsonl":
        return False
    try:
        resolved = candidate.resolve(strict=True)
        resolved_root = root.resolve(strict=True)
        resolved.relative_to(resolved_root)
        return resolved.is_file()
    except (OSError, ValueError):
        return False

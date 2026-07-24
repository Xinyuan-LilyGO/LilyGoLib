from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from vibe_keyboard.daemon.discovery import CodexFilesystemDiscovery, FilesystemDiscovery
from vibe_keyboard.daemon.focus import escape_jxa_string, validate_bundle_id, validate_tty
from vibe_keyboard.daemon.terminal import MacTerminalDetector, TerminalType
from vibe_keyboard.daemon.transcript import (
    FileOffset,
    calculate_cost,
    find_transcript_path,
    parse_jsonl_line,
    validate_transcript_path,
)


class TranscriptTests(unittest.TestCase):
    def test_parse_assistant_and_user(self) -> None:
        assistant = parse_jsonl_line(json.dumps({
            "type": "assistant",
            "message": {"model": "claude-opus-4-6", "content": [{"type": "text", "text": "ok"}]},
            "usage": {"input_tokens": 10, "cache_read_input_tokens": 5, "output_tokens": 3},
        }))
        user = parse_jsonl_line(json.dumps({"type": "user", "message": {"content": "hello"}}))
        self.assertEqual(assistant[1]["tokens_in"], 15)
        self.assertEqual(assistant[1]["content"], "ok")
        self.assertEqual(user[1]["content"], "hello")

    def test_parse_codex_turn_context_model(self) -> None:
        parsed = parse_jsonl_line(json.dumps({
            "type": "turn_context",
            "payload": {"model": "gpt-5.5", "cwd": "/tmp/project"},
        }))

        self.assertEqual(parsed, ("metadata", {"model": "gpt-5.5"}))

    def test_parse_codex_user_message(self) -> None:
        parsed = parse_jsonl_line(json.dumps({
            "type": "event_msg",
            "payload": {"type": "user_message", "message": "build the BLE screen"},
        }))

        self.assertEqual(parsed, ("user", {"content": "build the BLE screen"}))

    def test_parse_codex_token_count(self) -> None:
        parsed = parse_jsonl_line(json.dumps({
            "type": "event_msg",
            "payload": {
                "type": "token_count",
                "info": {
                    "total_token_usage": {"input_tokens": 20000, "output_tokens": 700},
                    "model_context_window": 100000,
                },
            },
        }))

        self.assertEqual(
            parsed,
            ("metadata", {"tokens_in": 20000, "tokens_out": 700, "context_window": 100000}),
        )

    def test_incremental_scan(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "session.jsonl"
            path.write_text(json.dumps({"type": "user", "message": {"content": "one"}}) + "\n")
            offset = FileOffset(path)
            self.assertTrue(offset.scan())
            self.assertEqual(offset.data.last_message, "one")
            with path.open("a") as handle:
                handle.write(json.dumps({"type": "user", "message": {"content": "two"}}) + "\n")
            self.assertTrue(offset.scan())
            self.assertEqual(offset.data.last_message, "two")

    def test_incremental_scan_reads_codex_model(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "session.jsonl"
            path.write_text(
                json.dumps({"type": "turn_context", "payload": {"model": "gpt-5.5"}}) + "\n",
                encoding="utf-8",
            )
            offset = FileOffset(path)

            self.assertTrue(offset.scan())
            self.assertEqual(offset.data.model, "gpt-5.5")

    def test_incremental_scan_reads_codex_prompt(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "session.jsonl"
            path.write_text(
                json.dumps({
                    "type": "event_msg",
                    "payload": {"type": "user_message", "message": "show the active model"},
                }) + "\n",
                encoding="utf-8",
            )
            offset = FileOffset(path)

            self.assertTrue(offset.scan())
            self.assertEqual(offset.data.last_message, "show the active model")

    def test_incremental_scan_reads_codex_usage(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "session.jsonl"
            path.write_text(
                json.dumps({
                    "type": "event_msg",
                    "payload": {
                        "type": "token_count",
                        "info": {
                            "total_token_usage": {"input_tokens": 20000, "output_tokens": 700},
                            "model_context_window": 100000,
                        },
                    },
                }) + "\n",
                encoding="utf-8",
            )
            offset = FileOffset(path)

            self.assertTrue(offset.scan())
            self.assertEqual(offset.data.tokens_in, 20000)
            self.assertEqual(offset.data.tokens_out, 700)
            self.assertEqual(offset.data.context_pct, 20)

    def test_cost_and_path_confinement(self) -> None:
        self.assertAlmostEqual(calculate_cost("opus", 1_000_000, 1_000_000), 90.0)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "projects"
            project = root / "project"
            project.mkdir(parents=True)
            transcript = project / "abc.jsonl"
            transcript.write_text("", encoding="utf-8")
            outside = Path(directory) / "outside.jsonl"
            outside.write_text("", encoding="utf-8")
            self.assertEqual(find_transcript_path("abc", root), transcript)
            self.assertTrue(validate_transcript_path(str(transcript), root))
            self.assertFalse(validate_transcript_path(str(outside), root))

    def test_discovery_sorts_recent_files(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            project = root / "-tmp-project"
            project.mkdir()
            (project / "abc.jsonl").write_text("", encoding="utf-8")
            sessions = FilesystemDiscovery(root).discover()
            self.assertEqual([item.session_id for item in sessions], ["abc"])

    def test_codex_discovery_reads_session_meta_and_index(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sessions_root = root / "sessions"
            day = sessions_root / "2026" / "07" / "13"
            day.mkdir(parents=True)
            index = root / "session_index.jsonl"
            index.write_text(
                json.dumps({"id": "thread-1", "thread_name": "Build keyboard"}) + "\n",
                encoding="utf-8",
            )
            transcript = day / "rollout-2026-07-13T10-00-00-thread-1.jsonl"
            transcript.write_text(
                json.dumps({
                    "type": "session_meta",
                    "payload": {"session_id": "thread-1", "cwd": "/tmp/project"},
                }) + "\n",
                encoding="utf-8",
            )

            sessions = CodexFilesystemDiscovery(sessions_root, index).discover()

            self.assertEqual(len(sessions), 1)
            self.assertEqual(sessions[0].session_id, "thread-1")
            self.assertEqual(sessions[0].project_dir, "Build keyboard")
            self.assertEqual(sessions[0].source, "codex")


class PlatformValidationTests(unittest.TestCase):
    def test_terminal_detection(self) -> None:
        detector = MacTerminalDetector()
        self.assertEqual(detector.detect({"TERM_PROGRAM": "iTerm.app"}).terminal_type, TerminalType.ITERM2)
        self.assertEqual(
            detector.detect({"TERM_PROGRAM": "vscode", "__CFBundleIdentifier": "com.todesktop.Cursor"}).terminal_type,
            TerminalType.CURSOR,
        )

    def test_validation_and_escaping(self) -> None:
        self.assertTrue(validate_tty("/dev/ttys001"))
        self.assertFalse(validate_tty("/dev/ttys001; rm -rf /"))
        self.assertTrue(validate_bundle_id("com.googlecode.iterm2"))
        self.assertFalse(validate_bundle_id("com.example;open"))
        self.assertEqual(escape_jxa_string('a"\\b'), 'a\\"\\\\b')


if __name__ == "__main__":
    unittest.main()

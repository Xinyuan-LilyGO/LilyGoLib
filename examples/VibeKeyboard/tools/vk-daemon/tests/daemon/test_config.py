from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from vibe_keyboard.daemon.config import (
    DaemonConfig,
    config_from_dict,
    load_config,
    save_config,
    set_config_value,
)


class ConfigTests(unittest.TestCase):
    def test_defaults_match_wire_application(self) -> None:
        config = DaemonConfig()
        self.assertEqual(config.general.hook_port, 19280)
        self.assertTrue(config.ble.enabled)
        self.assertEqual(config.ble.scan_timeout_seconds, 5)
        self.assertEqual(config.sound.volume, 80)

    def test_partial_dict_keeps_defaults(self) -> None:
        config = config_from_dict({"sound": {"volume": 60}})
        self.assertEqual(config.sound.volume, 60)
        self.assertTrue(config.sound.enabled)
        self.assertEqual(config.sound.mapping.permission_alert, "builtin:alert")

    def test_legacy_fn_voice_macro_migrates_to_dictation(self) -> None:
        config = config_from_dict({"macros": {"voice": "fn"}})
        self.assertEqual(config.macros.voice, "dictation")

    def test_blank_legacy_macros_fall_back_to_defaults(self) -> None:
        config = config_from_dict({"macros": {"delete": "", "voice": ""}})
        self.assertEqual(config.macros.delete, "ctrl_u")
        self.assertEqual(config.macros.voice, "dictation")

    def test_save_and_load_roundtrip(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "nested" / "config.toml"
            config = DaemonConfig()
            config.yolo.active = True
            config.macros.custom["test"] = "cmd_enter"
            save_config(path, config)
            self.assertEqual(load_config(path), config)
            self.assertFalse(path.with_suffix(".tmp").exists())

    def test_missing_and_invalid_files_return_default(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.toml"
            self.assertEqual(load_config(path), DaemonConfig())
            path.write_text("not valid {{{", encoding="utf-8")
            self.assertEqual(load_config(path), DaemonConfig())

    def test_set_typed_values(self) -> None:
        config = DaemonConfig()
        set_config_value(config, "yolo.active", "true")
        set_config_value(config, "ble.enabled", "false")
        set_config_value(config, "ble.scan_timeout_seconds", "10")
        set_config_value(config, "sound.volume", "42")
        set_config_value(config, "yolo.allow", "Read(*), Write(src/*)")
        self.assertTrue(config.yolo.active)
        self.assertFalse(config.ble.enabled)
        self.assertEqual(config.ble.scan_timeout_seconds, 10)
        self.assertEqual(config.sound.volume, 42)
        self.assertEqual(config.yolo.allow, ["Read(*)", "Write(src/*)"])

    def test_set_rejects_unknown_and_invalid_values(self) -> None:
        config = DaemonConfig()
        with self.assertRaises(KeyError):
            set_config_value(config, "unknown.value", "x")
        with self.assertRaises(ValueError):
            set_config_value(config, "sound.volume", "101")
        self.assertEqual(config.sound.volume, 80)
        with self.assertRaises(ValueError):
            set_config_value(config, "yolo.active", "yes")
        self.assertFalse(config.yolo.active)

    def test_set_rejects_yolo_rules_that_exceed_ble_packet_limit(self) -> None:
        config = DaemonConfig()
        original = list(config.yolo.allow)
        rules = ",".join(f"Read(path-{index}-{'x' * 48})" for index in range(10))

        with self.assertRaisesRegex(ValueError, "cannot exceed 500 bytes"):
            set_config_value(config, "yolo.allow", rules)

        self.assertEqual(config.yolo.allow, original)


if __name__ == "__main__":
    unittest.main()

"""TOML configuration with atomic persistence."""

from __future__ import annotations

import os
import tempfile
import tomllib
from collections.abc import Mapping
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any

YOLO_CONFIG_WIRE_MAX_BYTES = 500


@dataclass(slots=True)
class GeneralConfig:
    hook_port: int = 19280
    log_level: str = "info"


@dataclass(slots=True)
class YoloFileConfig:
    active: bool = False
    allow: list[str] = field(default_factory=lambda: ["Read(*)", "Glob(*)", "Grep(*)"])
    deny: list[str] = field(
        default_factory=lambda: ["Bash(git push*)", "Bash(rm -rf*)", "Bash(sudo*)"]
    )
    notify_auto_allow: bool = True
    auto_allow_log: bool = True


@dataclass(slots=True)
class BleConfig:
    enabled: bool = True
    scan_timeout_seconds: int = 5
    reconnect_delay_seconds: int = 2


@dataclass(slots=True)
class MacroConfig:
    delete: str = "ctrl_u"
    voice: str = "dictation"
    custom: dict[str, str] = field(default_factory=dict)


@dataclass(slots=True)
class AlwaysAllowConfig:
    patterns: list[str] = field(default_factory=list)


@dataclass(slots=True)
class SoundMappingConfig:
    permission_alert: str = "builtin:alert"
    session_complete: str = "builtin:ding"
    error: str = "builtin:buzz"
    click: str = "builtin:click"


@dataclass(slots=True)
class SoundConfig:
    enabled: bool = True
    volume: int = 80
    muted: bool = False
    mapping: SoundMappingConfig = field(default_factory=SoundMappingConfig)


@dataclass(slots=True)
class DaemonConfig:
    general: GeneralConfig = field(default_factory=GeneralConfig)
    yolo: YoloFileConfig = field(default_factory=YoloFileConfig)
    ble: BleConfig = field(default_factory=BleConfig)
    macros: MacroConfig = field(default_factory=MacroConfig)
    always_allow: AlwaysAllowConfig = field(default_factory=AlwaysAllowConfig)
    sound: SoundConfig = field(default_factory=SoundConfig)

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


def default_config_path() -> Path:
    home = Path(os.environ.get("HOME", "/tmp"))
    return home / ".config" / "vk-daemon" / "config.toml"


def _mapping(value: Any) -> Mapping[str, Any]:
    return value if isinstance(value, Mapping) else {}


def _strings(value: Any, default: list[str]) -> list[str]:
    return [str(item) for item in value] if isinstance(value, list) else default


def config_from_dict(raw: Mapping[str, Any]) -> DaemonConfig:
    general = _mapping(raw.get("general"))
    yolo = _mapping(raw.get("yolo"))
    ble = _mapping(raw.get("ble"))
    macros = _mapping(raw.get("macros"))
    always_allow = _mapping(raw.get("always_allow"))
    sound = _mapping(raw.get("sound"))
    sound_mapping = _mapping(sound.get("mapping"))
    defaults = DaemonConfig()
    delete_macro = str(macros.get("delete", defaults.macros.delete)).strip()
    if not delete_macro:
        delete_macro = defaults.macros.delete
    voice_macro = str(macros.get("voice", defaults.macros.voice)).strip()
    if voice_macro in {"", "fn"}:
        voice_macro = "dictation"
    cfg = DaemonConfig(
        general=GeneralConfig(
            hook_port=int(general.get("hook_port", defaults.general.hook_port)),
            log_level=str(general.get("log_level", defaults.general.log_level)),
        ),
        yolo=YoloFileConfig(
            active=bool(yolo.get("active", defaults.yolo.active)),
            allow=_strings(yolo.get("allow"), defaults.yolo.allow),
            deny=_strings(yolo.get("deny"), defaults.yolo.deny),
            notify_auto_allow=bool(
                yolo.get("notify_auto_allow", defaults.yolo.notify_auto_allow)
            ),
            auto_allow_log=bool(yolo.get("auto_allow_log", defaults.yolo.auto_allow_log)),
        ),
        ble=BleConfig(
            enabled=bool(ble.get("enabled", defaults.ble.enabled)),
            scan_timeout_seconds=int(
                ble.get("scan_timeout_seconds", defaults.ble.scan_timeout_seconds)
            ),
            reconnect_delay_seconds=int(
                ble.get("reconnect_delay_seconds", defaults.ble.reconnect_delay_seconds)
            ),
        ),
        macros=MacroConfig(
            delete=delete_macro,
            voice=voice_macro,
            custom={str(key): str(value) for key, value in _mapping(macros.get("custom")).items()},
        ),
        always_allow=AlwaysAllowConfig(
            patterns=_strings(always_allow.get("patterns"), defaults.always_allow.patterns)
        ),
        sound=SoundConfig(
            enabled=bool(sound.get("enabled", defaults.sound.enabled)),
            volume=int(sound.get("volume", defaults.sound.volume)),
            muted=bool(sound.get("muted", defaults.sound.muted)),
            mapping=SoundMappingConfig(
                permission_alert=str(
                    sound_mapping.get(
                        "permission_alert", defaults.sound.mapping.permission_alert
                    )
                ),
                session_complete=str(
                    sound_mapping.get(
                        "session_complete", defaults.sound.mapping.session_complete
                    )
                ),
                error=str(sound_mapping.get("error", defaults.sound.mapping.error)),
                click=str(sound_mapping.get("click", defaults.sound.mapping.click)),
            ),
        ),
    )
    validate_config(cfg)
    return cfg


def validate_config(config: DaemonConfig) -> None:
    if not 1 <= config.general.hook_port <= 65535:
        raise ValueError("general.hook_port must be between 1 and 65535")
    if not 0 <= config.sound.volume <= 100:
        raise ValueError("sound.volume must be between 0 and 100")
    if not 1 <= config.ble.scan_timeout_seconds <= 120:
        raise ValueError("ble.scan_timeout_seconds must be between 1 and 120")
    if not 1 <= config.ble.reconnect_delay_seconds <= 300:
        raise ValueError("ble.reconnect_delay_seconds must be between 1 and 300")
    if len(config.yolo.allow) > 0xFF or len(config.yolo.deny) > 0xFF:
        raise ValueError("YOLO rule lists cannot contain more than 255 items")
    yolo_wire_size = 5 + sum(
        2 + len(rule.encode("utf-8"))
        for rule in (*config.yolo.allow, *config.yolo.deny)
    )
    if yolo_wire_size > YOLO_CONFIG_WIRE_MAX_BYTES:
        raise ValueError(
            f"encoded YOLO configuration cannot exceed {YOLO_CONFIG_WIRE_MAX_BYTES} bytes"
        )


def load_config(path: Path | None = None) -> DaemonConfig:
    path = path or default_config_path()
    try:
        with path.open("rb") as handle:
            raw = tomllib.load(handle)
        return config_from_dict(raw)
    except (OSError, tomllib.TOMLDecodeError, TypeError, ValueError):
        return DaemonConfig()


def _toml_value(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, str):
        escaped = value.replace("\\", "\\\\").replace('"', '\\"')
        return f'"{escaped}"'
    if isinstance(value, int):
        return str(value)
    if isinstance(value, list):
        return "[" + ", ".join(_toml_value(item) for item in value) + "]"
    raise TypeError(f"unsupported TOML value: {type(value).__name__}")


def _to_toml(config: DaemonConfig) -> str:
    data = config.to_dict()
    sections: list[str] = []
    for section_name in [
        "general",
        "yolo",
        "ble",
        "macros",
        "always_allow",
        "sound",
    ]:
        section = dict(data[section_name])
        nested = {key: value for key, value in section.items() if isinstance(value, dict)}
        flat = {key: value for key, value in section.items() if not isinstance(value, dict)}
        sections.append(f"[{section_name}]")
        sections.extend(f"{key} = {_toml_value(value)}" for key, value in flat.items())
        sections.append("")
        for nested_name, nested_values in nested.items():
            sections.append(f"[{section_name}.{nested_name}]")
            sections.extend(
                f"{key} = {_toml_value(value)}" for key, value in nested_values.items()
            )
            sections.append("")
    return "\n".join(sections)


def save_config(path: Path, config: DaemonConfig) -> None:
    validate_config(config)
    path.parent.mkdir(parents=True, exist_ok=True)
    content = _to_toml(config)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            handle.write(content)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
        try:
            directory_fd = os.open(path.parent, os.O_RDONLY)
            try:
                os.fsync(directory_fd)
            finally:
                os.close(directory_fd)
        except OSError:
            pass
    finally:
        temporary.unlink(missing_ok=True)


def set_config_value(config: DaemonConfig, key: str, raw_value: str) -> None:
    parts = key.split(".")
    if len(parts) not in {2, 3} or any(not part or part.startswith("_") for part in parts):
        raise KeyError(key)
    target: Any = config
    for part in parts[:-1]:
        if not hasattr(target, part):
            raise KeyError(key)
        target = getattr(target, part)
    field_name = parts[-1]
    if not hasattr(target, field_name):
        raise KeyError(key)
    current = getattr(target, field_name)
    if isinstance(current, bool):
        normalized = raw_value.lower()
        if normalized not in {"true", "false"}:
            raise ValueError(f"{key} expects true or false")
        value: Any = normalized == "true"
    elif isinstance(current, int):
        value = int(raw_value)
    elif isinstance(current, list):
        value = [item.strip() for item in raw_value.split(",") if item.strip()]
    else:
        value = raw_value
    setattr(target, field_name, value)
    try:
        validate_config(config)
    except (TypeError, ValueError):
        setattr(target, field_name, current)
        raise

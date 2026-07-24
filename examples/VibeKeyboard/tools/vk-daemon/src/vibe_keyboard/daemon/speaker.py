"""Local sound playback backed by the macOS `afplay` utility."""

from __future__ import annotations

import subprocess
import sys
import threading
from pathlib import Path

from ..core import SoundType

_SOUND_FILES = {
    SoundType.PERMISSION_ALERT: "permission_alert.wav",
    SoundType.SESSION_COMPLETE: "session_complete.wav",
    SoundType.ERROR: "error.wav",
    SoundType.CLICK: "click.wav",
}


class LocalSpeaker:
    def __init__(self, assets_dir: Path | None = None) -> None:
        self.assets_dir = assets_dir or Path(__file__).parents[1] / "core" / "assets" / "sounds"
        self._volume = 80
        self._muted = False
        self._lock = threading.Lock()

    @property
    def volume(self) -> int:
        return self._volume

    @property
    def muted(self) -> bool:
        return self._muted

    def set_volume(self, volume: int) -> None:
        if not 0 <= volume <= 100:
            raise ValueError("volume must be between 0 and 100")
        self._volume = volume

    def set_muted(self, muted: bool) -> None:
        self._muted = muted

    def play(self, sound: SoundType) -> None:
        self.play_file(self.assets_dir / _SOUND_FILES[sound])

    def play_by_id(self, sound_id: str) -> None:
        mapping = {
            "builtin:alert": SoundType.PERMISSION_ALERT,
            "builtin:ding": SoundType.SESSION_COMPLETE,
            "builtin:buzz": SoundType.ERROR,
            "builtin:click": SoundType.CLICK,
        }
        sound = mapping.get(sound_id)
        if sound is None:
            raise ValueError(f"unknown sound id: {sound_id}")
        self.play(sound)

    def play_file(self, path: Path) -> None:
        if self._muted or self._volume == 0:
            return
        if not path.is_file():
            raise FileNotFoundError(path)
        if sys.platform != "darwin":
            return
        with self._lock:
            subprocess.Popen(
                ["afplay", "-v", f"{self._volume / 100:.2f}", str(path)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )


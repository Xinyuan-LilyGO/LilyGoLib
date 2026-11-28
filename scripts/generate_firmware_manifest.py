#!/usr/bin/env python3
"""
@file      generate_firmware_manifest.py
@license   MIT
@copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
@date      2026-11-28

Generate the static flash catalog from dated, merged firmware images.
"""

from __future__ import annotations

import argparse
import json
import logging
import os
import re
import tempfile
from datetime import datetime
from pathlib import Path


MANIFEST_NAME = "manifest.json"
LOGGER = logging.getLogger("firmware-manifest")

# Prefixes identify the board, never just the MCU. Keep V1 and V2 separate.
# Variant order is the default order shown in the firmware selector.
DEVICE_RULES = (
    ("lora-pager", "tlora-pager", "ESP32-S3", ("SX1262", "LR1121")),
    ("vibe-pager", "tvibe-pager", "ESP32-S3", ()),
    ("watch-ultra", "twatch-ultra", "ESP32-S3", ("SX1280", "SX1262")),
    ("watch-s3", "twatchs3", "ESP32-S3", ("SX1262", "SX1280", "CC1101")),
    ("t-deck", "tdeck", "ESP32-S3", ("SX1262",)),
    ("vibe-dev", "tvibe-dev", "ESP32-S3", ()),
    ("watch-2020", "q307-twatch2020v3", "ESP32", ()),
    ("watch-2019", "twatch-2019", "ESP32", ()),
    ("t-embed-plus", "tembed-plus", "ESP32-S3", ()),
    ("t-embed-plus-v2", "tembed-plus-v2", "ESP32-S3", ()),
    ("t-embed", "tembed", "ESP32-S3", ()),
)


def valid_merged_image(path: Path, chip: str) -> bool:
    """Check the same merged-image headers required by the browser flasher."""
    size = path.stat().st_size
    if not 0x8002 <= size <= 16 * 1024 * 1024:
        return False
    with path.open("rb") as source:
        header = source.read(0x8002)
    boot = 0x1000 if chip == "ESP32" else 0
    chip_id = 0 if chip == "ESP32" else 9
    return (
        len(header) == 0x8002
        and header[boot] == 0xE9
        and int.from_bytes(header[boot + 12:boot + 14], "little") == chip_id
        and header[0x8000:0x8002] == b"\xaa\x50"
    )


def build_manifest(firmware_dir: Path) -> dict:
    """Select the newest valid date for each exact board/radio combination."""
    if not firmware_dir.is_dir():
        raise FileNotFoundError(f"Firmware directory does not exist: {firmware_dir}")
    files = sorted(
        path for path in firmware_dir.iterdir()
        if path.is_file() and not path.is_symlink() and path.suffix.lower() == ".bin"
    )
    catalog = {}
    matched = set()
    for device_id, prefix, chip, variants in DEVICE_RULES:
        radio = "-(?P<variant>" + "|".join(variants) + ")" if variants else ""
        pattern = re.compile(
            re.escape(prefix) + radio + r"-factory-(?P<date>\d{8})-0x0\.bin",
            re.IGNORECASE,
        )
        newest = {}
        for path in files:
            match = pattern.fullmatch(path.name)
            if not match:
                continue
            matched.add(path)
            try:
                date = datetime.strptime(match["date"], "%Y%m%d").date().isoformat()
            except ValueError:
                LOGGER.warning("Ignoring invalid date: %s", path.name)
                continue
            if not valid_merged_image(path, chip):
                LOGGER.warning("Ignoring invalid %s merged image: %s", chip, path.name)
                continue
            variant = match["variant"].upper() if variants else ""
            entry = {
                "file": path.name,
                "date": date,
                "size": path.stat().st_size,
                "chip": chip,
                "offset": 0,
            }
            if variant:
                entry["variant"] = variant
            if device_id == "t-deck":
                entry["factory"] = True
            previous = newest.get(variant)
            if previous is None or (date, path.name) > (previous["date"], previous["file"]):
                newest[variant] = entry
        catalog[device_id] = [newest[key] for key in variants or ("",) if key in newest]
    for path in files:
        if path not in matched:
            LOGGER.warning("Ignoring unrecognized filename: %s", path.name)
    return {"schemaVersion": 1, "devices": catalog}


def manifest_text(manifest: dict) -> str:
    return json.dumps(manifest, ensure_ascii=False, indent=2) + "\n"


def generate_manifest(firmware_dir: Path) -> dict:
    """Publish a complete JSON atomically, leaving identical output untouched."""
    manifest = build_manifest(firmware_dir)
    output = firmware_dir / MANIFEST_NAME
    text = manifest_text(manifest)
    if output.is_file() and output.read_text(encoding="utf-8") == text:
        return manifest
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=firmware_dir, prefix=".manifest-",
            suffix=".tmp", delete=False,
        ) as target:
            temporary = Path(target.name)
            target.write(text)
        temporary.chmod(0o644)
        os.replace(temporary, output)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate the LilyGo flash firmware manifest")
    parser.add_argument(
        "--firmware-dir", type=Path,
        default=Path(__file__).resolve().parents[1] / "firmware",
        help="firmware directory (default: this library's firmware directory)",
    )
    parser.add_argument("--check", action="store_true", help="check without changing the manifest")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="[firmware-manifest] %(message)s")
    if args.check:
        expected = manifest_text(build_manifest(args.firmware_dir))
        output = args.firmware_dir / MANIFEST_NAME
        if not output.is_file() or output.read_text(encoding="utf-8") != expected:
            LOGGER.error("Manifest is missing or stale; run the generator first")
            return 1
        LOGGER.info("Manifest is up to date")
    else:
        manifest = generate_manifest(args.firmware_dir)
        count = sum(len(entries) for entries in manifest["devices"].values())
        LOGGER.info("Generated %s: %d firmware entries", MANIFEST_NAME, count)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

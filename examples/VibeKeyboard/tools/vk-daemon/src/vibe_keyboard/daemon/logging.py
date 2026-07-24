"""Standard-library logging setup for daemon runtime diagnostics."""

from __future__ import annotations

import logging
import sys
from logging.handlers import RotatingFileHandler
from pathlib import Path

from .config import DaemonConfig, default_config_path

LOGGER_NAME = "vibe_keyboard.daemon"
LOG_FORMAT = "%(asctime)s %(levelname)s %(name)s: %(message)s"


def daemon_log_path() -> Path:
    return default_config_path().parent / "daemon.log"


def log_level_value(level: str) -> int:
    return getattr(logging, level.upper(), logging.INFO)


def configure_logging(config: DaemonConfig) -> Path:
    """Configure stdout and rotating-file daemon logging."""

    log_path = daemon_log_path()
    log_path.parent.mkdir(parents=True, exist_ok=True)

    logger = logging.getLogger(LOGGER_NAME)
    logger.setLevel(log_level_value(config.general.log_level))
    logger.propagate = False

    formatter = logging.Formatter(LOG_FORMAT)
    handlers: list[logging.Handler] = [
        logging.StreamHandler(sys.stdout),
        RotatingFileHandler(log_path, maxBytes=1_000_000, backupCount=3, encoding="utf-8"),
    ]
    for handler in handlers:
        handler.setFormatter(formatter)
        handler.setLevel(logger.level)

    for handler in logger.handlers[:]:
        logger.removeHandler(handler)
        handler.close()
    logger.handlers.extend(handlers)

    return log_path


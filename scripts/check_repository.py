#!/usr/bin/env python3
"""Dependency-free structural checks for a reproducible repository clone."""

import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REQUIRED = (
    "README.md",
    "LICENSE",
    "docs/reproduction.md",
    "docs/hardware_checklist.md",
    "docs/track_mode.md",
    "docs/uart_protocol.md",
    "reproducibility/px4-base.json",
    "reproducibility/track-parameters.nsh",
    "scripts/reproduce_px4.py",
    "px4_tracker_integration/msg/TrackerTarget.msg",
    "px4_tracker_integration/msg/TrackStatus.msg",
    "px4_tracker_integration/src/modules/uart_tracker/UartTracker.cpp",
    "px4_tracker_integration/src/modules/track_control/TrackControl.cpp",
    "tests/test_tracker_protocol.py",
    "tests/test_dashboard_protocol.mjs",
    "tests/test_dashboard_mavlink.mjs",
)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    errors = []
    for relative in REQUIRED:
        if not (ROOT / relative).is_file():
            errors.append("missing {}".format(relative))

    lock_path = ROOT / "reproducibility" / "px4-base.json"
    if lock_path.is_file():
        try:
            lock = json.loads(lock_path.read_text(encoding="utf-8"))
            if len(lock.get("commit", "")) != 40:
                errors.append("px4-base.json does not contain a full commit hash")
            if lock.get("board_target") != "hkust_nxt-dual_default":
                errors.append("unexpected PX4 board target")
        except (ValueError, OSError) as error:
            errors.append("invalid px4-base.json: {}".format(error))

    old_message = ROOT / "px4_tracker_integration" / "msg" / "tracker_target.msg"
    if old_message.exists():
        errors.append("obsolete lowercase tracker_target.msg is present")

    firmware_manifest = ROOT / "firmware" / "README.md"
    firmware_dir = ROOT / "firmware"
    if firmware_manifest.is_file():
        manifest = firmware_manifest.read_text(encoding="utf-8")
        for firmware in firmware_dir.glob("*.px4"):
            digest = sha256(firmware)
            if digest not in manifest:
                errors.append("firmware hash not recorded: {}".format(firmware.name))

    if errors:
        for error in errors:
            print("ERROR:", error, file=sys.stderr)
        return 1

    print("Repository reproducibility check OK")
    print("PX4 base: {}".format(lock["commit"]))
    print("Build target: {}".format(lock["board_target"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Verify a Vulkan validation profile log and its effective settings file."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


EXPECTED: dict[str, dict[str, str]] = {
    "core": {
        "validate_core": "true",
        "validate_sync": "false",
        "syncval_submit_time_validation": "false",
        "validate_best_practices": "false",
        "gpuav_enable": "false",
    },
    "sync": {
        "validate_core": "true",
        "validate_sync": "true",
        "syncval_submit_time_validation": "true",
        "validate_best_practices": "false",
        "gpuav_enable": "false",
    },
    "best-practices": {
        "validate_core": "true",
        "validate_sync": "false",
        "syncval_submit_time_validation": "false",
        "validate_best_practices": "true",
        "gpuav_enable": "false",
    },
    "gpu-av": {
        "validate_core": "true",
        "validate_sync": "false",
        "syncval_submit_time_validation": "false",
        "validate_best_practices": "false",
        "gpuav_enable": "true",
    },
}


def parse_settings(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for raw in path.read_text(encoding="ascii").splitlines():
        line = raw.split("#", 1)[0].strip()
        match = re.fullmatch(r"khronos_validation\.([A-Za-z0-9_]+)\s*=\s*(\S+)", line)
        if match:
            result[match.group(1)] = match.group(2)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=sorted(EXPECTED), required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--settings", type=Path, required=True)
    args = parser.parse_args()

    failures: list[str] = []

    try:
        settings = parse_settings(args.settings)
        log = args.log.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        print(f"Vulkan validation profile verification FAILED: {error}", file=sys.stderr)
        return 1

    expected = EXPECTED[args.profile]
    for key, value in expected.items():
        actual = settings.get(key)
        if actual != value:
            failures.append(f"setting {key}: expected {value}, got {actual!r}")

    if "[Vulkan] validation layer enabled" not in log:
        failures.append("validation layer was not enabled")
    if "[Vulkan] instance created" not in log:
        failures.append("Vulkan instance creation milestone is missing")
    for marker in ("VUID-", "SYNC-HAZARD", "DEVICE_LOST", "[Vulkan] runtime failure"):
        if marker in log:
            failures.append(f"forbidden diagnostic present: {marker}")
    if args.profile == "sync" and "SYNC-HAZARD" in log:
        failures.append("Synchronization Validation reported a hazard")

    if failures:
        print(
            f"Vulkan validation profile verification FAILED ({args.profile})",
            file=sys.stderr,
        )
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print(f"Vulkan validation profile verification passed: {args.profile}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

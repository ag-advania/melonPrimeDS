#!/usr/bin/env python3
"""Compare Metal GPU Display Capture VRAM against the Software u16 result."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", type=Path)
    parser.add_argument(
        "--app",
        type=Path,
        default=Path("build-mac-vulkan/melonPrimeDS.app/Contents/MacOS/melonPrimeDS"),
    )
    parser.add_argument("--seconds", type=float, default=30.0)
    args = parser.parse_args()
    if not args.app.is_file() or not args.rom.is_file():
        parser.error("--app and rom must name existing files")

    environment = os.environ.copy()
    environment["MELONPRIME_CAPTURE_VRAM_DIFFERENTIAL"] = "1"
    environment["MELONPRIME_RASTER_DIFFERENTIAL"] = "1"
    environment["MELONPRIME_FORCE_METAL_COMPUTE_RENDERER"] = "1"
    environment["MELONPRIME_METAL_COMPUTE_VISIBLE"] = "1"
    process = subprocess.Popen(
        [str(args.app.resolve()), "--boot", "always", str(args.rom.resolve())],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=environment,
    )
    try:
        output, _ = process.communicate(timeout=args.seconds)
    except subprocess.TimeoutExpired:
        # Avoid Qt/Cocoa's re-entrant graceful-quit path during diagnostics.
        process.kill()
        output, _ = process.communicate()

    failures: list[str] = []
    required = (
        "metal compute: complete pipeline ready scale=1",
        "metal full-gpu: Phase 8B enabled",
        "metal display capture: configured scale=1",
    )
    for marker in required:
        if marker not in output:
            failures.append(f"missing runtime marker: {marker}")
    if "RasterReference fallback" in output or "using RasterReference" in output:
        failures.append("a RasterReference fallback was observed")
    if "GPU failure" in output or "command failed" in output:
        failures.append("a Metal command-buffer failure was observed")

    raster_records = re.findall(
        r"\[RasterDiff\].*?nonZeroPixels=(\d+).*?mismatchedPixels=(\d+)", output
    )
    if not raster_records:
        failures.append("no supporting native RasterDiff frames were reported")
    elif sum(int(mismatches) for _, mismatches in raster_records):
        failures.append("the supporting native RasterDiff was not exact")

    records = re.findall(
        r"\[CaptureVRAMDiff\].*?words=(\d+).*?mismatchWords=(\d+)", output
    )
    if not records:
        failures.append("the selected ROM/state did not execute Display Capture")
    mismatch_total = sum(int(mismatches) for _, mismatches in records)
    if mismatch_total:
        failures.append(f"CaptureVRAMDiff reported {mismatch_total} mismatched words")
    if process.returncode not in (0, -9):
        failures.append(f"process exited unexpectedly with status {process.returncode}")

    if failures:
        print("FAIL: Metal Display Capture VRAM differential", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        for line in output.splitlines():
            if "CaptureVRAMDiff" in line or "metal display capture" in line:
                print(line, file=sys.stderr)
        return 1

    words = sum(int(word_count) for word_count, _ in records)
    print(
        "PASS: Metal Display Capture VRAM exactly matched Software "
        f"({len(records)} captures, {words} u16 words)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())

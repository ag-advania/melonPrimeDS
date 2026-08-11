#!/usr/bin/env python3
"""Run a real ROM under the dormant 1x Metal Compute/Software differential."""

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
    parser.add_argument("--seconds", type=float, default=20.0)
    args = parser.parse_args()
    if not args.app.is_file() or not args.rom.is_file():
        parser.error("--app and rom must name existing files")

    environment = os.environ.copy()
    environment["MELONPRIME_RASTER_DIFFERENTIAL"] = "1"
    environment["MELONPRIME_FINAL_COMPOSED_DIFFERENTIAL"] = "1"
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
        process.terminate()
        try:
            output, _ = process.communicate(timeout=5.0)
        except subprocess.TimeoutExpired:
            process.kill()
            output, _ = process.communicate()

    failures: list[str] = []
    if "metal compute: complete pipeline ready scale=1" not in output:
        failures.append("Metal Compute foundation did not become ready at 1x")
    if "metal compute visible: CUTOVER active" not in output:
        failures.append("Metal Compute visible CUTOVER was not observed")
    if "RasterReference fallback" in output or "using RasterReference" in output:
        failures.append("a RasterReference fallback was observed")
    if "GPU failure" in output or "command failed" in output:
        failures.append("a Metal command-buffer failure was observed")

    records = re.findall(
        r"\[RasterDiff\].*?nonZeroPixels=(\d+).*?mismatchedPixels=(\d+)", output
    )
    if not records:
        failures.append("no RasterDiff frames were reported")
    elif not any(int(nonzero) > 0 for nonzero, _ in records):
        failures.append("RasterDiff never observed a non-zero 3D frame")
    mismatch_total = sum(int(mismatches) for _, mismatches in records)
    if mismatch_total:
        failures.append(f"RasterDiff reported {mismatch_total} mismatched pixels")

    final_records = re.findall(
        r"\[FinalComposedDiff\].*?pixels=(\d+).*?mismatchedPixels=(\d+)", output
    )
    if not final_records:
        failures.append("no FinalComposedDiff frames were reported")
    final_mismatch_total = sum(int(mismatches) for _, mismatches in final_records)
    if final_mismatch_total:
        failures.append(
            f"FinalComposedDiff reported {final_mismatch_total} mismatched pixels"
        )
    if process.returncode not in (0, -15):
        failures.append(f"process exited unexpectedly with status {process.returncode}")

    if failures:
        print("FAIL: Metal Compute 1x native 3D differential", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        for line in output.splitlines():
            if (
                "RasterDiff" in line
                or "FinalComposedDiff" in line
                or ("metal compute" in line and (
                    "ready" in line or "CUTOVER" in line or "failed" in line
                ))
            ):
                print(line, file=sys.stderr)
        return 1
    print(
        "PASS: Metal Compute 1x native 3D and final composed screens exactly "
        f"matched Software ({len(records)} raster frames, "
        f"{len(final_records)} composed frames)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())

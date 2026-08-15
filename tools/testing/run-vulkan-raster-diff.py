#!/usr/bin/env python3
"""Compare Vulkan's native 1x 3D output against Software for a real ROM."""

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
    parser.add_argument("--state", type=Path)
    parser.add_argument(
        "--custom-hud-off",
        action="store_true",
        help="force Custom HUD and its game patches off for savestate diagnostics",
    )
    args = parser.parse_args()
    if not args.app.is_file() or not args.rom.is_file():
        parser.error("--app and rom must name existing files")
    if args.state is not None and not args.state.is_file():
        parser.error("--state must name an existing savestate")

    environment = os.environ.copy()
    # macOS otherwise block-buffers stdout when the runner captures it; the
    # diagnostic child is killed at the deadline, so buffered evidence would
    # be lost before validation can inspect it.
    environment["NSUnbufferedIO"] = "YES"
    environment["MELONPRIME_RASTER_DIFFERENTIAL"] = "1"
    environment["MELONPRIME_FORCE_VULKAN_RENDERER"] = "1"
    environment["MELONPRIME_TEST_SOFTWARE_OPENGL_DISPLAY_OFF"] = "1"
    if args.state is not None:
        environment["MELONPRIME_TEST_SAVESTATE"] = str(args.state.resolve())
    if args.custom_hud_off:
        environment["MELONPRIME_TEST_CUSTOM_HUD_OFF"] = "1"

    process = subprocess.Popen(
        [str(args.app.resolve()), "--boot", "always", str(args.rom.resolve())],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=environment,
    )
    stopped_by_runner = False
    try:
        output, _ = process.communicate(timeout=args.seconds)
    except subprocess.TimeoutExpired:
        stopped_by_runner = True
        process.kill()
        output, _ = process.communicate()

    failures: list[str] = []
    comparison_output = output
    if args.state is not None:
        expected_state_marker = f"[SavestateDiff] path={args.state.resolve()} loaded=1"
        marker_offset = output.find(expected_state_marker)
        if marker_offset < 0:
            failures.append("the requested savestate was not loaded successfully")
        else:
            comparison_output = output[marker_offset + len(expected_state_marker):]
    if args.custom_hud_off and "[SavestateDiff] customHudForcedOff=1" not in output:
        failures.append("the diagnostic Custom HUD off override was not observed")
    if (
        "[RasterDiffConfig] softwareOpenGLDisplayForcedOff=1 effectiveUseGL=0"
        not in output
    ):
        failures.append("Software OpenGL display was not forced off")
    if "Vulkan renderer init succeeded requested=Vulkan actual=Vulkan" not in output:
        failures.append("Vulkan renderer initialization was not observed")
    if "Renderer fallback requested=Vulkan actual=Software" in output:
        failures.append("Vulkan fell back to Software")
    if "Vulkan runtime failure" in output or "command submission failed" in output:
        failures.append("a Vulkan runtime failure was observed")

    records = re.findall(
        r"\[RasterDiff\] backend=Vulkan .*?nonZeroPixels=(\d+).*?mismatchedPixels=(\d+)",
        comparison_output,
    )
    if not records:
        failures.append("no Vulkan RasterDiff frames were reported")
    elif not any(int(nonzero) > 0 for nonzero, _ in records):
        failures.append("RasterDiff never observed a non-zero 3D frame")
    mismatch_total = sum(int(mismatches) for _, mismatches in records)
    if mismatch_total:
        mismatches_per_frame = [int(mismatches) for _, mismatches in records]
        failures.append(
            f"RasterDiff reported {mismatch_total} mismatched pixels across "
            f"{len(records)} frames ({min(mismatches_per_frame)}-"
            f"{max(mismatches_per_frame)} per frame)"
        )
    if process.returncode not in (0, -15) and not (
        stopped_by_runner and process.returncode == -9
    ):
        failures.append(f"process exited unexpectedly with status {process.returncode}")

    if failures:
        print("FAIL: Vulkan 1x native 3D differential", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        diagnostic_lines = [
            line
            for line in output.splitlines()
            if (
                "RasterDiff" in line
                or "SavestateDiff" in line
                or "renderer" in line.lower()
                or (
                    "Vulkan" in line
                    and any(
                        word in line.lower()
                        for word in (
                            "pipeline",
                            "shader",
                            "failed",
                            "failure",
                            "first frame",
                            "presentation:",
                        )
                    )
                )
            )
        ]
        shown_lines = diagnostic_lines[:80]
        if len(diagnostic_lines) > len(shown_lines):
            omitted = len(diagnostic_lines) - len(shown_lines)
            shown_lines.append(f"... {omitted} diagnostic lines omitted ...")
            shown_lines.extend(diagnostic_lines[-4:])
        for line in shown_lines:
            print(line, file=sys.stderr)
        return 1

    print(
        "PASS: Vulkan 1x native 3D output exactly matched Software "
        f"({len(records)} raster frames)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())

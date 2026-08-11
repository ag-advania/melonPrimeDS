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
    environment["MELONPRIME_RASTER_DIFFERENTIAL"] = "1"
    environment["MELONPRIME_FINAL_COMPOSED_DIFFERENTIAL"] = "1"
    environment["MELONPRIME_FORCE_METAL_COMPUTE_RENDERER"] = "1"
    environment["MELONPRIME_METAL_COMPUTE_VISIBLE"] = "1"
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
        # SIGTERM can enter Qt's macOS quit handler while AppKit holds its
        # menu/window lock, which aborts on recursive os_unfair_lock use.
        # This is an isolated diagnostic child, so stop it directly instead.
        process.kill()
        output, _ = process.communicate()

    failures: list[str] = []
    comparison_output = output
    if args.state is not None:
        expected_state_marker = (
            f"[SavestateDiff] path={args.state.resolve()} loaded=1"
        )
        marker_offset = output.find(expected_state_marker)
        if marker_offset < 0:
            failures.append("the requested savestate was not loaded successfully")
        else:
            comparison_output = output[marker_offset + len(expected_state_marker):]
    if args.custom_hud_off and "[SavestateDiff] customHudForcedOff=1" not in output:
        failures.append("the diagnostic Custom HUD off override was not observed")
    if "metal compute: complete pipeline ready scale=1" not in output:
        failures.append("Metal Compute foundation did not become ready at 1x")
    if "metal compute visible: CUTOVER active" not in output:
        failures.append("Metal Compute visible CUTOVER was not observed")
    fallback_lines = [
        line for line in comparison_output.splitlines()
        if "RasterReference fallback" in line or "using RasterReference" in line
    ]
    unexpected_fallbacks = [
        line for line in fallback_lines
        if args.state is None or "abort=1" not in line
    ]
    if unexpected_fallbacks:
        failures.append("a RasterReference fallback was observed")
    if "GPU failure" in output or "command failed" in output:
        failures.append("a Metal command-buffer failure was observed")
    records = re.findall(
        r"\[RasterDiff\].*?nonZeroPixels=(\d+).*?mismatchedPixels=(\d+)",
        comparison_output,
    )
    if not records:
        failures.append("no RasterDiff frames were reported")
    elif not any(int(nonzero) > 0 for nonzero, _ in records):
        failures.append("RasterDiff never observed a non-zero 3D frame")
    mismatch_total = sum(int(mismatches) for _, mismatches in records)
    if mismatch_total:
        failures.append(f"RasterDiff reported {mismatch_total} mismatched pixels")

    final_records = re.findall(
        r"\[FinalComposedDiff\].*?pixels=(\d+).*?mismatchedPixels=(\d+)",
        comparison_output,
    )
    if not final_records:
        failures.append("no FinalComposedDiff frames were reported")
    final_mismatch_total = sum(int(mismatches) for _, mismatches in final_records)
    if final_mismatch_total:
        failures.append(
            f"FinalComposedDiff reported {final_mismatch_total} mismatched pixels"
        )
    if process.returncode not in (0, -15) and not (
        stopped_by_runner and process.returncode == -9
    ):
        failures.append(f"process exited unexpectedly with status {process.returncode}")

    if failures:
        print("FAIL: Metal Compute 1x native 3D differential", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        diagnostic_lines = [
            line for line in output.splitlines()
            if (
                "RasterDiff" in line
                or "FinalComposedDiff" in line
                or "SavestateDiff" in line
                or ("metal compute" in line and (
                    "ready" in line or "CUTOVER" in line or "failed" in line
                ))
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
        "PASS: Metal Compute 1x native 3D and final composed screens exactly "
        f"matched Software ({len(records)} raster frames, "
        f"{len(final_records)} composed frames"
        + (
            f", {len(fallback_lines)} discarded load-transition frame"
            if fallback_lines else ""
        )
        + ")"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())

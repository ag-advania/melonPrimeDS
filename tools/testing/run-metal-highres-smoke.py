#!/usr/bin/env python3
"""Launch a real ROM at the Metal Compute high-resolution scale matrix."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys


SCALES = (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 16)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", type=Path)
    parser.add_argument(
        "--app",
        type=Path,
        default=Path("build-mac-vulkan/melonPrimeDS.app/Contents/MacOS/melonPrimeDS"),
    )
    parser.add_argument("--seconds", type=float, default=5.0)
    parser.add_argument("--scales", default=",".join(map(str, SCALES)))
    parser.add_argument("--state", type=Path)
    args = parser.parse_args()
    scales = tuple(int(value) for value in args.scales.split(",") if value)
    if not args.app.is_file() or not args.rom.is_file():
        parser.error("--app and rom must name existing files")
    if args.state is not None and not args.state.is_file():
        parser.error("--state must name an existing savestate")

    failures: list[str] = []
    for scale in scales:
        environment = os.environ.copy()
        environment["MELONPRIME_FORCE_METAL_COMPUTE_RENDERER"] = "1"
        environment["MELONPRIME_METAL_COMPUTE_TEST_SCALE"] = str(scale)
        environment["MELONPRIME_METAL_COMPUTE_TRACE_FALLBACKS"] = "1"
        environment["MELONPRIME_TEST_SOFTWARE_OPENGL_DISPLAY_OFF"] = "1"
        environment["NSUnbufferedIO"] = "YES"
        if args.state is not None:
            environment["MELONPRIME_TEST_SAVESTATE"] = str(args.state.resolve())
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
            # SIGTERM enters Qt's Cocoa quit path and can recursively acquire
            # its os_unfair_lock while AppKit is still activating. Diagnostic
            # runners do not need graceful UI teardown, so stop immediately.
            process.kill()
            output, _ = process.communicate()

        expected = (
            "complete pipeline ready scale=1"
            if scale == 1
            else f"scale sync: applied forced scale={scale}"
        )
        cutover = f"CUTOVER active scale={scale}"
        bad = (
            expected not in output
            or cutover not in output
            or "GPU failure" in output
            or "command failed" in output
            or "using RasterReference" in output
            or "RasterReference fallback" in output
            or "[MetalComputeFallbackTrace]" in output
            or "frame input exceeded span budget" in output
            or "texture variants; falling back" in output
            or (
                args.state is not None
                and f"[SavestateDiff] path={args.state.resolve()} loaded=1"
                not in output
            )
        )
        print(f"{'FAIL' if bad else 'PASS'}: Metal Compute scale={scale}")
        if bad:
            failures.append(f"scale {scale}")
            for line in output.splitlines():
                if (
                    "[MetalComputeFallbackTrace]" in line
                    or "[SavestateDiff]" in line
                    or "CUTOVER active" in line
                    or (
                        "[MelonPrime] metal compute" in line
                        and (
                            "failed" in line
                            or "fallback" in line
                            or "ready" in line
                        )
                    )
                ):
                    print(line, file=sys.stderr)

    if failures:
        print("FAIL: high-resolution smoke: " + ", ".join(failures), file=sys.stderr)
        return 1
    print("PASS: Metal Compute high-resolution smoke matrix")
    return 0


if __name__ == "__main__":
    sys.exit(main())

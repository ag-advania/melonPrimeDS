#!/usr/bin/env python3
"""Compile-check script entry for generated Sapphire FrameLatch core (S74-11)."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    cmake = Path("C:/msys64/mingw64/bin/cmake.exe")
    build_dir = REPO_ROOT / "build" / "release-mingw-x86_64"
    if not cmake.is_file() or not build_dir.is_dir():
        print("compile_sapphire_generated_core: skipped (no local MinGW build tree)")
        return 0
    proc = subprocess.run(
        [str(cmake), "--build", str(build_dir), "--target", "sapphire_frame_latch_core", "-j", "1"],
        cwd=REPO_ROOT,
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        print(proc.stdout)
        print(proc.stderr, file=sys.stderr)
        return proc.returncode
    print("compile_sapphire_generated_core: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

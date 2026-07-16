#!/usr/bin/env python3
"""Compare normalized vs exact-pin Sapphire GPU2D cold-start builds (S80-8)."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def run_build(script: Path) -> None:
    subprocess.run([str(script)], check=True, shell=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--normalized-script",
        type=Path,
        default=Path(".claude/skills/build-mingw-vulkan-existing.bat"),
    )
    parser.add_argument(
        "--exact-script",
        type=Path,
        default=Path(".claude/skills/build-mingw-vulkan-sapphire-exact.bat"),
    )
    args = parser.parse_args()

    print("[s80-8] Building normalized GPU2D target")
    run_build(args.normalized_script)
    print("[s80-8] Building exact-pin GPU2D target")
    run_build(args.exact_script)
    print("[s80-8] Compare cold-start crash RVAs between:")
    print("  build/release-mingw-x86_64/melonPrimeDS.exe")
    print("  build/release-mingw-x86_64-sapphire-exact/melonPrimeDS.exe")
    return 0


if __name__ == "__main__":
    sys.exit(main())

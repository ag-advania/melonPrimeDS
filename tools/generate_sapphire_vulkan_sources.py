#!/usr/bin/env python3
"""Unified Sapphire Vulkan source generator entry point (rebuild R3).

Runs all pinned Sapphire desktop generators and optionally verifies output.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
GENERATORS = (
    "generate_sapphire_frame_queue.py",
    "generate_sapphire_frame_latch.py",
)


def run_generator(name: str, verify: bool) -> None:
    script = REPO_ROOT / "tools" / name
    if not script.is_file():
        raise FileNotFoundError(f"missing generator: {script}")
    args = [sys.executable, str(script)]
    if verify:
        args.append("--verify")
    subprocess.run(args, check=True, cwd=REPO_ROOT)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate or verify all Sapphire Vulkan desktop sources."
    )
    parser.add_argument(
        "--verify",
        action="store_true",
        help="Verify generated sources match manifests (no writes).",
    )
    args = parser.parse_args()

    for generator in GENERATORS:
        print(f"[generate_sapphire_vulkan_sources] {'verify' if args.verify else 'generate'} {generator}")
        run_generator(generator, args.verify)

    print("[generate_sapphire_vulkan_sources] ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

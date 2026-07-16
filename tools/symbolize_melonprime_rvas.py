#!/usr/bin/env python3
"""Resolve melonPrimeDS module RVAs with addr2line (S80-3)."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path


DEFAULT_RVAS = [
    "0xF901",
    "0x1D515E",
    "0x1DA6CB",
    "0x6C698",
    "0x71873",
]


def image_base(exe: Path) -> int | None:
    objdump = shutil.which("objdump")
    if objdump is None:
        return None
    output = subprocess.check_output([objdump, "-p", str(exe)], text=True, errors="replace")
    match = re.search(r"ImageBase\s+([0-9a-fA-F]+)", output)
    if not match:
        return None
    return int(match.group(1), 16)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--image-base", type=lambda value: int(value, 0), default=None)
    parser.add_argument("--rva", action="append", default=[])
    args = parser.parse_args()

    exe = args.exe.resolve()
    if not exe.is_file():
        print(f"missing exe: {exe}", file=sys.stderr)
        return 1

    addr2line = shutil.which("addr2line")
    if addr2line is None:
        print("addr2line not found on PATH", file=sys.stderr)
        return 1

    base = args.image_base if args.image_base is not None else image_base(exe)
    if base is None:
        base = 0x140000000
        print(f"[symbolize] ImageBase unavailable; assuming 0x{base:X}")

    rvas = args.rva or DEFAULT_RVAS
    addresses = [hex(base + int(rva, 16)) for rva in rvas]
    cmd = [addr2line, "-e", str(exe), "-f", "-C", "-p", *addresses]
    print("[symbolize]", " ".join(cmd))
    subprocess.run(cmd, check=False)
    return 0


if __name__ == "__main__":
    sys.exit(main())

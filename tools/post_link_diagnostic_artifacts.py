#!/usr/bin/env python3
"""Emit addr2line-ready debug artifacts for MinGW builds (S80-3)."""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(cmd: list[str]) -> None:
    print("[post_link_diagnostic_artifacts]", " ".join(cmd))
    subprocess.run(cmd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--artifact-dir", type=Path, required=True)
    args = parser.parse_args()

    exe = args.exe.resolve()
    if not exe.is_file():
        print(f"[post_link_diagnostic_artifacts] missing exe: {exe}", file=sys.stderr)
        return 1

    artifact_dir = args.artifact_dir.resolve()
    artifact_dir.mkdir(parents=True, exist_ok=True)

    staged_exe = artifact_dir / exe.name
    shutil.copy2(exe, staged_exe)

    debug_exe = artifact_dir / f"{exe.stem}.exe.debug"
    map_file = artifact_dir / f"{exe.stem}.map"

    objcopy = shutil.which("objcopy") or shutil.which("llvm-objcopy")
    if objcopy is not None:
        run([objcopy, "--only-keep-debug", str(staged_exe), str(debug_exe)])
        run([objcopy, "--strip-debug", str(staged_exe)])
        run([objcopy, f"--add-gnu-debuglink={debug_exe.name}", str(staged_exe)])

    sha_path = artifact_dir / "binary.sha256"
    sha_path.write_text(f"{sha256_file(staged_exe)}\n", encoding="utf-8")

    identity_src = exe.parent.parent / "src" / "MelonPrimeGitBuildIdentity.h"
    if identity_src.is_file():
        shutil.copy2(identity_src, artifact_dir / "build-identity.txt")

    if map_file.is_file():
        print(f"[post_link_diagnostic_artifacts] map={map_file}")
    print(f"[post_link_diagnostic_artifacts] staged={staged_exe} debug={debug_exe}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Verify the Nightly Build release payload before anything is published.

The nightly workflow reuses the normal per-OS build workflows, and several of
their jobs run with ``continue-on-error: true`` (Linux/BSD builds and both
``all-artifacts`` bundlers). A failed architecture therefore does not fail the
run, it just silently drops entries from the bundled ``*-all.zip``. Nothing may
touch the existing Nightly release until every expected artifact is present and
intact, so this script is the gate: it checks the files, checks what is inside
each archive, writes ``SHA256SUMS.txt``, and then re-reads that file and
verifies every published asset is listed with a matching digest.

This only inspects the bundles; it never rebuilds one. Producing the
``*-all.zip`` files stays the job of build-macos.yml / build-ubuntu.yml /
build-bsd.yml, and the nightly workflow publishes them byte-for-byte. Reading
the member list is the only way to tell a complete bundle from one that a
continue-on-error job silently truncated, which is why that check lives here
rather than being left to the bundling workflows.

Usage:
    python tools/ci/release/verify-nightly-assets.py --dist dist [--write-sums]

Exit code 0 means the payload is complete and may be published. Any other exit
code means publish must not run.
"""

from __future__ import annotations

import argparse
import hashlib
import sys
import zipfile
from pathlib import Path

SUMS_NAME = "SHA256SUMS.txt"

# Asset name -> archive members that must be present inside it.
# Every entry is required; a missing member means one platform's build did not
# produce output and the bundle is incomplete.
REQUIRED_ASSETS: dict[str, tuple[str, ...]] = {
    "melonPrimeDS-windows-x86_64.zip": (
        "melonPrimeDS.exe",
    ),
    "melonPrimeDS-macOS-all.zip": (
        "melonPrimeDS-macOS-x86_64.zip",
        "melonPrimeDS-macOS-arm64.zip",
        "melonPrimeDS-macOS-universal.zip",
    ),
    "melonPrimeDS-linux-all.zip": (
        "melonPrimeDS-linux-x86_64.zip",
        "melonPrimeDS-linux-aarch64.zip",
        "melonPrimeDS-linux-appimage-x86_64.zip",
        "melonPrimeDS-linux-appimage-aarch64.zip",
    ),
    "melonPrimeDS-bsd-all.zip": (
        "melonPrimeDS-bsd-freebsd-x86_64.zip",
        "melonPrimeDS-bsd-netbsd-x86_64.zip",
        "melonPrimeDS-bsd-openbsd-x86_64.zip",
    ),
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def describe_directory(directory: Path) -> str:
    if not directory.is_dir():
        return f"  (no such directory: {directory})"
    found = sorted(p for p in directory.rglob("*") if p.is_file())
    if not found:
        return "  (directory is empty)"
    return "\n".join(
        f"  {p.relative_to(directory)} ({p.stat().st_size} bytes)" for p in found
    )


def check_asset(directory: Path, name: str, members: tuple[str, ...]) -> list[str]:
    """Return a list of problems found with one asset (empty when it is fine)."""
    problems: list[str] = []
    path = directory / name

    if not path.is_file():
        return [f"{name}: missing"]
    if path.stat().st_size == 0:
        return [f"{name}: file is empty"]

    try:
        with zipfile.ZipFile(path) as archive:
            damaged = archive.testzip()
            if damaged is not None:
                problems.append(f"{name}: corrupt archive member '{damaged}'")

            # Bundles are built with `zip -j`, so members are flat, but compare
            # on the basename anyway to stay robust against a path prefix.
            present = {Path(entry.filename).name for entry in archive.infolist()}
            for member in members:
                if member not in present:
                    problems.append(f"{name}: missing '{member}'")
            for entry in archive.infolist():
                if entry.file_size == 0 and not entry.is_dir():
                    problems.append(f"{name}: '{entry.filename}' is empty")
    except zipfile.BadZipFile as exc:
        problems.append(f"{name}: not a valid ZIP archive ({exc})")

    return problems


def write_sums(directory: Path, names: list[str]) -> Path:
    sums_path = directory / SUMS_NAME
    lines = [f"{sha256(directory / name)}  {name}\n" for name in names]
    # newline="\n": the file is consumed by `sha256sum -c`, which treats a
    # trailing CR as part of the file name. Never let the host platform decide.
    sums_path.write_text("".join(lines), encoding="utf-8", newline="\n")
    return sums_path


def verify_sums(directory: Path, expected_names: list[str]) -> list[str]:
    """Re-read SHA256SUMS.txt and confirm it covers every published asset."""
    problems: list[str] = []
    sums_path = directory / SUMS_NAME

    if not sums_path.is_file():
        return [f"{SUMS_NAME}: missing"]

    listed: dict[str, str] = {}
    for lineno, line in enumerate(
        sums_path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        if not line.strip():
            continue
        parts = line.split(maxsplit=1)
        if len(parts) != 2:
            problems.append(f"{SUMS_NAME}: unparsable line {lineno}: {line!r}")
            continue
        listed[parts[1].strip().lstrip("*")] = parts[0].strip()

    for name in expected_names:
        if name not in listed:
            problems.append(f"{SUMS_NAME}: does not list '{name}'")
            continue
        actual = sha256(directory / name)
        if actual != listed[name]:
            problems.append(
                f"{SUMS_NAME}: digest mismatch for '{name}' "
                f"(listed {listed[name]}, actual {actual})"
            )

    for name in sorted(set(listed) - set(expected_names)):
        problems.append(f"{SUMS_NAME}: lists unexpected file '{name}'")

    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--dist",
        type=Path,
        required=True,
        help="Directory holding exactly the assets to publish.",
    )
    parser.add_argument(
        "--write-sums",
        action="store_true",
        help=f"Generate {SUMS_NAME} from the verified assets before checking it.",
    )
    args = parser.parse_args()

    directory: Path = args.dist
    expected_names = sorted(REQUIRED_ASSETS)

    problems: list[str] = []
    for name in expected_names:
        problems.extend(check_asset(directory, name, REQUIRED_ASSETS[name]))

    if problems:
        print("Nightly payload is incomplete; the release will not be touched.")
        for problem in problems:
            print(f"  ERROR: {problem}")
        print("Files present in the dist directory:")
        print(describe_directory(directory))
        return 1

    # Publishing uploads the whole dist directory, so anything else in it would
    # end up attached to the release without having been checked.
    unexpected = sorted(
        p.name
        for p in directory.iterdir()
        if p.is_file() and p.name not in expected_names and p.name != SUMS_NAME
    )
    if unexpected:
        print("Unexpected extra files staged for publication:")
        for name in unexpected:
            print(f"  ERROR: {name}")
        return 1

    if args.write_sums:
        write_sums(directory, expected_names)

    problems = verify_sums(directory, expected_names)
    if problems:
        print(f"{SUMS_NAME} does not match the payload; the release will not be touched.")
        for problem in problems:
            print(f"  ERROR: {problem}")
        return 1

    print("Nightly payload verified:")
    for name in expected_names:
        size = (directory / name).stat().st_size
        print(f"  OK {name} ({size} bytes)")
    print(f"  OK {SUMS_NAME} covers all {len(expected_names)} assets")
    return 0


if __name__ == "__main__":
    sys.exit(main())

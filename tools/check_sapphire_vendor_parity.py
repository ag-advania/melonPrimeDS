#!/usr/bin/env python3
"""Verify Sapphire vendor file parity for MelonPrimeDS."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = REPO_ROOT / "tools" / "sapphire_vendor_manifest.json"
ADAPTER_BLOCK_RE = re.compile(
    r"// MELONPRIME_DESKTOP_ADAPTER_BEGIN.*?// MELONPRIME_DESKTOP_ADAPTER_END",
    re.DOTALL,
)
SOURCE_HEADER_RE = re.compile(
    r"^// Source: SapphireRhodonite/.*\n(?:// .*\n)*",
    re.MULTILINE,
)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def normalize_vendor_text(text: str) -> str:
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    text = SOURCE_HEADER_RE.sub("", text)
    text = ADAPTER_BLOCK_RE.sub("", text)
    return text


def resolve_reference_root(entry: dict) -> Path | None:
    repo = entry["upstream_repository"]
    if repo.endswith("melonDS-android"):
        env = os.environ.get("SAPPHIRE_ANDROID_ROOT")
        return Path(env) if env else None
    if repo.endswith("melonDS-android-lib"):
        env = os.environ.get("SAPPHIRE_ANDROID_LIB_ROOT")
        return Path(env) if env else None
    return None


def require_upstream(entry: dict, verify_upstream: bool, errors: list[str]) -> Path | None:
    if not verify_upstream:
        return None

    ref_root = resolve_reference_root(entry)
    if ref_root is None:
        errors.append(
            f"{entry['local_path']}: {entry.get('parity_mode')} requires "
            "SAPPHIRE_ANDROID_ROOT or SAPPHIRE_ANDROID_LIB_ROOT"
        )
        return None

    upstream_path = ref_root / entry["upstream_path"]
    if not upstream_path.is_file():
        errors.append(f"missing upstream file: {upstream_path}")
        return None

    return upstream_path


def verify_manifest_upstream_hash(entry: dict, upstream_hash: str, errors: list[str]) -> None:
    expected = entry.get("upstream_sha256")
    if expected and upstream_hash != expected:
        errors.append(
            f"{entry['upstream_path']}: upstream hash drift "
            f"(manifest {expected}, got {upstream_hash})"
        )


def check_entry(entry: dict, verify_upstream: bool) -> list[str]:
    errors: list[str] = []
    local_path = REPO_ROOT / entry["local_path"]
    if not local_path.is_file():
        return [f"missing local file: {entry['local_path']}"]

    local_hash = sha256_file(local_path)
    mode = entry.get("parity_mode", "local_baseline")

    if mode == "local_baseline":
        if local_hash != entry["local_sha256"]:
            errors.append(
                f"{entry['local_path']}: local hash drift "
                f"(expected {entry['local_sha256']}, got {local_hash})"
            )
    elif mode == "exact_upstream":
        upstream_path = require_upstream(entry, verify_upstream, errors)
        if upstream_path is not None:
            upstream_hash = sha256_file(upstream_path)
            verify_manifest_upstream_hash(entry, upstream_hash, errors)
            if local_hash != upstream_hash:
                errors.append(
                    f"{entry['local_path']}: exact_upstream mismatch with "
                    f"{entry['upstream_path']}"
                )
    elif mode == "normalized_upstream":
        upstream_path = require_upstream(entry, verify_upstream, errors)
        if upstream_path is not None:
            upstream_hash = sha256_file(upstream_path)
            verify_manifest_upstream_hash(entry, upstream_hash, errors)

            local_norm = normalize_vendor_text(local_path.read_text(encoding="utf-8"))
            upstream_norm = normalize_vendor_text(upstream_path.read_text(encoding="utf-8"))
            if local_norm != upstream_norm:
                errors.append(
                    f"{entry['local_path']}: normalized_upstream mismatch "
                    f"(local {sha256_bytes(local_norm.encode('utf-8'))}, "
                    f"upstream {sha256_bytes(upstream_norm.encode('utf-8'))})"
                )
    else:
        errors.append(f"{entry['local_path']}: unknown parity mode: {mode}")

    return errors


def check_desktop_adapter_exempt(manifest: dict) -> list[str]:
    errors: list[str] = []
    for rel_path in manifest.get("desktop_adapter_exempt", []):
        if not (REPO_ROOT / rel_path).is_file():
            errors.append(f"missing desktop adapter file: {rel_path}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST,
        help="Path to sapphire_vendor_manifest.json",
    )
    parser.add_argument(
        "--verify-upstream",
        action="store_true",
        help="Also compare exact/normalized upstream clones when available",
    )
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    errors: list[str] = []
    for entry in manifest.get("vendor_files", []):
        errors.extend(check_entry(entry, args.verify_upstream))
    errors.extend(check_desktop_adapter_exempt(manifest))

    if errors:
        print("Sapphire vendor parity check FAILED:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    checked = len(manifest.get("vendor_files", []))
    exempt = len(manifest.get("desktop_adapter_exempt", []))
    print(
        f"Sapphire vendor parity OK ({checked} vendor files, "
        f"{exempt} desktop adapter files listed)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

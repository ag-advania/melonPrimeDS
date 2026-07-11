#!/usr/bin/env python3
"""Hash and pixel-compare Vulkan reference/candidate frame captures."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import tempfile
from pathlib import Path
from typing import NamedTuple


class ImageData(NamedTuple):
    width: int
    height: int
    rgba: bytes


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_raw(path: Path, width: int, height: int, fmt: str) -> ImageData:
    pixels = path.read_bytes()
    expected = width * height * 4
    if len(pixels) != expected:
        raise ValueError(f"{path}: expected {expected} bytes, found {len(pixels)}")
    if fmt == "rgba8":
        return ImageData(width, height, pixels)
    rgba = bytearray(expected)
    for offset in range(0, expected, 4):
        blue, green, red, alpha = pixels[offset : offset + 4]
        rgba[offset : offset + 4] = bytes((red, green, blue, alpha))
    return ImageData(width, height, bytes(rgba))


def load_png(path: Path) -> ImageData:
    try:
        from PIL import Image
    except ImportError as error:
        raise RuntimeError(
            "PNG pixel comparison requires Pillow; use raw RGBA/BGRA or install Pillow"
        ) from error
    with Image.open(path) as image:
        converted = image.convert("RGBA")
        return ImageData(converted.width, converted.height, converted.tobytes())


def load_image(path: Path, width: int | None, height: int | None, fmt: str) -> ImageData:
    if path.suffix.lower() == ".png":
        return load_png(path)
    if width is None or height is None:
        raise ValueError("raw captures require --width and --height")
    return load_raw(path, width, height, fmt)


def compare(reference: ImageData, candidate: ImageData) -> dict[str, object]:
    if reference.width != candidate.width or reference.height != candidate.height:
        return {
            "dimensions_match": False,
            "reference_dimensions": [reference.width, reference.height],
            "candidate_dimensions": [candidate.width, candidate.height],
            "changed_pixels": None,
            "max_channel_delta": None,
        }
    changed_pixels = 0
    max_channel_delta = 0
    channel_delta_sum = [0, 0, 0, 0]
    for offset in range(0, len(reference.rgba), 4):
        ref_pixel = reference.rgba[offset : offset + 4]
        candidate_pixel = candidate.rgba[offset : offset + 4]
        deltas = [abs(a - b) for a, b in zip(ref_pixel, candidate_pixel)]
        if any(deltas):
            changed_pixels += 1
        max_channel_delta = max(max_channel_delta, *deltas)
        for channel, delta in enumerate(deltas):
            channel_delta_sum[channel] += delta
    return {
        "dimensions_match": True,
        "reference_dimensions": [reference.width, reference.height],
        "candidate_dimensions": [candidate.width, candidate.height],
        "changed_pixels": changed_pixels,
        "max_channel_delta": max_channel_delta,
        "channel_delta_sum_rgba": channel_delta_sum,
    }


def run_self_test() -> int:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        reference = root / "reference.rgba"
        identical = root / "identical.rgba"
        changed = root / "changed.rgba"
        reference.write_bytes(bytes((1, 2, 3, 4, 10, 20, 30, 40)))
        identical.write_bytes(reference.read_bytes())
        changed.write_bytes(bytes((1, 2, 3, 4, 10, 20, 35, 40)))
        same = compare(load_raw(reference, 2, 1, "rgba8"), load_raw(identical, 2, 1, "rgba8"))
        different = compare(load_raw(reference, 2, 1, "rgba8"), load_raw(changed, 2, 1, "rgba8"))
        if same["changed_pixels"] != 0 or different["changed_pixels"] != 1:
            return 1
        if different["max_channel_delta"] != 5:
            return 1
    print("compare_renderer_frames self-test: PASS")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", nargs="?", type=Path)
    parser.add_argument("candidate", nargs="?", type=Path)
    parser.add_argument("--width", type=int)
    parser.add_argument("--height", type=int)
    parser.add_argument("--format", choices=("rgba8", "bgra8"), default="rgba8")
    parser.add_argument("--tolerance", type=int, default=0)
    parser.add_argument("--max-diff-pixels", type=int, default=0)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        return run_self_test()
    if args.reference is None or args.candidate is None:
        raise ValueError("reference and candidate paths are required")
    if not 0 <= args.tolerance <= 255:
        raise ValueError("--tolerance must be between 0 and 255")
    if args.max_diff_pixels < 0:
        raise ValueError("--max-diff-pixels must be nonnegative")

    reference = load_image(args.reference, args.width, args.height, args.format)
    candidate = load_image(args.candidate, args.width, args.height, args.format)
    result = compare(reference, candidate)
    result.update(
        {
            "reference": str(args.reference),
            "candidate": str(args.candidate),
            "reference_sha256": sha256(args.reference),
            "candidate_sha256": sha256(args.candidate),
            "tolerance": args.tolerance,
            "max_diff_pixels": args.max_diff_pixels,
        }
    )
    passed = bool(result["dimensions_match"])
    if passed:
        passed = (
            int(result["max_channel_delta"]) <= args.tolerance
            and int(result["changed_pixels"]) <= args.max_diff_pixels
        )
    result["passed"] = passed
    output = json.dumps(result, indent=2, sort_keys=True)
    print(output)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(output + "\n", encoding="utf-8")
    return 0 if passed else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, struct.error) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)

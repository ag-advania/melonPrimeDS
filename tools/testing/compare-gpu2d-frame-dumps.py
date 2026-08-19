#!/usr/bin/env python3
"""Compare independent canonical Software GPU2D frame dumps."""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path


MAGIC = b"MP2DDUMP"
HEADER = struct.Struct("<8sIIIIQQ")
WIDTH = 256
HEIGHT = 192
PIXELS_PER_SCREEN = WIDTH * HEIGHT
RAW_BYTES = PIXELS_PER_SCREEN * 2 * 4
RECORD_BYTES = HEADER.size + RAW_BYTES


def fnv1a64(data: bytes) -> int:
    value = 1469598103934665603
    for byte in data:
        value ^= byte
        value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def read_dump(path: Path) -> list[tuple[tuple[int, int, int, int, int, int], bytes]]:
    data = path.read_bytes()
    if not data:
        raise ValueError(f"{path}: empty dump")
    if len(data) % RECORD_BYTES != 0:
        raise ValueError(
            f"{path}: size {len(data)} is not a multiple of {RECORD_BYTES}"
        )

    records = []
    for offset in range(0, len(data), RECORD_BYTES):
        header_bytes = data[offset : offset + HEADER.size]
        magic, version, frame, width, height, top_hash, bottom_hash = HEADER.unpack(
            header_bytes
        )
        if magic != MAGIC or version != 1 or (width, height) != (WIDTH, HEIGHT):
            raise ValueError(
                f"{path}: invalid header at offset {offset}: "
                f"magic={magic!r} version={version} size={width}x{height}"
            )
        raw = data[offset + HEADER.size : offset + RECORD_BYTES]
        split = PIXELS_PER_SCREEN * 4
        actual_top = fnv1a64(raw[:split])
        actual_bottom = fnv1a64(raw[split:])
        if (top_hash, bottom_hash) != (actual_top, actual_bottom):
            raise ValueError(
                f"{path}: header hash mismatch at frame {frame}: "
                f"header=0x{top_hash:016x}/0x{bottom_hash:016x} "
                f"actual=0x{actual_top:016x}/0x{actual_bottom:016x}"
            )
        records.append(((frame, width, height, top_hash, bottom_hash, offset), raw))
    return records


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument(
        "--baseline-skip",
        type=int,
        default=0,
        help="skip this many leading baseline records before comparing",
    )
    parser.add_argument(
        "--candidate-skip",
        type=int,
        default=0,
        help="skip this many leading candidate records before comparing",
    )
    parser.add_argument(
        "--max-frames",
        type=int,
        default=0,
        help="compare at most this many records after leading skips (0=all)",
    )
    args = parser.parse_args()
    if min(args.baseline_skip, args.candidate_skip, args.max_frames) < 0:
        parser.error("skip and max-frames values must not be negative")

    try:
        baseline = read_dump(args.baseline)[args.baseline_skip :]
        candidate = read_dump(args.candidate)[args.candidate_skip :]
        if args.max_frames:
            baseline = baseline[: args.max_frames]
            candidate = candidate[: args.max_frames]
    except (OSError, ValueError) as error:
        print(f"FAIL: {error}")
        return 1

    mismatches = 0
    first_mismatch = None
    if len(baseline) != len(candidate):
        mismatches += 1
        first_mismatch = f"record-count {len(baseline)} != {len(candidate)}"

    for index, (left, right) in enumerate(zip(baseline, candidate)):
        left_header, left_raw = left
        right_header, right_raw = right
        if left_header[1:5] != right_header[1:5] or left_raw != right_raw:
            mismatches += 1
            if first_mismatch is None:
                if left_raw != right_raw:
                    first_byte = next(
                        (position for position, (a, b) in enumerate(zip(left_raw, right_raw)) if a != b),
                        None,
                    )
                    first_mismatch = (
                        f"record={index} frame={left_header[0]}/{right_header[0]} "
                        f"pixel-byte={first_byte} "
                        f"hash=0x{left_header[3]:016x}/0x{left_header[4]:016x} "
                        f"!=0x{right_header[3]:016x}/0x{right_header[4]:016x}"
                    )
                else:
                    first_mismatch = f"record={index} header differs"

    print(f"baseline={args.baseline}")
    print(f"baseline_sha256={sha256(args.baseline)}")
    print(f"candidate={args.candidate}")
    print(f"candidate_sha256={sha256(args.candidate)}")
    print(f"frames_baseline={len(baseline)}")
    print(f"frames_candidate={len(candidate)}")
    print(f"baseline_skip={args.baseline_skip}")
    print(f"candidate_skip={args.candidate_skip}")
    print(f"max_frames={args.max_frames}")
    print(f"mismatches={mismatches}")
    if first_mismatch is not None:
        print(f"first_mismatch={first_mismatch}")
    print("PASS: exact canonical Top/Bottom frame pixels" if mismatches == 0 else "FAIL: frame pixels differ")
    return 0 if mismatches == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

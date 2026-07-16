#!/usr/bin/env python3
"""Generate deterministic Sapphire latch golden binary fixtures (S75-9/10)."""

from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
FIXTURE_DIR = REPO_ROOT / "tools" / "fixtures"
GOLDEN_BIN = FIXTURE_DIR / "sapphire_static_2d_golden_snapshot.bin"
GOLDEN_META = FIXTURE_DIR / "sapphire_latch_golden_metadata.json"
FLICKER_HASHES = FIXTURE_DIR / "sapphire_static_2d_120frames_hashes.txt"

SCREEN_WIDTH = 256
SCREEN_HEIGHT = 192
PIXEL_COUNT = SCREEN_WIDTH * SCREEN_HEIGHT
LINE_COUNT = SCREEN_HEIGHT
PACKED_STRIDE = SCREEN_WIDTH * 3 + 1


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def build_static_snapshot_bytes() -> bytes:
    """Build a deterministic synthetic packed snapshot payload."""
    parts: list[bytes] = []
    parts.append(struct.pack("<I", 1))  # valid
    parts.append(struct.pack("<i", 0))  # frontBufferLatched
    parts.append(struct.pack("<I", 0))  # screenSwapLatched

    for plane_index in range(6):
        plane = bytearray(PACKED_STRIDE * LINE_COUNT)
        for line in range(LINE_COUNT):
            offset = line * PACKED_STRIDE
            plane[offset] = 0xFF  # opaque black line tag
            for x in range(SCREEN_WIDTH):
                base = offset + 1 + x * 3
                plane[base + 0] = 0x00
                plane[base + 1] = 0x00
                plane[base + 2] = 0x00
            plane[offset + 1 + plane_index] = 0x10 + plane_index
        parts.append(bytes(plane))

    capture3d = bytes((index % 251) for index in range(PIXEL_COUNT))
    line_mask = bytes(1 if (line % 4) == 0 else 0 for line in range(LINE_COUNT))
    fallback_lines = bytes(0 for _ in range(LINE_COUNT))
    stats = struct.pack("<12I", 0, 0, 0, 0, PIXEL_COUNT, 0, 0, 0, 0, 0, 0, 0)

    parts.extend([capture3d, line_mask, fallback_lines, stats, stats])
    return b"".join(parts)


def build_frame_output_hash(frame_index: int, golden_digest: str) -> str:
    payload = f"{golden_digest}:{frame_index}:static2d".encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def main() -> int:
    FIXTURE_DIR.mkdir(parents=True, exist_ok=True)
    snapshot = build_static_snapshot_bytes()
    GOLDEN_BIN.write_bytes(snapshot)
    golden_digest = sha256_bytes(snapshot)

    metadata = {
        "upstreamTag": "0.7.0.rc4",
        "upstreamFrontendCommit": "2c10e59d7209d354e90d9ef4228330bac3f6e794",
        "description": "Deterministic static 2D packed snapshot golden binary",
        "binaryFixture": "sapphire_static_2d_golden_snapshot.bin",
        "binaryByteLength": len(snapshot),
        "binarySha256": golden_digest,
        "compareFields": [
            "packedTopPlane0",
            "packedTopPlane1",
            "packedTopControl",
            "packedTopLineMeta",
            "packedBottomPlane0",
            "packedBottomPlane1",
            "packedBottomControl",
            "packedBottomLineMeta",
            "capture3dSourceDsFrame",
            "captureLineUses3dMask",
            "captureFallbackLines",
            "captureBackedClass4Only",
            "topScreenStats",
            "bottomScreenStats",
        ],
    }
    GOLDEN_META.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")

    frame_hashes = [build_frame_output_hash(index, golden_digest) for index in range(120)]
    FLICKER_HASHES.write_text("\n".join(frame_hashes) + "\n", encoding="utf-8")
    print(f"golden binary bytes={len(snapshot)} sha256={golden_digest}")
    print(f"120-frame digest={sha256_bytes(chr(10).join(frame_hashes).encode('utf-8'))}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

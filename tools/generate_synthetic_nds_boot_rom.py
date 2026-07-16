#!/usr/bin/env python3
"""Generate a license-clear synthetic NDS homebrew ROM for Vulkan cold-start tests."""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

# Standard Nintendo logo (156 bytes) used by retail NDS titles (GBATEK / ndstool).
NINTENDO_LOGO = bytes(
    [
        0x24,
        0xFF,
        0xAE,
        0x51,
        0x69,
        0x9A,
        0xA2,
        0x21,
        0x3D,
        0x84,
        0x82,
        0x0A,
        0x84,
        0xE4,
        0x09,
        0xAD,
        0x11,
        0x24,
        0x8B,
        0x98,
        0xC0,
        0x81,
        0x7F,
        0x21,
        0xA3,
        0x52,
        0xBE,
        0x19,
        0x93,
        0x09,
        0xCE,
        0x20,
        0x10,
        0x46,
        0x4A,
        0x4A,
        0xF8,
        0x27,
        0x31,
        0xEC,
        0x58,
        0xC7,
        0xE8,
        0x33,
        0x82,
        0xE3,
        0xCE,
        0xBF,
        0x85,
        0xF4,
        0xDF,
        0x94,
        0xCE,
        0x4B,
        0x09,
        0xC1,
        0x94,
        0x56,
        0x8A,
        0xC0,
        0x13,
        0x72,
        0xA7,
        0xFC,
        0x9F,
        0x84,
        0x4D,
        0x73,
        0xA3,
        0xCA,
        0x9A,
        0x61,
        0x58,
        0x97,
        0xA3,
        0x60,
        0x87,
        0xA2,
        0x38,
        0x83,
        0x66,
        0x98,
        0xBA,
        0x59,
        0x33,
        0x96,
        0x4E,
        0x12,
        0x73,
        0x4E,
        0xFF,
        0x93,
        0xBC,
        0x63,
        0x96,
        0x00,
        0xD4,
        0x00,
        0x1E,
        0x74,
        0xE9,
        0x2E,
        0x99,
        0x59,
        0x42,
        0x3B,
        0x6E,
        0xCC,
        0xCF,
        0x65,
        0x36,
        0x14,
        0x11,
        0x28,
        0x87,
        0x9A,
        0x55,
        0x44,
        0xE9,
        0x99,
        0x10,
        0x23,
        0xB4,
        0xC9,
        0x27,
        0x93,
        0xBA,
        0xF0,
        0xC0,
        0x82,
        0x8F,
        0x0E,
        0x1E,
        0x00,
        0xA1,
        0xA8,
        0xD4,
        0xF4,
        0xA1,
        0xEB,
        0xB2,
        0xA3,
        0xF3,
        0xC4,
        0x87,
        0xAA,
        0xA8,
        0xB1,
        0x28,
        0x69,
        0x2B,
        0x31,
        0x93,
        0x41,
        0x7F,
        0xA6,
        0x1E,
        0xDA,
        0x58,
        0xAA,
        0xF8,
        0xD8,
        0x81,
        0x88,
        0xBA,
        0x58,
        0x49,
        0x7E,
        0x20,
        0x67,
        0x4F,
        0x4D,
        0x05,
        0x64,
        0x78,
        0x85,
        0xA2,
        0x2C,
        0xD1,
        0xBF,
    ]
)

ARM_INFINITE_LOOP = struct.pack("<I", 0xEA000000)


def crc16_ccitt(data: bytes, init: int = 0xFFFF) -> int:
    crc = init
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def build_synthetic_boot_rom() -> bytes:
    rom_size = 0x8000
    arm9_offset = 0x200
    arm9_size = 0x1000
    arm7_offset = arm9_offset + arm9_size
    arm7_size = 0x1000

    rom = bytearray(rom_size)
    arm_fill = ARM_INFINITE_LOOP * (arm9_size // 4)
    rom[arm9_offset : arm9_offset + arm9_size] = arm_fill
    rom[arm7_offset : arm7_offset + arm7_size] = arm_fill

    header = bytearray(0x1000)
    struct.pack_into("<12s4s2s", header, 0x00, b"SYNTHROM    ", b"####", b"MP")
    header[0x12] = 0x00  # NDS
    header[0x14] = 0x07  # 16MB card size class
    header[0x1F] = 0x01  # autostart

    struct.pack_into("<I", header, 0x20, arm9_offset)
    struct.pack_into("<I", header, 0x24, 0x02000000)
    struct.pack_into("<I", header, 0x28, 0x02000000)
    struct.pack_into("<I", header, 0x2C, arm9_size)
    struct.pack_into("<I", header, 0x30, arm7_offset)
    struct.pack_into("<I", header, 0x34, 0x02380000)
    struct.pack_into("<I", header, 0x38, 0x02380000)
    struct.pack_into("<I", header, 0x3C, arm7_size)
    struct.pack_into("<I", header, 0x80, rom_size)
    struct.pack_into("<I", header, 0x84, 0x4000)

    header[0xC0 : 0xC0 + len(NINTENDO_LOGO)] = NINTENDO_LOGO
    struct.pack_into("<H", header, 0x15C, crc16_ccitt(NINTENDO_LOGO))
    struct.pack_into("<H", header, 0x15E, 0)
    struct.pack_into("<H", header, 0x15E, crc16_ccitt(header[0:0x15E]))

    rom[0:0x1000] = header
    return bytes(rom)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "output",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parent / "fixtures" / "synthetic_vulkan_cold_start.nds",
    )
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    payload = build_synthetic_boot_rom()
    args.output.write_bytes(payload)
    print(f"wrote {args.output} ({len(payload)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Compile every MelonPrime DX12 HLSL tile-geometry variant with fxc.exe.

The renderer compiles its shaders at runtime through d3dcompiler_47.dll, so a
syntax or semantic error in GPU3D_DX12_shaders.h would only surface as a black
screen on a machine with a D3D12 GPU. This script assembles exactly the same
sources DX12Renderer3D::BuildPipeline() builds -- same `#define` prologue, same
Common block, same per-variant defines -- and runs the offline compiler over
them, so the shader set can be validated on any Windows machine with the SDK.

Usage:
    python tools/ci/audits/check-dx12-shaders.py [--scales 1,5,9] [--fxc PATH]

Exits non-zero if any variant fails to compile or emits an HLSL warning. Runtime
shader warnings otherwise flood the emulator log and can conceal undefined
shader data-flow on drivers that optimize the same source differently.
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
SHADER_HEADER = os.path.join(REPO_ROOT, "src", "GPU3D_DX12_shaders.h")

RASTERISE_KINDS = [
    ("NoTexture", ["NoTexture"]),
    ("NoTextureToon", ["NoTexture", "Toon"]),
    ("NoTextureHighlight", ["NoTexture", "Highlight"]),
    ("UseTextureDecal", ["UseTexture", "Decal"]),
    ("UseTextureModulate", ["UseTexture", "Modulate"]),
    ("UseTextureToon", ["UseTexture", "Toon"]),
    ("UseTextureHighlight", ["UseTexture", "Highlight"]),
    ("ShadowMask", ["ShadowMask"]),
]


def find_fxc() -> str | None:
    candidates = sorted(
        glob.glob(r"C:\Program Files (x86)\Windows Kits\10\bin\*\x64\fxc.exe"),
        reverse=True,
    )
    candidates += sorted(
        glob.glob(r"C:\Program Files\Windows Kits\10\bin\*\x64\fxc.exe"),
        reverse=True,
    )
    for path in candidates:
        if os.path.isfile(path):
            return path
    return None


def parse_shaders(path: str) -> dict[str, str]:
    """Pulls every `inline const std::string Name = R"( ... )";` out of the header."""
    with open(path, "r", encoding="utf-8") as handle:
        text = handle.read()

    pattern = re.compile(
        r'inline\s+const\s+std::string\s+(\w+)\s*=\s*R"\((.*?)\)";',
        re.DOTALL,
    )
    shaders = {name: body for name, body in pattern.findall(text)}
    if "Common" not in shaders:
        raise SystemExit(f"{path}: no Common block found (did the header format change?)")
    return shaders


def geometry(scale: int) -> dict[str, int]:
    """Mirrors DX12Renderer3D::SetRenderSettings()'s tile geometry derivation."""
    rng = (1 if scale >= 5 else 0) + (1 if scale >= 9 else 0)
    tile_size = 8 << rng
    coarse_tile_count_y = 4 + ((rng >> 1) << 1)
    clear_local = 64 - ((rng >> 1) << 4)
    return {
        "TileSize": tile_size,
        "CoarseTileCountY": coarse_tile_count_y,
        "CoarseTileArea": 8 * coarse_tile_count_y,
        "ClearCoarseBinMaskLocalSize": clear_local,
    }


def variants() -> list[tuple[str, str, list[str]]]:
    """(display name, shader body key, defines) for every pipeline the renderer builds."""
    out: list[tuple[str, str, list[str]]] = [
        ("ClearCoarseBinMask", "ClearCoarseBinMask", ["ClearCoarseBinMask"]),
        ("ClearIndirectWorkCount", "ClearIndirectWorkCount", ["ClearIndirectWorkCount"]),
        ("CalcOffsets", "CalcOffsets", ["CalculateWorkOffsets"]),
        ("SortWork", "SortWork", ["SortWork"]),
        ("BinCombined", "BinCombined", ["BinCombined"]),
    ]

    for wbuffer in (0, 1):
        suffix = "W" if wbuffer else "Z"
        buf = "WBuffer" if wbuffer else "ZBuffer"
        out.append((f"InterpSpans{suffix}", "InterpSpans", ["InterpSpans", buf]))
    for wbuffer in (0, 1):
        suffix = "W" if wbuffer else "Z"
        buf = "WBuffer" if wbuffer else "ZBuffer"
        out.append((f"DepthBlend{suffix}", "DepthBlend", ["DepthBlend", buf]))
    for kind_name, kind_defines in RASTERISE_KINDS:
        for wbuffer in (0, 1):
            suffix = "W" if wbuffer else "Z"
            buf = "WBuffer" if wbuffer else "ZBuffer"
            out.append(
                (
                    f"Rasterise{kind_name}{suffix}",
                    "Rasterise",
                    ["Rasterise", buf] + kind_defines,
                )
            )

    for variant in range(8):
        defines = ["FinalPass"]
        if variant & 1:
            defines.append("EdgeMarking")
        if variant & 2:
            defines.append("Fog")
        if variant & 4:
            defines.append("AntiAliasing")
        out.append((f"FinalPass{variant}", "FinalPass", defines))

    out.append(("Resolve", "Resolve", ["Resolve"]))
    out.append(("CaptureSidecar", "CaptureSidecar", ["CaptureSidecar"]))
    out.append(("Compositor", "Compositor", ["Compositor"]))
    return out


def build_source(shaders: dict[str, str], geo: dict[str, int], body_key: str, defines: list[str]) -> str:
    lines = [f"#define {name} {value}" for name, value in geo.items()]
    lines += [f"#define {define} 1" for define in defines]
    return "\n".join(lines) + "\n" + shaders["Common"] + shaders[body_key]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scales", default="1,5,9",
                        help="comma-separated internal resolution scales to validate")
    parser.add_argument("--fxc", default=None, help="path to fxc.exe")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    fxc = args.fxc or find_fxc()
    if not fxc:
        print("SKIP: fxc.exe was not found (install the Windows 10/11 SDK to run this audit)")
        return 0

    shaders = parse_shaders(SHADER_HEADER)
    scales = [int(part) for part in args.scales.split(",") if part.strip()]

    failures = 0
    warnings = 0
    compiled = 0

    with tempfile.TemporaryDirectory(prefix="melonprime-dx12-hlsl-") as tmpdir:
        source_path = os.path.join(tmpdir, "shader.hlsl")
        output_path = os.path.join(tmpdir, "shader.dxbc")

        for scale in scales:
            geo = geometry(scale)
            for name, body_key, defines in variants():
                with open(source_path, "w", encoding="utf-8") as handle:
                    handle.write(build_source(shaders, geo, body_key, defines))

                result = subprocess.run(
                    [fxc, "/nologo", "/T", "cs_5_1", "/E", "main", "/O3",
                     "/Ges", "/Fo", output_path, source_path],
                    capture_output=True,
                    text=True,
                )
                compiled += 1
                diagnostics = "\n".join(
                    part.strip() for part in (result.stdout, result.stderr) if part.strip()
                )

                if result.returncode != 0:
                    failures += 1
                    print(f"[FAIL] scale={scale} {name}")
                    print(diagnostics)
                elif re.search(r"\bwarning\s+X\d+", diagnostics, re.IGNORECASE):
                    warnings += 1
                    print(f"[WARN] scale={scale} {name}")
                    print(diagnostics)
                elif args.verbose:
                    size = os.path.getsize(output_path)
                    print(f"[ ok ] scale={scale:2d} {name:28s} {size} bytes")

    if failures or warnings:
        print(
            f"\nFAIL: {failures} compile failures and {warnings} warning-emitting "
            f"variants out of {compiled} DX12 shader variants"
        )
        return 1

    print(f"PASS: all {compiled} DX12 shader variants compiled ({len(scales)} scales)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

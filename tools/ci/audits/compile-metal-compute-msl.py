#!/usr/bin/env python3
"""Extract and offline-compile every embedded Metal Compute MSL library."""

from __future__ import annotations

from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[3]


def extract(path: str, symbol: str) -> str:
    source = (ROOT / path).read_text(encoding="utf-8")
    match = re.search(
        rf"{re.escape(symbol)}\s*=\s*R\"MSL\(\n(.*?)\n\)MSL\";",
        source,
        re.DOTALL,
    )
    if not match:
        raise RuntimeError(f"could not extract {symbol} from {path}")
    return match.group(1) + "\n"


def main() -> int:
    span = extract("src/GPU3D_MetalComputeSpanMath.inc", "kMetalComputeSpanMathSource")
    libraries = {
        "span-bin": span + extract("src/GPU3D_MetalCompute.mm", "kMetalComputeSource"),
        "textured": span + extract(
            "src/GPU3D_MetalComputeTexturedShaders.inc", "kMetalComputeTexturedSource"
        ),
        "depth-blend": extract(
            "src/GPU3D_MetalComputeDepthBlendShaders.inc",
            "kMetalComputeCompleteDepthBlendSource",
        ),
        "final-pass": extract(
            "src/GPU3D_MetalComputeFinalPassShaders.inc", "kMetalComputeFinalPassSource"
        ),
    }

    with tempfile.TemporaryDirectory(prefix="melonprime-msl-") as temporary:
        directory = Path(temporary)
        for name, source in libraries.items():
            metal = directory / f"{name}.metal"
            air = directory / f"{name}.air"
            metal.write_text(source, encoding="utf-8")
            result = subprocess.run(
                ["xcrun", "-sdk", "macosx", "metal", "-c", str(metal), "-o", str(air)],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
            if result.returncode != 0:
                print(f"MSL compile FAILED: {name}\n{result.stdout}", file=sys.stderr)
                return result.returncode
            print(f"PASS: MSL {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""PR-0 static gate: OutputState atomic publication + capture writer tokens.

Fails if HEAD regresses contracts from
docs/plans/melonPrimeDS_develop_metal_完全Metal化_実行指示書_17b46586_2026-07-17.md §6.3.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

FILES = [
    ROOT / "src/GPU_Metal.h",
    ROOT / "src/GPU_Metal.mm",
    ROOT / "src/GPU_MetalCaptureMethods.inc",
    ROOT / "src/GPU_MetalFullGpuMethods.inc",
    ROOT / "src/frontend/qt_sdl/MelonPrimeScreenMetal.mm",
]


def main() -> int:
    issues: list[str] = []

    header = (ROOT / "src/GPU_Metal.h").read_text(encoding="utf-8")
    for name in (
        "LoadOutputState",
        "StoreOutputState",
        "ExchangeOutputState",
        "atomic_load_explicit",
        "atomic_exchange_explicit",
        "atomic_store_explicit",
    ):
        if name not in header:
            issues.append(f"GPU_Metal.h missing {name}")

    capture = (ROOT / "src/GPU_MetalCaptureMethods.inc").read_text(encoding="utf-8")
    for name in ("GpuWritesInFlight", "LatestGpuSubmittedToken", "NextGpuWriteToken"):
        if name not in capture:
            issues.append(f"GPU_MetalCaptureMethods.inc missing {name}")

    # Live (non-comment) use of retired bool name must be zero.
    for path in FILES:
        if not path.exists():
            issues.append(f"missing file {path}")
            continue
        for i, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            stripped = line.lstrip()
            if stripped.startswith("//"):
                continue
            if re.search(r"\bGpuCaptureInFlight\b", line):
                issues.append(f"{path.relative_to(ROOT)}:{i}: live GpuCaptureInFlight")
            if re.search(r"std::exchange\s*\(\s*OutputState\b", line):
                issues.append(
                    f"{path.relative_to(ROOT)}:{i}: std::exchange(OutputState) "
                    "(use ExchangeOutputState)"
                )

    # GetOutput must not return a raw MetalTexture (comments may mention it).
    mm = (ROOT / "src/GPU_Metal.mm").read_text(encoding="utf-8")
    match = re.search(
        r"RendererOutput\s+MetalRenderer::GetOutput\s*\(\s*\)\s*\{(.*?)\n\}",
        mm,
        re.S,
    )
    if not match:
        issues.append("MetalRenderer::GetOutput not found")
    else:
        body_lines = []
        for line in match.group(1).splitlines():
            s = line.lstrip()
            if s.startswith("//"):
                continue
            body_lines.append(line)
        body = "\n".join(body_lines)
        if "MetalTexture" in body:
            issues.append("GetOutput live body returns/constructs MetalTexture")
        # PR-7: MetalRenderer no longer inherits SoftRenderer -- GetOutput()
        # must not fall back to it anymore (empty/None output instead).
        if "SoftRenderer" in body:
            issues.append("GetOutput must not reference SoftRenderer (PR-7 flip)")

    # Every non-helper OutputState use should go through Load/Exchange/Store.
    # Helper implementations themselves may touch &OutputState.
    mm_lines = mm.splitlines()
    for i, line in enumerate(mm_lines, 1):
        s = line.lstrip()
        if s.startswith("//"):
            continue
        if "LoadOutputState" in line or "ExchangeOutputState" in line or "StoreOutputState" in line:
            continue
        if "atomic_load_explicit" in line or "atomic_store_explicit" in line or "atomic_exchange_explicit" in line:
            continue
        if re.search(r"\bOutputState\s*=", line) and "shared_ptr<MetalOutputState> OutputState" not in line:
            issues.append(f"GPU_Metal.mm:{i}: direct OutputState assignment")

    if issues:
        print("FAIL: metal output-state publication audit")
        for item in issues:
            print(f"  - {item}")
        return 1

    print("PASS: metal output-state publication audit")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""
DEPRECATED (S61-4): Do not use.

This script performed blind line-range extraction and text replacement from
Sapphire GPU2D_Soft.cpp. It cannot preserve C++ semantics (CurUnit ownership,
capture timing, structured plane indexing).

Use vendored sources under src/SapphireGPU2DCore/ instead.
See docs/vulkan/SAPPHIRE_UPSTREAM.md.
"""

import sys

print(
    "extract_sapphire_gpu2d_structured.py is deprecated. "
    "Vendor SapphireGPU2DCore/GPU2D_Soft.* from the pinned core commit instead.",
    file=sys.stderr,
)
sys.exit(1)

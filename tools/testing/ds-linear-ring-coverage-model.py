#!/usr/bin/env python3
"""Model the DS-linear polygon ring rebuild against DS span coverage.

This is a *model*, not a source audit: it reimplements the geometry rule that
BuildAcceleratedScene() applies to DS-linear polygons (GPU3D_AcceleratedFrontend.cpp,
`dsGridLinear`) and compares its centre-sampled hardware coverage against the DS span
rule, so the claim in docs/features/rendering/vulkan-graphics-ds-raster-semantics.md
can be re-derived without a GPU.

DS reference (GPU3D_Soft.cpp):
  * scanlines [YTop, YBottom)
  * per scanline the span is [XL, XR] inclusive
  * x-major edges fill their whole horizontal run across the scanline, so the span is
    conservative over [y, y+1]
  * a *vertical* right edge is pushed one pixel back (GPU3D_Soft.cpp:988-990,
    `if (SlopeR.Increment==0 && ...) xend--`), making an axis-aligned span [XL, XR)

Run:  python tools/testing/ds-linear-ring-coverage-model.py
Exit non-zero if an axis-aligned case stops being exact, or if any case regresses
against the pre-change geometry (subpixel positions, no snap, no extension).
"""

from __future__ import annotations

import math
import sys
from fractions import Fraction as F

# name, vertices in ring order, must_be_exact.
# VTop/VBottom and the right-chain direction are derived, not hand-written: getting them
# wrong by hand is exactly what made an earlier revision of this model bless a bad rule.
CASES = [
    ("axis-aligned rect", [(10, 5), (20, 5), (20, 12), (10, 12)], True),
    ("rect, reverse wind", [(10, 5), (10, 12), (20, 12), (20, 5)], True),
    ("1px tall rect", [(4, 6), (9, 6), (9, 7), (4, 7)], True),
    # Vertical right chain => the extension must stay suppressed. Not exact overall: the
    # slanted *left* edge still loses the DS's x-major run fill, which is untouched here.
    ("vert right, slant left", [(12, 4), (20, 4), (20, 11), (7, 11)], False),
    ("pointed-apex tri", [(10, 4), (18, 11), (6, 11)], False),
    ("slanted quad", [(8, 3), (17, 3), (21, 10), (12, 10)], False),
]
SCALES = (1, 2, 4)


def polygon_setup(verts):
    """Reproduce GPU3D.cpp:1263-1286 VTop/VBottom selection, then pick the ring direction
    whose chain from VTop to VBottom carries the right-hand side."""
    vtop, vbot = 0, 0
    ytop, ybot, xbot = 192, 0, 0
    for i, (x, y) in enumerate(verts):
        if y < ytop:
            ytop, vtop = y, i
        if y > ybot or (y == ybot and x > xbot):
            ybot, xbot, vbot = y, x, i

    n = len(verts)
    max_x_index = max(range(n), key=lambda i: verts[i][0])

    def chain(step):
        out, i = [], vtop
        while i != vbot:
            i = (i + step) % n
            out.append(i)
        return out

    for facing in (False, True):
        step = (n - 1) if facing else 1
        if max_x_index in (vtop, vbot) or max_x_index in chain(step):
            return vtop, vbot, facing
    raise AssertionError("no ring direction carries the right-hand chain")


def right_chain_vertical(verts, vtop, vbottom, facing_view):
    """Zero-height edges are skipped: SetupPolygonRightEdge() advances past them, so they
    never become the active SlopeR."""
    n = len(verts)
    step = (n - 1) if facing_view else 1
    i = vtop
    while i != vbottom:
        j = (i + step) % n
        if verts[i][1] != verts[j][1] and verts[i][0] != verts[j][0]:
            return False
        i = j
    return True


def ds_ring(verts, vtop, vbottom, facing_view, scale):
    """Reproduce the ring BuildAcceleratedScene() emits for a DS-linear polygon:
    vertices snapped to the DS pixel grid and, when the right chain is not vertical,
    the right chain shifted by one DS pixel with both shared apexes duplicated (the
    Minkowski sum with a horizontal segment)."""
    n = len(verts)
    step = (n - 1) if facing_view else 1

    def snap(i, extend):
        x, y = verts[i]
        return (F(x * scale + extend), F(y * scale))

    if right_chain_vertical(verts, vtop, vbottom, facing_view):
        return [snap(i, 0) for i in range(n)]

    out = [snap(vtop, 0), snap(vtop, scale)]
    i = (vtop + step) % n
    while i != vbottom:
        out.append(snap(i, scale))
        i = (i + step) % n
    out += [snap(vbottom, scale), snap(vbottom, 0)]
    i = (vbottom + step) % n
    while i != vtop:
        out.append(snap(i, 0))
        i = (i + step) % n
    return out


def poly_span(ring, y):
    """Continuous x-extent of a convex polygon at continuous height y."""
    xs = []
    n = len(ring)
    for k in range(n):
        (x0, y0), (x1, y1) = ring[k], ring[(k + 1) % n]
        if y0 == y1:
            if y0 == y:
                xs += [F(x0), F(x1)]
            continue
        if min(y0, y1) <= y <= max(y0, y1):
            xs.append(F(x0) + F(y - y0, y1 - y0) * (x1 - x0))
    return (min(xs), max(xs)) if xs else None


def hw_cover(ring, w, h):
    """Fragments whose centre lies inside the polygon."""
    out = set()
    for p in range(h):
        span = poly_span(ring, F(2 * p + 1, 2))
        if span is None:
            continue
        lo, hi = span
        out |= {(q, p) for q in range(w) if lo <= F(2 * q + 1, 2) <= hi}
    return out


def ds_cover(verts, vtop, vbottom, facing_view, scale, w, h):
    ring = [(F(x), F(y)) for x, y in verts]
    ytop = min(y for _, y in verts)
    ybot = max(y for _, y in verts)
    vertical_right = right_chain_vertical(verts, vtop, vbottom, facing_view)
    out = set()
    for y in range(ytop, ybot):
        spans = [s for s in (poly_span(ring, F(y)), poly_span(ring, F(min(y + 1, ybot)))) if s]
        if not spans:
            continue
        xl = math.floor(min(s[0] for s in spans))
        xr = math.floor(max(s[1] for s in spans))
        if vertical_right and xr != xl and xr != 0:
            xr -= 1  # GPU3D_Soft.cpp:990
        for x in range(xl, xr + 1):
            for dy in range(scale):
                for dx in range(scale):
                    out.add((x * scale + dx, y * scale + dy))
    return {p for p in out if p[0] < w and p[1] < h}


def main() -> int:
    errors = []
    print(f"{'case':22s} {'scale':>5s} | {'new miss':>8s} {'new extra':>9s} | {'old miss':>8s} {'old extra':>9s}")
    for name, verts, exact in CASES:
        vtop, vbottom, facing = polygon_setup(verts)
        for scale in SCALES:
            w, h = 32 * scale, 20 * scale
            want = ds_cover(verts, vtop, vbottom, facing, scale, w, h)
            new = hw_cover(ds_ring(verts, vtop, vbottom, facing, scale), w, h)
            old = hw_cover([(F(x * scale), F(y * scale)) for x, y in verts], w, h)
            nm, nx = len(want - new), len(new - want)
            om, ox = len(want - old), len(old - want)
            print(f"{name:22s} {scale:5d} | {nm:8d} {nx:9d} | {om:8d} {ox:9d}")
            if exact and (nm or nx):
                errors.append(f"{name} @ {scale}x is no longer exact ({nm} missing, {nx} extra)")
            if nm + nx > om + ox:
                errors.append(f"{name} @ {scale}x regressed ({nm + nx} vs {om + ox} wrong pixels)")

    if errors:
        print("\n".join(["", *errors]), file=sys.stderr)
        return 1
    print("\naxis-aligned cases exact; no case regressed against the pre-change geometry")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

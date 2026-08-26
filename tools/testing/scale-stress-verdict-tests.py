#!/usr/bin/env python3
"""Contract test for the --scale-stress verdict in run-backend-runtime-smoke.py.

The point of --scale-stress is a *live* internal-resolution change inside a
running renderer: the ReleaseOutput -> RecreateOutput pair that a separate
process per scale never reaches. Its first implementation could not tell that
apart from a run where nothing happened, because it counted the driver's
pre-Apply request lines. Cycling "4,4" from a 4x start armed the driver, logged
eight steps, applied nothing, and passed.

These cases pin the distinction against synthetic logs, so the verdict can be
checked without a GPU and so a regression in it fails here rather than silently
turning a runtime gate into a no-op.
"""

from __future__ import annotations

import importlib.util
from pathlib import Path


def load_smoke():
    path = Path(__file__).with_name("run-backend-runtime-smoke.py")
    spec = importlib.util.spec_from_file_location("backend_runtime_smoke", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load smoke runner: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ARMED = ("[switch-stress] armed: 8 switches "
         "(2-step cycle x 4 iterations), 400 ms apart\n")
INIT = "Vulkan renderer init succeeded requested=Vulkan actual=Vulkan\n"
BUILT = ("[Vulkan] internal resolution 4x -> 3D output\n"
         "[Vulkan] internal resolution 5x -> 3D output\n")


def generations(*values: int, epoch: int = 3) -> str:
    return "".join(
        f"Vulkan compositor output created 1024x768 "
        f"resourceGeneration={g} epoch={epoch}\n"
        for g in values)


def summary(requested: int, applied: int, noop: int, failed: int) -> str:
    return (f"[scale-stress] complete: requested={requested} applied={applied} "
            f"noop={noop} failed={failed}, restoring scale 4\n")


def cases() -> list[tuple[str, str, bool]]:
    """(name, synthetic log, must_be_rejected)."""
    healthy_tail = generations(*range(1, 10)) + summary(8, 8, 0, 0)
    return [
        ("healthy full alternating cycle",
         ARMED + INIT + BUILT + healthy_tail, False),

        # A leading no-op is legitimate: the cycle's first element can equal the
        # scale the run started at, and asking for the scale you already have
        # changes nothing.
        ("leading no-op only",
         ARMED + INIT + BUILT
         + "[scale-stress] no-op 1/8: 4x already active\n"
         + generations(*range(1, 9)) + summary(8, 7, 1, 0), False),

        # A renderer torn down and rebuilt gets a fresh composer, so its
        # generation counter legitimately starts again at 1.
        ("renderer restart mid-run",
         ARMED + INIT + BUILT + generations(1, 2, 3)
         + INIT + generations(1, 2, epoch=4) + summary(8, 8, 0, 0), False),

        ("every step a no-op",
         ARMED + INIT + BUILT + generations(1) + summary(8, 0, 8, 0), True),

        ("no-op after the first step",
         ARMED + INIT + BUILT
         + "[scale-stress] no-op 1/8: 4x already active\n"
         + "[scale-stress] no-op 5/8: 4x already active\n"
         + generations(*range(1, 8)) + summary(8, 6, 2, 0), True),

        ("single generation while the summary claims eight applied",
         ARMED + INIT + BUILT + generations(1) + summary(8, 8, 0, 0), True),

        ("generation gap",
         ARMED + INIT + BUILT + generations(1, 2, 4) + summary(8, 8, 0, 0), True),

        ("invoke failure",
         ARMED + INIT + BUILT + generations(1, 2)
         + "[scale-stress] invoke-failed 3/8: onUpdateVideoSettings could not "
           "be invoked; stopping\n"
         + summary(3, 2, 0, 1), True),

        ("cycle never completed",
         ARMED + INIT + BUILT + generations(1, 2, 3), True),

        ("armed eight steps, requested three",
         ARMED + INIT + BUILT + generations(1, 2, 3) + summary(3, 3, 0, 0), True),

        ("a requested scale was never built",
         ARMED + INIT
         + "[Vulkan] internal resolution 4x -> 3D output\n"
         + healthy_tail, True),

        ("driver never armed",
         INIT + BUILT + healthy_tail, True),
    ]


def main() -> int:
    smoke = load_smoke()
    failures: list[str] = []

    for name, log, must_reject in cases():
        reported = smoke.scale_stress_failures(log, "4,5")
        rejected = bool(reported)
        if rejected != must_reject:
            failures.append(
                f"{name}: expected {'rejection' if must_reject else 'acceptance'}, "
                f"got {reported if reported else 'acceptance'}")

    if failures:
        print("scale-stress verdict tests FAILED:")
        for f in failures:
            print(f"  - {f}")
        return 1

    print(f"scale-stress verdict tests PASS ({len(cases())} cases)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

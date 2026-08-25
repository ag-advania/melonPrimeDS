#!/usr/bin/env python3
"""Run a real ROM on the Vulkan and DX12 backends and check what they report.

Compilation and model tests say nothing about whether a frame reached the
screen. This drives the actual binary against a real ROM and a savestate, then
reads the backend's own log markers to answer the questions the SRP audit's
runtime matrix asks: did the requested renderer initialise, did it stay
initialised, did the native GPU2D path compose, did display capture stay on
the native mirror, and did anything fall back to Software.

It does not judge image content -- that needs per-frame comparison at 120fps,
which is a separate harness. What it does catch is a backend that no longer
starts, silently degrades to Software, or trips a runtime failure latch.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# 3D.Renderer values on a Windows build with OpenGL, Vulkan and DX12 enabled.
RENDERER_IDS = {"vulkan": 3, "dx12": 4}

BACKEND_LABEL = {"vulkan": "Vulkan", "dx12": "DX12"}


def write_config(
    source: Path, destination: Path, renderer_id: int, scale: int
) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    text = source.read_text(encoding="utf-8", errors="replace")
    for key, value in (
        ("3D.Renderer", renderer_id),
        # Shared by every hardware backend, not just GL.
        ("3D.GL.ScaleFactor", scale),
    ):
        text, count = re.subn(
            rf"^{re.escape(key)} = \d+$", f"{key} = {value}", text,
            count=1, flags=re.M)
        if count != 1:
            raise SystemExit(f"could not set {key} in {source}")
    destination.write_text(text, encoding="utf-8")


def run_backend(
    backend: str,
    app: Path,
    rom: Path,
    state: Path | None,
    seconds: float,
    workdir: Path,
    stall_frames: int,
) -> tuple[list[str], str]:
    """Returns (failures, captured output)."""
    label = BACKEND_LABEL[backend]

    environment = os.environ.copy()
    environment["MELONPRIME_TEST_SOFTWARE_OPENGL_DISPLAY_OFF"] = "1"
    if stall_frames:
        # Forces the compositor to report SemanticOnly for this many frames.
        # The publication policy must retain the last visible frame and keep
        # capture ownership, never degrade to a Software hybrid frame.
        environment["MELONPRIME_TEST_GPU2D_PRESENTATION_STALL_FRAMES"] = str(
            stall_frames)
    if state is not None:
        environment["MELONPRIME_TEST_SAVESTATE"] = str(state.resolve())
        environment["MELONPRIME_TEST_SAVESTATE_UNPAUSE"] = "1"

    process = subprocess.Popen(
        [str(app.resolve()), "--boot", "always", str(rom.resolve())],
        cwd=str(workdir),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
        env=environment,
    )
    try:
        output, _ = process.communicate(timeout=seconds)
    except subprocess.TimeoutExpired:
        process.terminate()
        try:
            output, _ = process.communicate(timeout=20)
        except subprocess.TimeoutExpired:
            process.kill()
            output, _ = process.communicate()

    failures: list[str] = []

    if f"{label} renderer init succeeded" not in output:
        failures.append(f"{label} renderer initialization was not observed")
    if f"Renderer fallback requested={label} actual=Software" in output:
        failures.append(f"{label} fell back to Software")
    if f"{label} renderer init failed" in output:
        failures.append(f"{label} renderer init reported a failure")
    if f"{label} renderer gpu2d=Software fallback=1 disabled=1" in output:
        failures.append(f"{label} latched a runtime failure and disabled itself")
    if "command submission failed" in output:
        failures.append("a command submission failure was observed")

    if state is not None:
        marker = f"[SavestateDiff] path={state.resolve()} loaded=1"
        if marker not in output:
            failures.append("the requested savestate was not loaded")

    # The native GPU2D path announces itself exactly once when it first
    # composes. Its absence means every frame took the structured or Software
    # route, which is a real regression even when nothing errored.
    if f"{label} renderer gpu2d={label} gpu3d={label} fallback=0" not in output:
        failures.append(f"the native GPU2D path never composed on {label}")

    if stall_frames:
        # Backpressure and SemanticOnly are legitimate outcomes, not failures.
        # A fallback line under injected stall means the retained-result
        # grouping in the publication policy stopped holding.
        for reason in ("native dispatch unavailable", "native frame unavailable"):
            if f"{label} renderer gpu2d=Software fallback=1 reason={reason}" in output:
                failures.append(
                    f"{label} degraded to Software under injected presentation "
                    f"stall (reason={reason})")
        if f"{label} renderer gpu2d=Software fallback=1 reason=stale_generation_reject" in output:
            failures.append(
                f"{label} rejected a generation under injected presentation stall")

    # Only emitted from Stop(), which a terminated process never reaches, so
    # its absence is a property of the runner rather than of the backend.
    counters = re.search(
        r"\[GPU2DFallbackCounters\] backend=" + label + r" (.*)", output
    )
    if counters:
        fields = dict(
            (key, int(value))
            for key, value in re.findall(r"(\w+)=(\d+)", counters.group(1))
        )
        for name in ("stale_generation_reject", "capture_software_fallback"):
            if fields.get(name, 0):
                failures.append(f"{label} reported {name}={fields[name]}")

    return failures, output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", type=Path)
    parser.add_argument(
        "--app", type=Path,
        default=Path("build/release-mingw-x86_64/melonPrimeDS.exe"))
    parser.add_argument(
        "--config", type=Path,
        default=Path("build/release-mingw-x86_64/portable/melonDS.toml"))
    parser.add_argument("--state", type=Path)
    parser.add_argument("--seconds", type=float, default=25.0)
    parser.add_argument(
        "--stall-frames", type=int, default=0,
        help="inject N SemanticOnly compositor frames to exercise the "
             "retained-frame publication policy")
    parser.add_argument(
        "--scales", default="1",
        help="comma-separated internal resolution factors to sweep")
    parser.add_argument("--out", type=Path, default=Path("build/backend-smoke"))
    parser.add_argument(
        "--backends", default="vulkan,dx12",
        help="comma-separated subset of vulkan,dx12")
    args = parser.parse_args()

    if not args.app.is_file():
        parser.error(f"--app does not exist: {args.app}")
    if not args.rom.is_file():
        parser.error(f"rom does not exist: {args.rom}")
    if not args.config.is_file():
        parser.error(f"--config does not exist: {args.config}")
    if args.state is not None and not args.state.is_file():
        parser.error(f"--state does not exist: {args.state}")

    backends = [b.strip() for b in args.backends.split(",") if b.strip()]
    for backend in backends:
        if backend not in RENDERER_IDS:
            parser.error(f"unknown backend: {backend}")

    app = args.app.resolve()
    args.out.mkdir(parents=True, exist_ok=True)

    scales = [int(v) for v in args.scales.split(",") if v.strip()]

    overall: dict[str, list[str]] = {}
    for backend in backends:
      for scale in scales:
        # Each run gets its own portable tree so one run's pipeline cache and
        # config cannot influence the next.
        run_id = f"{backend}-x{scale}"
        if args.stall_frames:
            run_id += f"-stall{args.stall_frames}"
        workdir = (args.out / run_id).resolve()
        if workdir.exists():
            shutil.rmtree(workdir)
        workdir.mkdir(parents=True)
        shutil.copy2(app, workdir / app.name)
        for sibling in app.parent.glob("*.dll"):
            shutil.copy2(sibling, workdir / sibling.name)
        write_config(
            args.config, workdir / "portable" / "melonDS.toml",
            RENDERER_IDS[backend], scale)

        print(f"=== {BACKEND_LABEL[backend]} @ {scale}x:"
              f" running {args.seconds:.0f}s ===", flush=True)
        failures, output = run_backend(
            backend, workdir / app.name, args.rom, args.state,
            args.seconds, workdir, args.stall_frames)
        (args.out / f"{run_id}.log").write_text(output, encoding="utf-8")
        overall[run_id] = failures

        for line in output.splitlines():
            if re.search(
                r"renderer init|gpu2d=|GPU2DFallbackCounters|SavestateDiff|"
                r"runtime failure|Renderer fallback|low-latency", line):
                print("   " + line.strip(), flush=True)

    print()
    failed = False
    for run_id, failures in overall.items():
        if failures:
            failed = True
            print(f"FAIL: {run_id}", file=sys.stderr)
            for failure in failures:
                print(f"  - {failure}", file=sys.stderr)
        else:
            print(f"PASS: {run_id}")
    print(f"logs: {args.out}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())

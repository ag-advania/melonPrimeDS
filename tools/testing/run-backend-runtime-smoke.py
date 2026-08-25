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
import threading
import time
from pathlib import Path

# 3D.Renderer values on a Windows build with OpenGL, Vulkan and DX12 enabled.
RENDERER_IDS = {"vulkan": 3, "dx12": 4}

BACKEND_LABEL = {"vulkan": "Vulkan", "dx12": "DX12"}


def drive_window(pid: int, delay: float) -> None:
    """Resize, maximize, minimize and restore the app's own window.

    The §14 presentation rows are the ones no configuration switch reaches: a
    swapchain has to be told the surface changed, and only the window manager
    can tell it that. Driving the real window is the only way to exercise
    swapchain recreation, and it is what a user does by dragging a corner.
    """
    import ctypes
    from ctypes import wintypes

    user32 = ctypes.WinDLL("user32", use_last_error=True)
    EnumWindows = user32.EnumWindows
    EnumWindows.argtypes = [
        ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM),
        wintypes.LPARAM]

    target: list[int] = []

    def visit(hwnd, _lparam):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            length = user32.GetWindowTextLengthW(hwnd)
            if length > 0:
                target.append(hwnd)
                return False
        return True

    time.sleep(delay)
    EnumWindows(ctypes.WINFUNCTYPE(
        wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(visit), 0)
    if not target:
        print("   [window] no window found; presentation rows not driven",
              flush=True)
        return
    hwnd = target[0]

    SW_MAXIMIZE, SW_RESTORE, SW_MINIMIZE = 3, 9, 6
    for label, action in (
        ("resize 720x540", lambda: user32.MoveWindow(hwnd, 80, 80, 720, 540, True)),
        ("resize 420x760", lambda: user32.MoveWindow(hwnd, 80, 80, 420, 760, True)),
        ("maximize", lambda: user32.ShowWindow(hwnd, SW_MAXIMIZE)),
        ("restore", lambda: user32.ShowWindow(hwnd, SW_RESTORE)),
        ("minimize", lambda: user32.ShowWindow(hwnd, SW_MINIMIZE)),
        ("restore", lambda: user32.ShowWindow(hwnd, SW_RESTORE)),
        ("resize 640x480", lambda: user32.MoveWindow(hwnd, 80, 80, 640, 480, True)),
    ):
        print(f"   [window] {label}", flush=True)
        action()
        time.sleep(1.6)


def write_config(
    source: Path,
    destination: Path,
    renderer_id: int,
    scale: int,
    overrides: dict[str, str],
) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    text = source.read_text(encoding="utf-8", errors="replace")
    settings: list[tuple[str, object]] = [
        ("3D.Renderer", renderer_id),
        # Shared by every hardware backend, not just GL.
        ("3D.GL.ScaleFactor", scale),
    ]
    settings.extend(overrides.items())
    for key, value in settings:
        text, count = re.subn(
            rf"^{re.escape(key)} = .*$", f"{key} = {value}", text,
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
    switch_stress: str,
    switch_iterations: int,
    env_extra: dict[str, str],
    expect_degrade: bool,
    window_actions: bool,
) -> tuple[list[str], str]:
    """Returns (failures, captured output)."""
    label = BACKEND_LABEL[backend]

    environment = os.environ.copy()
    environment["MELONPRIME_TEST_SOFTWARE_OPENGL_DISPLAY_OFF"] = "1"
    environment.update(env_extra)
    if stall_frames:
        # Forces the compositor to report SemanticOnly for this many frames.
        # The publication policy must retain the last visible frame and keep
        # capture ownership, never degrade to a Software hybrid frame.
        environment["MELONPRIME_TEST_GPU2D_PRESENTATION_STALL_FRAMES"] = str(
            stall_frames)
    if switch_stress:
        # Drives the production settings-dialog path that destroys the screen
        # panel and replaces the 3D renderer, which is what exercises output
        # lease invalidation, ring recreation and every component teardown the
        # refactor moved.
        environment["MELONPRIME_RENDERER_SWITCH_STRESS"] = switch_stress
        environment["MELONPRIME_RENDERER_SWITCH_STRESS_ITERATIONS"] = str(
            switch_iterations)
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
    if window_actions:
        threading.Thread(
            target=drive_window, args=(process.pid, 8.0), daemon=True).start()

    try:
        output, _ = process.communicate(timeout=seconds)
    except subprocess.TimeoutExpired:
        # Ask the window to close rather than killing the process. Only a
        # graceful exit runs Renderer::Stop(), which is what emits the GPU2D
        # fallback counters -- the record of which publication paths the run
        # actually took. TerminateProcess would throw that evidence away.
        subprocess.run(
            ["taskkill", "/PID", str(process.pid)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        try:
            output, _ = process.communicate(timeout=25)
        except subprocess.TimeoutExpired:
            process.kill()
            output, _ = process.communicate()

    failures: list[str] = []

    if not expect_degrade:
        if f"{label} renderer init succeeded" not in output:
            failures.append(f"{label} renderer initialization was not observed")
        if f"Renderer fallback requested={label} actual=Software" in output:
            failures.append(f"{label} fell back to Software")
        if f"{label} renderer init failed" in output:
            failures.append(f"{label} renderer init reported a failure")
    disabled_marker = f"{label} renderer gpu2d=Software fallback=1 disabled=1"
    init_fallback = f"Renderer fallback requested={label} actual=Software"
    if expect_degrade:
        # Injected-failure runs assert the opposite: the backend must notice,
        # say so, and hand presentation back cleanly rather than crash, hang or
        # keep showing a frame it can no longer vouch for.
        #
        # There are two legitimate shapes depending on when the fault lands. A
        # fault at initialization never produces a renderer to disable, so the
        # frontend swaps to Software before the first frame; a fault mid-frame
        # disables the live renderer and degrades from there. Requiring only
        # one of them would fail a correct degradation for taking the other
        # route.
        if disabled_marker not in output and init_fallback not in output:
            failures.append(
                f"{label} did not report the injected failure")
        if "Renderer transition complete" not in output:
            failures.append(
                f"{label} reported the failure but never handed over")
    else:
        if disabled_marker in output:
            failures.append(
                f"{label} latched a runtime failure and disabled itself")
        if "command submission failed" in output:
            failures.append("a command submission failure was observed")

    if state is not None:
        marker = f"[SavestateDiff] path={state.resolve()} loaded=1"
        if marker not in output:
            failures.append("the requested savestate was not loaded")

    # The native GPU2D path announces itself exactly once when it first
    # composes. Its absence means every frame took the structured or Software
    # route, which is a real regression even when nothing errored.
    if not expect_degrade             and f"{label} renderer gpu2d={label} gpu3d={label} fallback=0" not in output:
        failures.append(f"the native GPU2D path never composed on {label}")

    if switch_stress:
        armed = re.search(r"\[switch-stress\] armed: (\d+) switches", output)
        if not armed:
            failures.append("the renderer-switch stress driver never armed")
        else:
            performed = re.findall(r"\[switch-stress\] switch (\d+)/(\d+)", output)
            if not performed:
                failures.append("no renderer switch was performed")
            elif int(performed[-1][0]) < int(armed.group(1)):
                failures.append(
                    f"only {performed[-1][0]} of {armed.group(1)} switches ran")
        if "[switch-stress] onUpdateVideoSettings could not be invoked" in output:
            failures.append("a renderer switch could not be invoked")

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
        "--window-actions", action="store_true",
        help="resize, maximize, minimize and restore the app window mid-run")
    parser.add_argument(
        "--expect-degrade", action="store_true",
        help="the run injects a failure; require a clean reported degradation "
             "instead of a healthy frame path")
    parser.add_argument(
        "--env", action="append", default=[], metavar="NAME=VALUE",
        help="extra environment variable for the child; repeatable")
    parser.add_argument(
        "--switch-stress", default="",
        help="comma-separated 3D.Renderer ids to cycle through, e.g. 3,4")
    parser.add_argument("--switch-iterations", type=int, default=6)
    parser.add_argument(
        "--set", action="append", default=[], metavar="KEY=VALUE",
        help="override a melonDS.toml key; repeatable")
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
    env_extra: dict[str, str] = {}
    for item in args.env:
        if "=" not in item:
            parser.error(f"--env needs NAME=VALUE, got {item!r}")
        name, value = item.split("=", 1)
        env_extra[name.strip()] = value.strip()

    overrides: dict[str, str] = {}
    for item in args.set:
        if "=" not in item:
            parser.error(f"--set needs KEY=VALUE, got {item!r}")
        key, value = item.split("=", 1)
        overrides[key.strip()] = value.strip()

    overall: dict[str, list[str]] = {}
    for backend in backends:
      for scale in scales:
        # Each run gets its own portable tree so one run's pipeline cache and
        # config cannot influence the next.
        run_id = f"{backend}-x{scale}"
        if args.stall_frames:
            run_id += f"-stall{args.stall_frames}"
        if args.window_actions:
            run_id += "-window"
        if args.switch_stress:
            run_id += "-switch" + args.switch_stress.replace(",", "")
        for key, value in overrides.items():
            run_id += f"-{key.split('.')[-1]}{value}"
        for name in env_extra:
            run_id += "-" + name.rsplit("_", 1)[-1].lower()
        workdir = (args.out / run_id).resolve()
        if workdir.exists():
            shutil.rmtree(workdir)
        workdir.mkdir(parents=True)
        shutil.copy2(app, workdir / app.name)
        for sibling in app.parent.glob("*.dll"):
            shutil.copy2(sibling, workdir / sibling.name)
        write_config(
            args.config, workdir / "portable" / "melonDS.toml",
            RENDERER_IDS[backend], scale, overrides)

        print(f"=== {BACKEND_LABEL[backend]} @ {scale}x:"
              f" running {args.seconds:.0f}s ===", flush=True)
        failures, output = run_backend(
            backend, workdir / app.name, args.rom, args.state,
            args.seconds, workdir, args.stall_frames,
            args.switch_stress, args.switch_iterations, env_extra,
            args.expect_degrade, args.window_actions)
        (args.out / f"{run_id}.log").write_text(output, encoding="utf-8")
        overall[run_id] = failures

        for line in output.splitlines():
            if re.search(
                r"renderer init|gpu2d=|GPU2DFallbackCounters|SavestateDiff|"
                r"runtime failure|Renderer fallback|low-latency|switch-stress",
                line):
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

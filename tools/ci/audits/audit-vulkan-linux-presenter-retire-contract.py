#!/usr/bin/env python3
"""Audit the Linux Vulkan presenter/lifecycle ownership contract.

This is intentionally a narrow source audit. It checks that renderer-output
quiescence does not fake native presenter retirement, and that every native
retirement path reaches markPresenterRetired only after presenter Shutdown or
an explicit uninitialized-presenter branch.
"""

from __future__ import annotations

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[3]
SCREEN = ROOT / "src" / "frontend" / "qt_sdl" / "MelonPrimeScreenVulkan.cpp"


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing function: {signature}")
    brace = source.find("{", start)
    if brace < 0:
        raise AssertionError(f"missing function body: {signature}")

    depth = 0
    for index in range(brace, len(source)):
        character = source[index]
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    try:
        source = SCREEN.read_text(encoding="utf-8")
    except OSError as error:
        print(f"Linux Vulkan presenter-retire audit FAILED: {error}", file=sys.stderr)
        return 1

    failures: list[str] = []
    try:
        renderer_transition = function_body(
            source,
            "void ScreenPanelVulkan::prepareForRendererTransition(",
        )
        native_retire = function_body(
            source,
            "void ScreenPanelVulkan::retireLinuxPresentationSurface(",
        )
        service_retire = function_body(
            source,
            "void ScreenPanelVulkan::serviceLinuxSurfaceRetire()",
        )
        panel_retire = function_body(
            source,
            "void ScreenPanelVulkan::retireLinuxPresenterForPanelDestruction()",
        )
        destructor = function_body(source, "ScreenPanelVulkan::~ScreenPanelVulkan()")
    except AssertionError as error:
        print(f"Linux Vulkan presenter-retire audit FAILED: {error}", file=sys.stderr)
        return 1

    require(
        "requestRetire()" not in renderer_transition,
        "renderer transition still requests native presenter retirement",
        failures,
    )
    require(
        "markPresenterRetired()" not in renderer_transition,
        "renderer transition still marks the native presenter retired",
        failures,
    )

    shutdown = native_retire.find("vulkan->presenter.Shutdown()")
    mark_retired = native_retire.find("markPresenterRetired()")
    require(
        shutdown >= 0,
        "native retire helper does not call presenter Shutdown",
        failures,
    )
    require(
        mark_retired > shutdown,
        "native retire helper marks retired before presenter Shutdown",
        failures,
    )

    require(
        "vulkan->presenter.IsInitialized()" in service_retire,
        "service retire path does not inspect actual presenter initialization",
        failures,
    )
    require(
        service_retire.count("markPresenterRetired()") == 1,
        "service retire path has an unexpected number of direct retire marks",
        failures,
    )
    require(
        "retireLinuxPresentationSurface(" in service_retire,
        "initialized service path does not use the native retire helper",
        failures,
    )

    panel_shutdown = panel_retire.find("retireLinuxPresentationSurface(")
    panel_wait = panel_retire.find("waitForDestroySafe()")
    require(
        panel_shutdown >= 0,
        "panel destruction has no actual presenter retirement call",
        failures,
    )
    require(
        panel_wait > panel_shutdown,
        "panel destruction does not wait for DestroySafe after retirement",
        failures,
    )

    retire_call = destructor.find("retireLinuxPresenterForPanelDestruction()")
    release_call = destructor.find("releaseNativeSurface()")
    require(
        retire_call >= 0 and release_call > retire_call,
        "panel destruction releases the native child before presenter retirement",
        failures,
    )

    if failures:
        print("Linux Vulkan presenter-retire audit FAILED", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print(
        "Linux Vulkan presenter-retire audit passed: renderer transition preserves "
        "native presenter ownership and destruction retires before child release"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

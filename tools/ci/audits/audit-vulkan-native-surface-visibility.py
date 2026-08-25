#!/usr/bin/env python3
"""Audit the Vulkan native-child visibility lifecycle contract."""

from __future__ import annotations

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[3]
SOURCE = "src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp"


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    source = read(SOURCE)
    failures: list[str] = []

    draw_start = source.index("void ScreenPanelVulkan::drawScreenFrame()")
    draw_end = source.index("\n\nbool ScreenPanelVulkan::buildOsdStrip", draw_start)
    draw = source[draw_start:draw_end]

    # T01/T02: the generic path must request visibility only after a successful
    # EndFrame. The failure block must never expose an unpresented child. Keep
    # the success-path markers separate: the presented frame can be GPU-backed
    # or software-backed, and those cases use different bookkeeping calls.
    end_frame_marker = "if (!vulkan->presenter.EndFrame())"
    presented_marker = "if (gpuFrame)"
    show_marker = "requestNativeSurfaceVisible(true);"
    end_frame_start = draw.find(end_frame_marker)
    presented_start = draw.find(
        presented_marker,
        end_frame_start + len(end_frame_marker) if end_frame_start >= 0 else 0,
    )
    show_start = draw.find(
        show_marker,
        presented_start + len(presented_marker) if presented_start >= 0 else 0,
    )
    end_frame_contract = (
        end_frame_start >= 0
        and presented_start > end_frame_start
        and show_start > presented_start
    )
    require(
        end_frame_contract,
        "successful EndFrame must be followed by the common native-child show request",
        failures,
    )
    if end_frame_contract:
        end_frame_failure = draw[end_frame_start:presented_start]
        successful_present = draw[presented_start:show_start]
        require(
            "return;" in end_frame_failure,
            "EndFrame failure path must return before the success path",
            failures,
        )
        require(
            show_marker not in end_frame_failure,
            "EndFrame failure path must not request native-child visibility",
            failures,
        )
        require(
            "noteFramePresented(" in successful_present
            and "noteFramePresentedWithoutIdentity();" in successful_present,
            "successful EndFrame path must acknowledge both GPU and software frames",
            failures,
        )

    # Non-Linux keeps the child hidden until the first successful present.
    linux_marker = (
        "#if defined(__linux__)  // scatter-budget-exempt: "
        "Linux native-surface lifecycle boundary"
    )
    linux_start = draw.find(linux_marker)
    linux_else = draw.find("#else", linux_start)
    linux_end = draw.find("#endif", linux_else)
    non_linux = (
        draw[linux_else:linux_end]
        if linux_start >= 0 and linux_else >= 0 and linux_end >= 0
        else ""
    )
    require(
        bool(non_linux),
        "non-Linux presenter branch was not found",
        failures,
    )
    if non_linux:
        require(
            "requestNativeSurfaceVisible(true)" not in non_linux,
            "non-Linux path must not show the child before the first successful present",
            failures,
        )

    # T03: normal no-ROM operation exposes the Qt splash/fallback, while the
    # separately gated Linux CI smoke path is intentionally allowed to show.
    no_rom = re.search(
        r"// No ROM:.*?requestNativeSurfaceVisible\(false\);",
        draw,
        re.DOTALL,
    )
    require(no_rom is not None, "normal no-ROM path must hide the native child", failures)
    require(
        "Normal no-ROM operation keeps the native child hidden" in draw,
        "Linux no-ROM smoke exception must remain explicitly documented",
        failures,
    )

    # T04: runtime failure must return ownership to the Qt fallback.
    failure_start = source.index("void ScreenPanelVulkan::reportVulkanRuntimeFailure")
    failure_end = source.index("// Native surface plumbing", failure_start)
    runtime_failure = source[failure_start:failure_end]
    require(
        re.search(
            r"ReportRuntimeFailure\(.*?requestNativeSurfaceVisible\(false\);",
            runtime_failure,
            re.DOTALL,
        )
        is not None,
        "runtime failure must hide the native child",
        failures,
    )

    # T05: Linux must preserve show-before-bind for Wayland/X11 lifecycle
    # publication; the common post-present request is a coalesced confirmation.
    linux_frame = draw[linux_start:linux_else] if linux_start >= 0 and linux_else >= 0 else ""
    require(bool(linux_frame), "Linux frame-admission branch was not found", failures)
    if linux_frame:
        show = linux_frame.find("requestNativeSurfaceVisible(true)")
        bind = linux_frame.find("beginLinuxPresentationFrame()")
        require(
            show >= 0 and bind >= 0 and show < bind,
            "Linux must request visibility before binding the presenter",
            failures,
        )

    # The startup/fallback contract still depends on the constructor hide.
    require(
        "vulkan->surface->hide();" in source,
        "native child must remain hidden during startup/fallback",
        failures,
    )

    if failures:
        print("Vulkan native-surface visibility audit FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print(
        "Vulkan native-surface visibility audit passed: "
        "post-present show, failure/no-ROM hide, and Linux pre-bind show"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

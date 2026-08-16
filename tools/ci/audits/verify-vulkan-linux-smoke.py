#!/usr/bin/env python3
"""Fail-closed validation for the Linux Vulkan native-surface smoke log.

The smoke test is intentionally small, but its useful result is a lifecycle
contract rather than a collection of independent log strings: the first
surface, swapchain, and presenter bind must all use the generation published
after the host completed Show.  This validator keeps that ordering visible and
rejects the old pre-Show admission race.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import sys


@dataclass(frozen=True)
class Milestone:
    name: str
    generation: int
    line: int


def matches(
    lines: list[str], pattern: re.Pattern[str], name: str
) -> list[Milestone]:
    result: list[Milestone] = []
    for line_number, line in enumerate(lines):
        match = pattern.search(line)
        if match:
            result.append(
                Milestone(name, int(match.group("generation")), line_number)
            )
    return result


def main() -> int:
    if len(sys.argv) != 2:
        print(
            "usage: verify-vulkan-linux-smoke.py <vulkan-validation.log>",
            file=sys.stderr,
        )
        return 2

    log_path = Path(sys.argv[1])
    try:
        lines = log_path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as error:
        print(
            f"Linux Vulkan smoke validation FAILED: cannot read {log_path}: {error}",
            file=sys.stderr,
        )
        return 1

    failures: list[str] = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    require(
        any("[Vulkan] validation layer enabled" in line for line in lines),
        "validation layer was not enabled",
    )
    require(
        any(
            re.search(
                r"\[Vulkan\] instance created \(API 1\.1\.0 requested, "
                r"\d+ extensions, 1 layers\)",
                line,
            )
            for line in lines
        ),
        "the expected Vulkan 1.1 instance creation milestone is missing",
    )
    require(
        any("[Vulkan][LinuxWSI] host show requested" in line for line in lines),
        "the Linux native host show request is missing",
    )
    require(
        any("[Vulkan] presentation: requested-vsync=" in line for line in lines),
        "the presentation configuration milestone is missing",
    )
    require(
        any("[Vulkan] presenter ready:" in line for line in lines),
        "the presenter-ready milestone is missing",
    )

    forbidden_diagnostics = (
        "native transition retire timed out",
        "native surface destruction retire timed out",
        "VUID-",
        "Vulkan/validation",
        "SYNC-HAZARD",
        "DEVICE_LOST",
        "[Vulkan] runtime failure",
    )
    for line_number, line in enumerate(lines, start=1):
        for diagnostic in forbidden_diagnostics:
            if diagnostic in line:
                failures.append(
                    f"forbidden diagnostic on line {line_number}: {diagnostic}"
                )

    show_pattern = re.compile(
        r"\[Vulkan\]\[LinuxWSI\] Show generation=(?P<generation>\d+)\b"
    )
    surface_pattern = re.compile(
        r"\[Vulkan\]\[LinuxWSI\] VkSurfaceKHR created "
        r"backend=VK_KHR_(?:xcb|xlib)_surface generation=(?P<generation>\d+)\b"
    )
    swapchain_pattern = re.compile(
        r"\[Vulkan\]\[LinuxWSI\] swapchain ready .*"
        r"generation=(?P<generation>\d+)\b"
    )
    bind_pattern = re.compile(
        r"\[Vulkan\]\[LinuxWSI\] presenter bind "
        r"generation=(?P<generation>\d+)\b"
    )

    shows = matches(lines, show_pattern, "Show")
    surfaces = matches(lines, surface_pattern, "VkSurfaceKHR")
    swapchains = matches(lines, swapchain_pattern, "swapchain")
    binds = matches(lines, bind_pattern, "presenter bind")

    require(bool(shows), "post-Show lifecycle generation milestone is missing")
    require(bool(surfaces), "VkSurfaceKHR creation milestone is missing")
    require(bool(swapchains), "swapchain-ready milestone is missing")
    require(bool(binds), "presenter-bind milestone is missing")

    if shows:
        first_show = shows[0]
        show_request_lines = [
            line_number
            for line_number, line in enumerate(lines)
            if "[Vulkan][LinuxWSI] host show requested" in line
        ]
        require(
            any(line_number < first_show.line for line_number in show_request_lines),
            "host show request must precede the completed Show milestone",
        )

        # No Vulkan surface or presenter milestone may be admitted before the
        # first completed Show. This is the regression that the old grep-based
        # smoke test could not distinguish from a healthy surface creation.
        pre_show = [*surfaces, *swapchains, *binds]
        for milestone in pre_show:
            if milestone.line < first_show.line:
                failures.append(
                    f"{milestone.name} generation {milestone.generation} was admitted "
                    f"before Show generation {first_show.generation} "
                    f"(line {milestone.line + 1})"
                )

        if surfaces and swapchains and binds:
            first_surface = surfaces[0]
            first_swapchain = swapchains[0]
            first_bind = binds[0]
            presentation_lines = [
                line_number
                for line_number, line in enumerate(lines)
                if "[Vulkan] presentation: requested-vsync=" in line
            ]
            require(
                first_surface.line > first_show.line,
                "VkSurfaceKHR creation must follow the completed Show milestone",
            )
            require(
                first_surface.generation == first_show.generation,
                "VkSurfaceKHR must use the post-Show generation",
            )
            require(
                first_swapchain.generation == first_show.generation,
                "swapchain must use the post-Show generation",
            )
            require(
                first_bind.generation == first_show.generation,
                "presenter bind must use the post-Show generation",
            )
            require(
                first_surface.line < first_swapchain.line < first_bind.line,
                "surface, swapchain, and presenter bind milestones are out of order",
            )
            require(
                any(
                    first_surface.line < line_number < first_swapchain.line
                    for line_number in presentation_lines
                ),
                "presentation configuration must be logged while creating the bound swapchain",
            )

            presenter_ready_lines = [
                line_number
                for line_number, line in enumerate(lines)
                if "[Vulkan] presenter ready:" in line
            ]
            require(
                any(line_number > first_bind.line for line_number in presenter_ready_lines),
                "presenter-ready must follow the post-Show presenter bind",
            )

    if failures:
        print(f"Linux Vulkan smoke validation FAILED for {log_path}", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    generation = shows[0].generation
    backend = "VK_KHR_xcb_surface" if any(
        "backend=VK_KHR_xcb_surface" in lines[milestone.line]
        for milestone in surfaces
    ) else "VK_KHR_xlib_surface"
    print(
        "Linux Vulkan smoke validation passed: "
        f"post-Show generation={generation}, backend={backend}, "
        "surface -> swapchain -> presenter bind -> ready"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

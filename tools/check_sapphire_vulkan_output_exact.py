#!/usr/bin/env python3
"""Verify VulkanOutput and FrameQueue bodies against pinned Sapphire (S74-10)."""

from __future__ import annotations

import argparse
import re
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ANDROID = Path(r"C:\Users\Admin\Documents\git\melonDS-android")

COMPARE_FILES = (
    ("app/src/main/cpp/renderer/VulkanOutput.h", "src/frontend/qt_sdl/VulkanReference/VulkanOutput.h"),
    ("app/src/main/cpp/renderer/VulkanOutput.cpp", "src/frontend/qt_sdl/VulkanReference/VulkanOutput.cpp"),
    ("app/src/main/cpp/renderer/FrameQueue.h", "src/frontend/qt_sdl/VulkanReference/FrameQueue.h"),
    ("app/src/main/cpp/renderer/FrameQueue.cpp", "src/frontend/qt_sdl/VulkanReference/FrameQueue.cpp"),
)

STRUCTS = (
    "SoftPackedScreenStats",
    "SoftPackedFrameSnapshot",
    "PreparedSoftPackedFrameDebugView",
    "VulkanCompositionInputs",
    "VulkanOutputTemporalStats",
)

ALLOWED_DESKTOP_DIFF_FUNCTIONS = {
    # Desktop WSI / queue-family / presentation lifecycle (S73-12 / S74-10).
    "VulkanOutput::init",
    "VulkanOutput::shutdown",
    "VulkanOutput::createCompositorResources",
    "VulkanOutput::createAccumulateResources",
    "VulkanOutput::createFrameResource",
    "VulkanOutput::destroyFrameResource",
    "VulkanOutput::ensureFrameResources",
    "VulkanOutput::beginFrameCommand",
    "VulkanOutput::submitFrameCommand",
    "VulkanOutput::prepareFrameForPresentation",
    "VulkanOutput::destroyRenderer3dSnapshot",
    "VulkanOutput::dispatchCompositor",
    "VulkanOutput::validateCompositorSubmission",
    "VulkanOutput::waitForFrame",
    "FrameQueue::clear",
    "FrameQueue::commitPresentedFrame",
    "FrameQueue::deferPresentedFrame",
    "FrameQueue::discardRenderedFrame",
    "FrameQueue::dropPendingFramesToBacklogLocked",
    "FrameQueue::pushRenderedFrame",
    "FrameQueue::rebuildFreeQueueLocked",
    "FrameQueue::recycleRenderFrame",
    "FrameQueue::requestFastForwardPresentationTransition",
    "FrameQueue::requestPresentationResync",
    "FrameQueue::validateRenderFrame",
}


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def normalize_source(text: str) -> str:
    text = re.sub(r"\r\n", "\n", text)
    text = re.sub(r"//.*", "", text)
    text = re.sub(r"/\*[\s\S]*?\*/", "", text)
    text = text.replace("#include <vulkan/vulkan.h>", "#include <volk.h>")
    text = text.replace("renderer/FrameQueue.h", "FrameQueue.h")
    text = text.replace("renderer/VulkanFilterMode.h", "VulkanFilterMode.h")
    text = text.replace(
        "melonDS::VulkanStructuredControlAbi::NativeScreenWidth",
        "256u",
    )
    text = text.replace(
        "melonDS::VulkanStructuredControlAbi::NativeScreenHeight",
        "192u",
    )
    text = text.replace(
        "melonDS::VulkanStructuredControlAbi::PackedScreenStride",
        "(256u * 3u + 1u)",
    )
    text = re.sub(r"\s+", " ", text)
    text = re.sub(r"\(\s+", "(", text)
    text = re.sub(r"\s+\)", ")", text)
    text = re.sub(r",\s+", ",", text)
    return text.strip()


def extract_struct(text: str, name: str) -> str:
    match = re.search(rf"struct {name}\s*\{{([\s\S]*?)\n\s*\}};", text)
    if not match:
        raise AssertionError(f"struct {name} not found")
    body = match.group(1)
    body = re.sub(r"\r\n", "\n", body)
    body = re.sub(r"//.*", "", body)
    body = re.sub(
        r"static constexpr size_t kScreenWidth =[\s\S]*?kLineCount = kScreenHeight;",
        "static constexpr size_t kScreenWidth = 256u;\n    static constexpr size_t kScreenHeight = 192u;\n    static constexpr size_t kPixelCount = kScreenWidth * kScreenHeight;\n    static constexpr size_t kLineCount = kScreenHeight;",
        body,
        count=1,
    )
    lines = [line.strip() for line in body.splitlines() if line.strip()]
    return "\n".join(lines)


def extract_named_function_bodies(text: str) -> dict[str, str]:
    bodies: dict[str, str] = {}
    for match in re.finditer(
        r"(?:bool|void|u32|int|std::[\w:<>,\s]+)\s+([A-Za-z_:][\w:]*)\s*\([^;{]*\)\s*\{",
        text,
    ):
        start = match.start()
        name = match.group(1)
        brace = text.find("{", match.end() - 1)
        if brace == -1:
            continue
        depth = 0
        for index in range(brace, len(text)):
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
                if depth == 0:
                    bodies[name] = normalize_source(text[start : index + 1])
                    break
    return bodies


class VulkanOutputExactTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import os

        android_root = Path(os.environ.get("SAPPHIRE_ANDROID_ROOT", DEFAULT_ANDROID))
        cls.pairs = []
        for upstream_rel, desktop_rel in COMPARE_FILES:
            cls.pairs.append(
                (
                    read(android_root / upstream_rel),
                    read(REPO_ROOT / desktop_rel),
                    upstream_rel,
                )
            )
        cls.android_header, cls.desktop_header, _ = cls.pairs[0]

    def test_soft_packed_snapshot_exact(self):
        self.assertEqual(
            extract_struct(self.android_header, "SoftPackedFrameSnapshot"),
            extract_struct(self.desktop_header, "SoftPackedFrameSnapshot"),
        )

    def test_required_structs_present(self):
        for name in STRUCTS:
            self.assertIn(f"struct {name}", self.desktop_header)

    def test_compare_files_have_matching_composition_bodies(self):
        mismatches: list[str] = []
        for upstream_text, desktop_text, label in self.pairs:
            upstream_bodies = extract_named_function_bodies(upstream_text)
            desktop_bodies = extract_named_function_bodies(desktop_text)
            for name, upstream_body in sorted(upstream_bodies.items()):
                if name in ALLOWED_DESKTOP_DIFF_FUNCTIONS:
                    continue
                if name not in desktop_bodies:
                    mismatches.append(f"{label}: missing function {name}")
                    continue
                if upstream_body != desktop_bodies[name]:
                    mismatches.append(f"{label}: body mismatch {name}")
        self.assertEqual(mismatches, [])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--upstream-tag", default="0.7.0.rc4")
    args = parser.parse_args()
    if args.upstream_tag != "0.7.0.rc4":
        print(f"unsupported tag {args.upstream_tag}", file=sys.stderr)
        return 2
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(VulkanOutputExactTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())

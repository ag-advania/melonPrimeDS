#!/usr/bin/env python3
"""Verify VulkanOutput composition regions against pinned Sapphire (S75-8)."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ANDROID = Path(r"C:\Users\Admin\Documents\git\melonDS-android")
MANIFEST_PATH = REPO_ROOT / "tools" / "sapphire_vulkan_output_vendor_manifest.json"
DESKTOP_VULKAN_OUTPUT_CPP = REPO_ROOT / "src" / "frontend" / "qt_sdl" / "VulkanReference" / "VulkanOutput.cpp"
DESKTOP_VULKAN_OUTPUT_H = REPO_ROOT / "src" / "frontend" / "qt_sdl" / "VulkanReference" / "VulkanOutput.h"

STRUCTS = (
    "SoftPackedScreenStats",
    "SoftPackedFrameSnapshot",
    "PreparedSoftPackedFrameDebugView",
    "VulkanCompositionInputs",
    "VulkanOutputTemporalStats",
)

REGION_FUNCTION_NAMES = {
    "prepare_frame_for_presentation": "VulkanOutput::prepareFrameForPresentation",
    "update_prepared_capture3d_source": "VulkanOutput::updatePreparedCapture3dSource",
    "build_composition_inputs": "VulkanOutput::buildCompositionInputs",
}


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def load_manifest() -> dict:
    return json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))


def android_root() -> Path:
    import os

    env = os.environ.get("SAPPHIRE_ANDROID_ROOT")
    return Path(env) if env else DEFAULT_ANDROID


def normalize_source(text: str) -> str:
    text = re.sub(r"\r\n", "\n", text)
    text = re.sub(r"//.*", "", text)
    text = re.sub(r"/\*[\s\S]*?\*/", "", text)
    text = re.sub(
        r"if\s*\(\s*!waitBeforePackedBufferOverwrite\([^)]*\)\s*\)\s*return\s+false\s*;",
        "",
        text,
    )
    text = re.sub(
        r"melonDS::VulkanR24Barrier::HostWriteToShaderRead\([^;]*\)\s*;",
        "__PLATFORM_BUFFER_BARRIER__;",
        text,
    )
    text = re.sub(
        r"melonDS::VulkanR24Barrier::CompositionWriteToPresenterRead\([^;]*\)\s*;",
        "__PLATFORM_OUTPUT_BARRIER__;",
        text,
    )
    text = re.sub(
        r"VkBufferMemoryBarrier\s+\w+\s*\{[\s\S]*?\}\s*;",
        "__PLATFORM_BUFFER_BARRIER__;",
        text,
    )
    text = re.sub(
        r"std::array<VkBufferMemoryBarrier,\s*\d+>\s+\w+\s*=\s*\{[\s\S]*?\}\s*;",
        "",
        text,
    )
    text = re.sub(r"\w+Barrier(?:\.\w+)+\s*=[^;]*;", "", text)
    text = re.sub(r"VkImageMemoryBarrier\s+outputReadableBarrier\{\}\s*;", "", text)
    text = re.sub(
        r"vkCmdPipelineBarrier\(resource\.commandBuffer,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,static_cast<u32>\(compositorBufferBarriers\.size\(\)\),compositorBufferBarriers\.data\(\),0,nullptr\)\s*;",
        "__PLATFORM_BUFFER_BARRIER_SUBMIT__;",
        text,
    )
    text = re.sub(
        r"vkCmdPipelineBarrier\(resource\.commandBuffer,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&outputReadableBarrier\)\s*;",
        "__PLATFORM_OUTPUT_BARRIER__;",
        text,
    )
    replacements = {
        "MP_VK_COMPOSITOR_OUTPUT_IMAGE_BINDING": "0",
        "MP_VK_COMPOSITOR_CURRENT_3D_BINDING": "1",
        "MP_VK_COMPOSITOR_TOP_PACKED_BINDING": "2",
        "MP_VK_COMPOSITOR_BOTTOM_PACKED_BINDING": "3",
        "MP_VK_COMPOSITOR_PREVIOUS_TOP_3D_BINDING": "4",
        "MP_VK_COMPOSITOR_CAPTURE_3D_BINDING": "5",
        "MP_VK_COMPOSITOR_PREVIOUS_BOTTOM_3D_BINDING": "6",
        "melonDS::VulkanStructuredControlAbi::CompositorBindingCount": "7",
    }
    for old, new in replacements.items():
        text = text.replace(old, new)
    text = re.sub(r"__PLATFORM_BUFFER_BARRIER__;(?:\s*__PLATFORM_BUFFER_BARRIER__;){0,2}", "__PLATFORM_BUFFER_BARRIER_SUBMIT__;", text)
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


def extract_function_body(text: str, function_name: str) -> str:
    suffix = function_name.split("::", 1)[-1]
    pattern = rf"(?:bool|void|u32|int|Frame\*|std::[\w:<>,\s]+)\s+{re.escape(function_name)}\s*\([^;{{]*\)\s*(?:const\s*)?\{{"
    match = re.search(pattern, text)
    if match is None:
        pattern = rf"(?:bool|void|u32|int|Frame\*|std::[\w:<>,\s]+)\s+{re.escape(suffix)}\s*\([^;{{]*\)\s*(?:const\s*)?\{{"
        match = re.search(pattern, text)
    if match is None:
        raise AssertionError(f"function {function_name} not found")
    brace = text.find("{", match.end() - 1)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[match.start() : index + 1]
    raise AssertionError(f"unterminated body for {function_name}")


def extract_upstream_region(lines: list[str], start: int, end: int) -> str:
    return "".join(lines[start - 1 : end])


def region_sha256(text: str) -> str:
    return hashlib.sha256(normalize_source(text).encode("utf-8")).hexdigest()


class VulkanOutputExactTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        manifest = load_manifest()
        upstream_cpp = android_root() / manifest["upstreamRelativeCpp"]
        cls.manifest = manifest
        cls.upstream_cpp_text = read(upstream_cpp)
        cls.upstream_cpp_lines = cls.upstream_cpp_text.splitlines(keepends=True)
        cls.desktop_cpp_text = read(DESKTOP_VULKAN_OUTPUT_CPP)
        cls.desktop_header = read(DESKTOP_VULKAN_OUTPUT_H)
        cls.android_header = read(android_root() / "app/src/main/cpp/renderer/VulkanOutput.h")

    def test_soft_packed_snapshot_exact(self):
        self.assertEqual(
            extract_struct(self.android_header, "SoftPackedFrameSnapshot"),
            extract_struct(self.desktop_header, "SoftPackedFrameSnapshot"),
        )

    def test_required_structs_present(self):
        for name in STRUCTS:
            self.assertIn(f"struct {name}", self.desktop_header)

    def test_composition_regions_match_upstream(self):
        mismatches: list[str] = []
        for region in self.manifest["compositionRegions"]:
            label = region["label"]
            function_name = REGION_FUNCTION_NAMES[label]
            upstream_body = extract_upstream_region(
                self.upstream_cpp_lines,
                region["start"],
                region["end"],
            )
            desktop_body = extract_function_body(self.desktop_cpp_text, function_name)
            if region_sha256(upstream_body) != region_sha256(desktop_body):
                mismatches.append(
                    f"{label}: normalized body mismatch "
                    f"(upstream={region_sha256(upstream_body)[:12]}, "
                    f"desktop={region_sha256(desktop_body)[:12]})"
                )
        self.assertEqual(mismatches, [])

    def test_dispatch_compositor_uses_platform_barrier_helpers(self):
        self.assertIn("VulkanR24Barrier::HostWriteToShaderRead", self.desktop_cpp_text)
        self.assertIn("VulkanR24Barrier::CompositionWriteToPresenterRead", self.desktop_cpp_text)
        dispatch = extract_function_body(self.desktop_cpp_text, "VulkanOutput::dispatchCompositor")
        self.assertNotIn("compositorBufferBarriers", dispatch)

    def test_platform_hooks_are_declared_separately(self):
        self.assertIn("waitBeforePackedBufferOverwrite", self.desktop_cpp_text)
        prepare = extract_function_body(self.desktop_cpp_text, "VulkanOutput::prepareFrameForPresentation")
        self.assertNotIn("waitForFrame(frame, UINT64_MAX)", prepare)

    def test_frame_queue_selection_core_verified_by_generator(self):
        proc = subprocess.run(
            [sys.executable, str(REPO_ROOT / "tools" / "generate_sapphire_frame_queue.py"), "--verify"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)


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

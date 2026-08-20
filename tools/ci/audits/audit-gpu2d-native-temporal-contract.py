#!/usr/bin/env python3
"""Audit the cross-backend native GPU2D temporal/identity contract."""

from __future__ import annotations

from pathlib import Path


def require(text: str, needle: str, label: str, failures: list[str]) -> None:
    if needle not in text:
        failures.append(f"{label}: missing {needle!r}")


def main() -> int:
    root = Path(__file__).resolve().parents[3]
    files = {
        "native_header": root / "src/GPU2DNative.h",
        "native_recorder": root / "src/GPU2DNative.cpp",
        "native_contract_test": root / "tools/testing/gpu2d-native-contract-vectors.cpp",
        "native_purity_test": root / "tools/testing/gpu2d-native-recorder-purity.cpp",
        "soft_renderer": root / "src/GPU_Soft.cpp",
        "soft_header": root / "src/GPU_Soft.h",
        "vulkan_frontend": root / "src/GPU_Vulkan.cpp",
        "dx12_frontend": root / "src/GPU_DX12.cpp",
        "vulkan_renderer": root / "src/GPU3D_Vulkan.cpp",
        "dx12_renderer": root / "src/GPU3D_DX12.cpp",
        "vulkan_shader": root / "src/GPU3D_Vulkan_shaders/GPU2DNative.comp",
        "dx12_shader": root / "src/GPU3D_DX12_shaders.h",
        "opengl_renderer": root / "src/GPU_OpenGL.cpp",
        "physical_runner": root / "tools/testing/renderer-physical-ab.ps1",
    }
    failures: list[str] = []
    text = {}
    for label, path in files.items():
        if not path.is_file():
            failures.append(f"missing contract file: {path}")
        else:
            text[label] = path.read_text(encoding="utf-8")

    if not failures:
        require(text["native_header"], "IsCurrentFrame", "frame identity", failures)
        require(text["native_header"], "TimelineBlockCount", "timeline ABI", failures)
        require(text["native_header"], "TimelineDeltaCount", "timeline ABI", failures)
        require(text["native_header"], "TimelineHashTableSize", "deduplicated timeline ABI", failures)
        require(text["native_header"], "SpriteTimelineIndex", "private sprite timeline ABI", failures)
        require(text["native_recorder"], "CaptureMemoryForLine", "temporal recorder", failures)
        require(text["native_recorder"], "AppendTimelineDelta", "temporal recorder", failures)
        require(text["native_recorder"], "HashTimelineBlock", "deduplicated timeline", failures)
        require(text["native_recorder"], "CaptureSpriteLatchForLine", "OBJ/OAM latch timeline", failures)
        require(text["native_recorder"], "state.LCDVRAMMap", "per-line LCDC mapping", failures)
        require(text["native_recorder"], "Input.TimelineOverflow", "timeline overflow gate", failures)
        require(text["native_purity_test"], "RunHighChurnTimeline", "high-churn overflow stress", failures)
        require(text["native_purity_test"], "TimelineOverflow == 0u", "high-churn overflow stress", failures)
        require(text["soft_renderer"], "NativeGPU2DFrame.Reset()", "stale rejection", failures)
        require(text["soft_renderer"], "NativeGPU2DRecordedFrameSerial", "recorded identity", failures)
        if "&& !GPU.CaptureEnable" in text["soft_renderer"]:
            failures.append("soft renderer: CaptureEnable still disables native GPU2D ownership")
        require(text["soft_renderer"], "CaptureNativeDisplayLine", "native capture CPU mirror", failures)

        for label in ("vulkan_renderer", "dx12_renderer"):
            require(text[label], "CaptureYOffset", f"{label} line dispatch", failures)
            require(text[label], "NativeGPU2DDispatchCount", f"{label} dispatch accounting", failures)
            require(text[label], "PublishedOutputGeneration", f"{label} published identity", failures)
            require(text[label], "2u * static_cast<u32>(ScaleFactor)",
                f"{label} high-resolution sub-row dispatch", failures)
            require(text[label], ", 1u, 1u);", f"{label} capture line dispatch", failures)

        require(text["vulkan_frontend"], "stale_generation_reject", "Vulkan stale diagnostic", failures)
        require(text["dx12_frontend"], "stale_generation_reject", "DX12 stale diagnostic", failures)
        require(text["vulkan_frontend"], "SyncVRAMCapture", "Vulkan capture ownership", failures)
        require(text["dx12_frontend"], "SyncVRAMCapture", "DX12 capture ownership", failures)

        for label in ("vulkan_shader", "dx12_shader"):
            require(text[label], "TimelineVersion", f"{label} temporal shader", failures)
            require(text[label], "SpriteTimelineVersion", f"{label} OBJ/OAM latch shader", failures)
            require(text[label], "LCDVRAMMap", f"{label} per-line LCDC shader", failures)
            require(text[label], "linePass", f"{label} line shader pass", failures)
            require(text[label], "scaledScreens", f"{label} high-resolution line shader", failures)
        require(text["vulkan_shader"], "WriteNativeCapturePair", "Vulkan capture feedback", failures)
        require(text["dx12_shader"], "NativeCaptureSourceA", "DX12 capture feedback", failures)

        require(text["opengl_renderer"], "DumpFrameForValidation", "OpenGL pixel gate", failures)
        require(text["opengl_renderer"], "glReadPixels(0, 0, 256, 192", "OpenGL pixel gate", failures)
        require(text["opengl_renderer"], "ScaleFactor != 1", "OpenGL scale gate", failures)
        require(text["opengl_renderer"], "DumpGPU2DFrame", "OpenGL canonical dump", failures)

        for needle in (
            "[switch]$RequireCleanProvenance",
            "final acceptance requires detached HEAD",
            "checkout is dirty",
            "binary git_dirty=",
        ):
            require(text["physical_runner"], needle, "clean provenance gate", failures)

        for name in (
            "startup_pipeline_fallback",
            "runtime_native_unavailable_fallback",
            "capture_software_fallback",
            "stale_generation_reject",
            "structured_fallback",
        ):
            require(text["soft_header"], name, "fallback counter separation", failures)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: native GPU2D temporal, identity, capture, OpenGL, and provenance contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Audit the shipping GPU-memory instrumentation boundary.

This is a source contract, not a performance benchmark.  It prevents the
diagnostic memory model from silently returning to Release allocation paths and
keeps the scale-admission safety boundary in place.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
GPU_MEMORY_GATE = "MELONPRIME_ENABLE_GPU_MEMORY_TELEMETRY"


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise ValueError(f"missing function signature: {signature}")
    opening = source.find("{", start)
    if opening < 0:
        raise ValueError(f"missing function body: {signature}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening : index + 1]
    raise ValueError(f"unterminated function body: {signature}")


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def guarded_telemetry_blocks(source: str) -> tuple[str, str]:
    marker = f"#if defined({GPU_MEMORY_GATE})"
    # The header also uses the option in a small include guard before the
    # namespace; the last occurrence is the detailed-model branch.
    start = source.rfind(marker)
    if start < 0:
        raise ValueError(f"missing {GPU_MEMORY_GATE} compile-time gate")
    else_position = source.find("#else", start + len(marker))
    end_position = source.find("#endif", start + len(marker))
    if else_position < 0 or (end_position >= 0 and else_position > end_position):
        raise ValueError(f"missing OFF facade branch for {GPU_MEMORY_GATE}")
    return source[start:else_position], source[else_position:end_position]


def audit_binary(path: Path, failures: list[str]) -> None:
    try:
        data = path.read_bytes()
    except OSError as error:
        failures.append(f"cannot read shipping binary {path}: {error}")
        return

    for marker in (
        b"[Vulkan] memory telemetry boundary=",
        b"[Vulkan] memory telemetry heap=",
        b"buckets=1M:",
        b"[RendererStartup]",
        b"[RendererStartupSummary]",
    ):
        require(marker not in data,
                f"shipping binary contains detailed memory telemetry marker {marker!r}",
                failures)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path,
                        help="optional shipping executable to scan for telemetry strings")
    args = parser.parse_args()

    failures: list[str] = []

    dx12_context = read("src/DX12Context.cpp")
    for signature in (
        "DX12::ComPtr<ID3D12Resource> DX12Context::CreateBuffer(",
        "DX12::ComPtr<ID3D12Resource> DX12Context::CreateTexture2D(",
    ):
        try:
            body = function_body(dx12_context, signature)
        except ValueError as error:
            failures.append(str(error))
            continue
        require("GetResourceAllocationInfo" not in body,
                f"{signature} must not perform a redundant allocation-info preflight",
                failures)
        require("CreateCommittedResource" in body,
                f"{signature} must retain CreateCommittedResource authority",
                failures)
        require("DX12::Fail" in body,
                f"{signature} must retain HRESULT failure handling",
                failures)

    require("GetResourceAllocationInfo" not in dx12_context,
            "DX12Context.cpp must not reintroduce the unused resource-allocation query",
            failures)

    memory_admission = read("src/VulkanMemoryAdmission.h")
    require("#include <string>" not in memory_admission,
            "Vulkan admission result must not carry an allocating std::string reason",
            failures)
    require("enum class VulkanMemoryAdmissionReason" in memory_admission,
            "Vulkan admission reasons must be POD enum values",
            failures)
    require("VulkanMemoryAdmissionReason Reason" in memory_admission,
            "Vulkan admission result must use a POD reason code",
            failures)

    memory_doc = read("docs/development/performance/vulkan-memory-admission.md")
    memory_doc_lower = memory_doc.lower()
    for token in (
        "VulkanTextureHeap::Reserve()",
        "VulkanTextureHeap::MaterializePendingCreates()",
        "CreateScratchUpload()",
        "retry once",
        "device-lost",
        "bypasses the persistent reservation counters",
        "driver is the final",
    ):
        require(token.lower() in memory_doc_lower,
                f"memory-accounting policy must document {token}",
                failures)

    telemetry = read("src/VulkanMemoryTelemetry.h")
    try:
        telemetry_on, telemetry_off = guarded_telemetry_blocks(telemetry)
    except ValueError as error:
        failures.append(str(error))
        telemetry_on, telemetry_off = "", ""
    for token in (
        "PeakAllocationCount",
        "TotalAllocationCount",
        "TotalFreeCount",
        "AllocationSizeBuckets",
        "BucketFor(",
    ):
        require(token in telemetry_on,
                f"detailed telemetry model must retain {token} when enabled",
                failures)
        require(token not in telemetry_off,
                f"detailed telemetry token {token} must be absent from the OFF facade",
                failures)
    require("constexpr VulkanMemoryTelemetry() noexcept = default;" in telemetry_off,
            "OFF telemetry facade must be constexpr constructible",
            failures)
    require("constexpr void RecordAllocation" in telemetry_off
            and "constexpr void RecordFree" in telemetry_off,
            "OFF telemetry facade must make allocation updates no-op",
            failures)

    vulkan_device = read("src/VulkanDevice.cpp")
    reserve_body = function_body(vulkan_device, "bool VulkanDevice::ReserveMemoryAllocation(")
    require("VulkanMemoryAdmissionSnapshot snapshot" not in reserve_body,
            "per-allocation admission must not copy the full capability snapshot",
            failures)
    require("GetSnapshot()" not in reserve_body,
            "per-allocation admission must not copy detailed telemetry snapshots",
            failures)
    require("std::string" not in reserve_body,
            "accepted allocation admission must not construct diagnostic strings",
            failures)
    require("RecordAllocation" in reserve_body and f"#if defined({GPU_MEMORY_GATE})" in reserve_body,
            "detailed allocation recording must remain behind the GPU-memory gate",
            failures)
    require("ApplyMemoryTelemetry" not in vulkan_device,
            "admission must not derive safety state from detailed telemetry",
            failures)
    log_body = function_body(vulkan_device, "void VulkanDevice::LogMemoryTelemetry(")
    require(f"#if defined({GPU_MEMORY_GATE})" in log_body
            and "[Vulkan] memory telemetry boundary=" in log_body,
            "detailed memory logging must be compile-time gated",
            failures)

    dx12_renderer = read("src/GPU3D_DX12.cpp")
    vulkan_renderer = read("src/GPU3D_Vulkan.cpp")
    for name, renderer in (("DX12", dx12_renderer), ("Vulkan", vulkan_renderer)):
        require("struct ComposeWorkSlot" in renderer,
                f"{name} must separate command-ring work from presentation slots",
                failures)
        require("std::array<ComposeWorkSlot" in renderer,
                f"{name} must own exactly the work-slot ring resources",
                failures)
        require("if (outputSlot || diagnosticReadback)" in renderer,
                f"{name} must skip discarded Stage B presentation work",
                failures)
        require("EnsureDiagnosticResources" in renderer,
                f"{name} must lazily create native diagnostic readback resources",
                failures)
        require("native_readbacks=0" in renderer,
                f"{name} startup resource evidence must report zero native readbacks",
                failures)
        slot_match = re.search(
            r"struct Slot\s*\{(?P<body>.*?)\n\s*\};", renderer, re.DOTALL)
        require(slot_match is not None,
                f"{name} presentation Slot declaration must be auditable", failures)
        if slot_match:
            slot_body = slot_match.group("body")
            for token in ("NativeStaging", "NativeInput", "NativeReadback",
                          "NativeMapped", "UploadedNativeGeneration"):
                require(token not in slot_body,
                        f"{name} presentation Slot must not own {token}", failures)

    for token in ("ID3D12PipelineLibrary", "LoadComputePipeline",
                  "StorePipeline", "Serialize(", "AdapterLuid",
                  "RootSignatureHash", "ShaderBlobHash"):
        require(token in dx12_renderer or token == "ID3D12PipelineLibrary",
                f"DX12 persistent pipeline cache must retain {token}", failures)
    dx12_header = read("src/GPU3D_DX12.h")
    require("ID3D12PipelineLibrary" in dx12_header,
            "DX12 renderer must own a pipeline library", failures)
    for renderer, name in ((dx12_renderer, "DX12"), (vulkan_renderer, "Vulkan")):
        require("MELONPRIME_ENABLE_DEVELOPER_FEATURES" in renderer
                and "MELONPRIME_RENDERER_STARTUP_PROFILE" in renderer,
                f"{name} startup profiler must be developer-only and opt-in",
                failures)
    try:
        dx12_frame = function_body(dx12_renderer, "void DX12Renderer3D::RenderFrame(")
        vulkan_frame = function_body(vulkan_renderer, "void VulkanRenderer3D::RenderFrame(")
    except ValueError as error:
        failures.append(str(error))
        dx12_frame = vulkan_frame = ""
    for name, frame in (("DX12 RenderFrame", dx12_frame),
                        ("Vulkan RenderFrame", vulkan_frame)):
        require("QueryVideoMemoryInfo" not in frame,
                f"{name} must not query live video-memory budget",
                failures)
        require("GetPhysicalDeviceMemoryProperties2" not in frame,
                f"{name} must not query live Vulkan memory budget",
                failures)
        require("RefreshMemoryAdmission" not in frame,
                f"{name} must not refresh memory admission in steady state",
                failures)

    cmake = read("src/frontend/qt_sdl/CMakeLists.txt")
    require(re.search(
        rf"option\(\s*{GPU_MEMORY_GATE}\s+.*?\n\s*OFF\s*\)", cmake, re.DOTALL) is not None,
            "GPU memory telemetry CMake option must default to OFF",
            failures)
    require(re.search(
        rf"if \(\s*{GPU_MEMORY_GATE}\s*\).*?{GPU_MEMORY_GATE}=1.*?endif\(\)",
        cmake, re.DOTALL) is not None,
            "GPU memory telemetry definitions must be conditional",
            failures)
    require(f"{GPU_MEMORY_GATE}=ON" not in cmake,
            "project CMake must not force GPU memory telemetry ON",
            failures)

    try:
        presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
        preset_text = json.dumps(presets)
    except (OSError, json.JSONDecodeError) as error:
        failures.append(f"cannot parse CMakePresets.json: {error}")
        preset_text = ""
    require(preset_text.count(GPU_MEMORY_GATE) >= 2
            and '"value": "OFF"' in preset_text,
            "release presets must explicitly keep GPU memory telemetry OFF",
            failures)

    workflows = ROOT / ".github" / "workflows"
    for path in sorted(workflows.glob("build-*.y*ml")):
        text = path.read_text(encoding="utf-8")
        if "MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=OFF" in text:
            require(f"{GPU_MEMORY_GATE}=OFF" in text,
                    f"{path.name} must explicitly configure GPU memory telemetry OFF",
                    failures)
        require(f"{GPU_MEMORY_GATE}=ON" not in text,
                f"{path.name} must not force GPU memory telemetry ON",
                failures)

    if args.binary:
        audit_binary(args.binary, failures)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print("PASS: GPU memory production-overhead contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

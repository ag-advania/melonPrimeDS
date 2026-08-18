#!/usr/bin/env python3
"""Audit the explicit DX12/Vulkan raster preparation/reuse-wait contract.

The explicit renderers intentionally keep one scale-dependent raster resource
set.  Correctness therefore requires the CPU-only polygon/texture plan to run
before the single-slot reuse wait, while uploads and command recording remain
after the fence retires.  This is a source contract, not a performance claim.
"""

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


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


def require_order(body: str, before: str, after: str, label: str, failures: list[str]) -> None:
    before_position = body.find(before)
    after_position = body.find(after)
    require(before_position >= 0, f"{label}: missing {before!r}", failures)
    require(after_position >= 0, f"{label}: missing {after!r}", failures)
    if before_position >= 0 and after_position >= 0:
        require(
            before_position < after_position,
            f"{label}: {before!r} must precede {after!r}",
            failures,
        )


def main() -> int:
    failures: list[str] = []

    dx12_source = read("src/GPU3D_DX12.cpp")
    vulkan_source = read("src/GPU3D_Vulkan.cpp")
    dx12_context = read("src/DX12Context.cpp")
    vulkan_sync = read("src/VulkanSync.cpp")
    vulkan_header = read("src/GPU3D_Vulkan.h")
    soft_header = read("src/GPU_Soft.h")
    soft_source = read("src/GPU_Soft.cpp")
    dx12_perf = read("src/DX12Perf.h")
    vulkan_perf = read("src/VulkanPerf.h")
    perf_probe = read("src/frontend/qt_sdl/MelonPrimePerfProbe.h")
    emu_thread = read("src/frontend/qt_sdl/EmuThread.cpp")

    try:
        dx12_frame = function_body(dx12_source, "void DX12Renderer3D::RenderFrame()")
        vulkan_frame = function_body(vulkan_source, "void VulkanRenderer3D::RenderFrame()")
    except ValueError as error:
        failures.append(str(error))
        dx12_frame = vulkan_frame = ""

    for name, frame, begin, record in (
        (
            "DX12",
            dx12_frame,
            "Commands.Begin(true)",
            "TextureHeap.RecordPendingUploads()",
        ),
        (
            "Vulkan",
            vulkan_frame,
            "Frames.BeginFrame(true)",
            "TextureHeap.RecordPendingUploads()",
        ),
    ):
        require_order(frame, "BuildPolygons(", begin, f"{name} CPU preparation", failures)
        require_order(frame, begin, record, f"{name} upload recording", failures)
        require_order(
            frame,
            record,
            "FlushUploadBarriers()",
            f"{name} texture barriers",
            failures,
        )
        require(
            "Texcache.Update(" in frame,
            f"{name} RenderFrame must retain texture-cache preparation",
            failures,
        )

    require(
        "RendererFramesInFlight = 1" in vulkan_header
        and "RendererFramesInFlight = 2" not in vulkan_header,
        "Vulkan rasterizer must keep the single scale-dependent resource slot",
        failures,
    )
    require(
        "WaitForSingleObject(FenceEvent, INFINITE)" not in dx12_context
        and "kFenceWaitTimeoutMs" in dx12_context
        and "GetDeviceRemovedReason" in dx12_context,
        "DX12 raster reuse waits must be bounded and check device removal",
        failures,
    )
    require(
        "WaitForFences(" in vulkan_sync
        and "DefaultFenceTimeoutNanoseconds" in vulkan_sync,
        "Vulkan raster reuse waits must retain a bounded fence timeout",
        failures,
    )

    store_start = soft_header.find("inline void StoreStructuredEnginePixel(")
    store_end = soft_header.find("inline void FlushStructuredEngineLine(", store_start)
    store_body = soft_header[store_start:store_end] if store_start >= 0 and store_end >= 0 else ""
    require(
        store_body and "StructuredEngineChangedMask[engine]" in store_body,
        "structured engine pixels must batch changed planes per scanline",
        failures,
    )
    require(
        "MarkStructuredPlaneDirty(" not in store_body,
        "structured engine pixel loop must not publish generation per pixel",
        failures,
    )
    require(
        "FlushStructuredEngineLine" in soft_header
        and "FlushStructuredGeneration" in soft_header
        and "StructuredPendingPlaneDirtyMask" in soft_header,
        "structured generation publication must have scanline/frame aggregation",
        failures,
    )

    screen_start = soft_source.find("void SoftRenderer::BuildStructuredScreenLine(")
    screen_end = soft_source.find("void SoftRenderer::FinalizeStructuredCaptureFrame", screen_start)
    screen_body = soft_source[screen_start:screen_end] if screen_start >= 0 and screen_end >= 0 else ""
    require(
        "u8 changedPlaneMask = 0" in screen_body
        and screen_body.count("&changedPlaneMask") >= 4,
        "structured fallback screen lines must batch plane dirty tracking",
        failures,
    )

    for name, perf in (("DX12", dx12_perf), ("Vulkan", vulkan_perf)):
        for metric in (
            "raster_cpu_prepare_us",
            "raster_reuse_wait_us",
            "raster_record_submit_us",
            "soft2d_total_us",
            "structured2d_metadata_us",
            "structured_pack_us",
        ):
            require(
                f'"{metric}"' in perf,
                f"{name} telemetry must expose {metric}",
                failures,
            )
        require(
            "#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)" in perf,
            f"{name} telemetry must remain compile-time gated",
            failures,
        )

    require(
        '"present_slot_wait_us"' in dx12_perf,
        "DX12 telemetry must expose present_slot_wait_us",
        failures,
    )
    require(
        '"present_slot_wait_us"' in vulkan_perf
        and '"present_acquire_wait_us"' in vulkan_perf,
        "Vulkan telemetry must expose presenter slot/acquire waits",
        failures,
    )
    for metric in (
        "frame_input_sample_to_runframe_begin_us",
        "input_sample_to_present_end_us",
    ):
        require(
            metric in perf_probe,
            f"developer latency probe must expose {metric}",
            failures,
        )
    for marker in (
        "MelonPrimePerf::MarkInputSample();",
        "MelonPrimePerf::MarkRunFrameBegin();",
        "MelonPrimePerf::MarkPresentEnd();",
    ):
        require(
            marker in emu_thread,
            f"emulation-thread latency probe must call {marker}",
            failures,
        )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print("PASS: explicit DX12/Vulkan late-fence and structured producer contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

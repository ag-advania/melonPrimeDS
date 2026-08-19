#!/usr/bin/env python3
"""Audit the explicit DX12/Vulkan raster preparation/reuse-wait contract.

The explicit renderers intentionally keep one scale-dependent raster resource
set.  Correctness therefore requires the CPU-only polygon/texture plan to run
before the single-slot reuse wait, while uploads and command recording remain
after the fence retires.  Texture arrays retain their logical handle during
CPU preparation, but physical resources and descriptor-visible views may only
be created at the post-fence materialization boundary.  This is a source
contract, not a performance claim.
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
    dx12_texcache_header = read("src/GPU3D_TexcacheDX12.h")
    dx12_texcache_source = read("src/GPU3D_TexcacheDX12.cpp")
    vulkan_texcache_header = read("src/GPU3D_TexcacheVulkan.h")
    vulkan_texcache_source = read("src/GPU3D_TexcacheVulkan.cpp")
    generic_texcache = read("src/GPU3D_Texcache.h")
    structured_perf = read("src/MelonPrimeStructuredPerf.h")
    soft2d_source = read("src/GPU2D_Soft.cpp")
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
            begin,
            "TextureHeap.MaterializePendingCreates()",
            f"{name} physical texture materialization",
            failures,
        )
        require_order(
            frame,
            "TextureHeap.MaterializePendingCreates()",
            record,
            f"{name} materialization before uploads",
            failures,
        )
        require_order(
            frame,
            record,
            "FlushUploadBarriers()",
            f"{name} texture barriers",
            failures,
        )
        reset_position = frame.find("TextureHeap.ResetFailures();")
        update_position = frame.find("Texcache.Update(")
        require(
            reset_position >= 0 and update_position >= 0 and reset_position < update_position,
            f"{name} texture preparation failures must be reset before Texcache.Update",
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

    # P2-001: reserve/identity is CPU-only; driver objects are materialized
    # after the explicit frame slot has been acquired.  Keep this check close
    # to the implementation so a future Create/Reserve regression fails CI.
    for name, header, source, reserve_signature, materialize_signature in (
        (
            "DX12",
            dx12_texcache_header,
            dx12_texcache_source,
            "u32 DX12TextureHeap::Reserve",
            "bool DX12TextureHeap::MaterializePendingCreates",
        ),
        (
            "Vulkan",
            vulkan_texcache_header,
            vulkan_texcache_source,
            "u32 VulkanTextureHeap::Reserve",
            "bool VulkanTextureHeap::MaterializePendingCreates",
        ),
    ):
        try:
            reserve_body = function_body(source, reserve_signature)
            materialize_body = function_body(source, materialize_signature)
        except ValueError as error:
            failures.append(str(error))
            reserve_body = materialize_body = ""

        require(
            "PendingCreate" in header and "PhysicalReady" in header,
            f"{name} texture entries must expose logical/physical lifecycle state",
            failures,
        )
        require(
            "PendingCreate = true" in reserve_body
            and "PhysicalReady = true" not in reserve_body,
            f"{name} Reserve must create a logical identity without a physical resource",
            failures,
        )
        physical_create = "CreateTexture2D" if name == "DX12" else "CreateImage"
        require(
            physical_create in materialize_body
            and "PhysicalReady = true" in materialize_body
            and "PendingCreate = false" in materialize_body,
            f"{name} MaterializePendingCreates must own physical creation and readiness",
            failures,
        )

        try:
            record_body = function_body(source, f"bool {name}TextureHeap::RecordUpload")
        except ValueError as error:
            failures.append(str(error))
            record_body = ""
        require(
            "PhysicalReady" in record_body,
            f"{name} uploads must reject handles before physical materialization",
            failures,
        )
        require(
            "PendingUpload" in source and "Data.resize(bytes)" in source,
            f"{name} pending upload storage must be reusable backend-owned CPU storage",
            failures,
        )

    # P2-002: the generic decoder writes into the explicit backend's pending
    # CPU storage.  The old common decoded-buffer-to-pending-buffer memcpy must
    # not return to the hot miss path.
    try:
        get_texture_body = function_body(generic_texcache, "void GetTexture(")
    except ValueError as error:
        failures.append(str(error))
        get_texture_body = ""
    require(
        "TextureDecodeTarget" in generic_texcache
        and "BeginTextureUpload" in get_texture_body
        and "decodeBuffer" in get_texture_body,
        "generic texture decoding must target backend-owned pending CPU storage",
        failures,
    )
    require_order(
        get_texture_body,
        "BeginTextureUpload(",
        "BeginTextureDecode()",
        "texture direct decode setup",
        failures,
    )
    require_order(
        get_texture_body,
        "BeginTextureDecode()",
        "CommitTextureUpload(",
        "texture direct decode commit",
        failures,
    )
    require(
        "std::memcpy" not in get_texture_body,
        "generic texture miss path must not copy DecodingBuffer into pending upload data",
        failures,
    )

    # P3-001/P3-002: the shared software producer selects its explicit backend
    # once per frame and routes both structured timers through that selection.
    require(
        "enum class StructuredPerfBackend" in structured_perf
        and "BeginStructured2DPerfFrame" in structured_perf
        and "EndStructured2DPerfFrame" in structured_perf
        and "ToDX12Metric" in structured_perf
        and "ToVulkanMetric" in structured_perf,
        "structured software timing must use a backend-neutral explicit wrapper",
        failures,
    )
    try:
        draw_scanline_body = function_body(
            soft_source, "void SoftRenderer::DrawScanline(u32 line)"
        )
    except ValueError as error:
        failures.append(str(error))
        draw_scanline_body = ""
    require(
        draw_scanline_body.count("GetStructured2DPerfBackend()") == 1
        and "StructuredPerfBackendForFrame" in draw_scanline_body,
        "structured backend selection must occur once at frame start",
        failures,
    )
    require(
        "VulkanPerf::" not in soft_source
        and "VulkanPerf::" not in soft2d_source
        and "DX12Perf::" not in soft_source
        and "DX12Perf::" not in soft2d_source,
        "shared structured producers must not hard-code a renderer telemetry backend",
        failures,
    )
    require(
        "PhysicalReady" in dx12_source and "PhysicalReady" in vulkan_source,
        "explicit descriptor setup must require a physically materialized texture",
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
            "texture_decode_us",
            "texture_pending_cpu_copy_us",
            "texture_resource_create_us",
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
        for counter_name in (
            "texture_materialize_count",
            "texture_pending_upload_bytes",
            "texture_pending_upload_count",
        ):
            require(
                counter_name in perf,
                f"{name} telemetry must expose {counter_name}",
                failures,
            )
        require(
            "p50_us=" in perf
            and "p95_us=" in perf
            and "p99_us=" in perf
            and "max_us=" in perf,
            f"{name} telemetry must report p50/p95/p99/max samples",
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

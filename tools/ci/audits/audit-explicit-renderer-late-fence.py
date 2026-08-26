#!/usr/bin/env python3
"""Audit the explicit DX12/Vulkan raster preparation/reuse-wait contract.

The explicit renderers keep one scale-dependent GPU scratch set, but use two
CPU-owned recording slots. Correctness therefore requires slot-local command,
descriptor, upload, and staging resources plus explicit same-queue dependencies
for the shared GPU scratch. CPU-only polygon/texture preparation and independent
host-side texture resource creation run before the selected slot's reuse wait;
uploads and command recording remain after that fence retires. This is a source
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
    # Command lists, allocators and their fence waits split out of the device
    # owner; the bounded-wait contract travels with them.
    dx12_command_context = read("src/DX12CommandContext.cpp")
    dx12_pipeline_repo_header = read("src/DX12PipelineRepository.h")
    vulkan_sync_header = read("src/VulkanSync.h")
    vulkan_sync = read("src/VulkanSync.cpp")
    vulkan_header = read("src/GPU3D_Vulkan.h")
    dx12_header = read("src/GPU3D_DX12.h")
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
    frame_retire_test = read("tools/testing/vulkan-frame-retire-tests.cpp")
    vulkan_cmake = read("src/frontend/qt_sdl/CMakeLists.txt")
    vulkan_workflows = {
        workflow: read(f".github/workflows/{workflow}")
        for workflow in (
            "build-windows.yml",
            "build-ubuntu.yml",
            "build-bsd.yml",
            "build-macos.yml",
        )
    }

    try:
        dx12_frame = function_body(dx12_source, "void DX12Renderer3D::RenderFrame()")
        vulkan_frame = function_body(vulkan_source, "void VulkanRenderer3D::RenderFrame()")
    except ValueError as error:
        failures.append(str(error))
        dx12_frame = vulkan_frame = ""

    for name, frame, begin, record, submit in (
        (
            "DX12",
            dx12_frame,
            "rasterFrame.Commands.Begin(true)",
            "TextureHeap.RecordPendingUploads()",
            "rasterFrame.Commands.Submit()",
        ),
        (
            "Vulkan",
            vulkan_frame,
            "Frames.BeginFrame(true)",
            "TextureHeap.RecordPendingUploads()",
            "Frames.SubmitFrame(Device.GetMainQueue())",
        ),
        ):
        build_position = frame.find("BuildPolygons(")
        failure_after_build = frame.find("TextureHeap.HadFailure()", build_position + 1)
        materialize = "TextureHeap.MaterializePendingCreates()"
        first_materialize = frame.find(materialize)
        second_materialize = frame.find(materialize, first_materialize + len(materialize))
        begin_position = frame.find(begin)
        record_position = frame.find(record)
        retry_guard = frame.find(
            "materializeResult == TextureMaterializeResult::RetryAfterRetire")
        clear_retry_position = frame.find(
            "TextureHeap.ClearRetryableCreationFailure()", retry_guard)
        retry_submit_position = frame.find(submit, second_materialize)

        require(
            build_position >= 0 and failure_after_build > build_position,
            f"{name} BuildPolygons must be followed by a CPU-preparation failure check",
            failures,
        )
        require(
            first_materialize > failure_after_build and first_materialize < begin_position,
            f"{name} pre-fence materialization must follow BuildPolygons failure checking",
            failures,
        )
        require(
            second_materialize > begin_position,
            f"{name} must have a post-retire retry materialization call",
            failures,
        )
        require_order(frame, begin, record, f"{name} upload recording", failures)
        require(
            frame.count(materialize) == 2
            and retry_guard >= begin_position
            and retry_guard < second_materialize
            and clear_retry_position >= 0
            and clear_retry_position < second_materialize
            and retry_submit_position > second_materialize,
            f"{name} retry path must begin after the reuse wait, clear retry state, retry once, and recover the frame slot",
            failures,
        )
        require(
            second_materialize < record_position,
            f"{name} retry materialization must finish before upload recording",
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
        "RendererFramesInFlight = 2" in vulkan_header
        and "RendererFramesInFlight = 1" not in vulkan_header
        and "std::array<Vk::StagingRing, RendererFramesInFlight> FrameStaging" in vulkan_header,
        "Vulkan rasterizer must use exactly two CPU recording/staging slots",
        failures,
    )
    require(
        "RasterFramesInFlight = 2" in dx12_header
        and "std::array<RasterFrameSlot, RasterFramesInFlight> RasterFrames" in dx12_header,
        "DX12 rasterizer must use exactly two CPU recording slots",
        failures,
    )
    raster_slot_start = dx12_header.find("struct RasterFrameSlot")
    raster_slot_end = dx12_header.find("\n    };", raster_slot_start)
    raster_slot = (
        dx12_header[raster_slot_start:raster_slot_end]
        if raster_slot_start >= 0 and raster_slot_end >= 0
        else ""
    )
    require(
        all(
            token in raster_slot
            for token in (
                "DX12CommandContext Commands",
                "DX12UploadRing Uploads",
                "DX12DescriptorRing Descriptors",
                "YSpanSetupStaging",
                "SetupIndicesStaging",
                "RenderPolygonStaging",
                "ClearBitmapUpload",
                "MetaUniformUpload",
            )
        ),
        "DX12 command, descriptor, upload, and mapped staging resources must be slot-local",
        failures,
    )
    require(
        all(
            token not in raster_slot
            for token in (
                "TileBuffers",
                "ResultBuffer",
                "BinResultBuffer",
                "WorkDescBuffer",
                "BlendStateBuffer",
                "FinalFBBuffer",
                "CaptureSidecarBuffer",
            )
        ),
        "DX12 large GPU scratch must remain single-copy outside the CPU slot ring",
        failures,
    )
    try:
        vulkan_scratch_barrier = function_body(
            vulkan_source,
            "void VulkanRenderer3D::RecordSharedScratchReuseBarrier(",
        )
        dx12_scratch_barrier = function_body(
            dx12_source,
            "void DX12Renderer3D::InsertRasterScratchReuseBarriers(",
        )
    except ValueError as error:
        failures.append(str(error))
        vulkan_scratch_barrier = dx12_scratch_barrier = ""
    require(
        "RecordSharedScratchReuseBarrier(cmd);" in vulkan_frame
        and "VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT" in vulkan_scratch_barrier
        and "VK_PIPELINE_STAGE_TRANSFER_BIT" in vulkan_scratch_barrier
        and "VK_ACCESS_TRANSFER_WRITE_BIT" in vulkan_scratch_barrier
        and "VK_PIPELINE_STAGE_ALL_COMMANDS_BIT" not in vulkan_scratch_barrier,
        "Vulkan shared compute scratch and geometry overwrite must have scoped GPU-side dependencies",
        failures,
    )
    require(
        "InsertRasterScratchReuseBarriers(list);" in dx12_frame
        # The barrier shapes themselves are contract, not renderer code: they
        # live beside the root signature every list records against.
        and "D3D12_RESOURCE_BARRIER_TYPE_UAV" in dx12_pipeline_repo_header
        and "TileBuffers" in dx12_scratch_barrier
        and "FinalFBBuffer" in dx12_scratch_barrier,
        "DX12 shared GPU scratch must have resource-scoped cross-list UAV barriers",
        failures,
    )
    require_order(
        dx12_frame,
        "TextureHeap.SetFrameResources(",
        "Texcache.Update(",
        "DX12 slot-local texture lifetime selection",
        failures,
    )
    require_order(
        dx12_frame,
        "rasterFrame.Commands.Begin(true)",
        "TextureHeap.CollectGarbage()",
        "DX12 texture retirement after slot fence",
        failures,
    )
    require(
        "GraveyardRetirePrefix" in dx12_texcache_header
        and "SpillRetirePrefix" in dx12_texcache_header
        and "Graveyards[ActiveFrameSlot]" in dx12_texcache_source
        and "SpillUploadSlots[ActiveFrameSlot]" in dx12_texcache_source,
        "DX12 texture and spill lifetimes must retire only the selected slot's pre-prepare prefix",
        failures,
    )
    require(
        "WaitForSingleObject(FenceEvent, INFINITE)" not in dx12_command_context
        and "kFenceWaitTimeoutMs" in dx12_command_context
        and "GetDeviceRemovedReason" in dx12_command_context,
        "DX12 raster reuse waits must be bounded and check device removal",
        failures,
    )
    require(
        "WaitForFences(" in vulkan_sync
        and "DefaultFenceTimeoutNanoseconds" in vulkan_sync,
        "Vulkan raster reuse waits must retain a bounded fence timeout",
        failures,
    )

    # P2-001: deferred resource tags must name the last frame that can still
    # reference an object, not the scheduler's next frame. Keep the policy
    # pure so the no-submission / submitted / recording cases remain runnable
    # without a Vulkan device, and keep the queue/fake-OOM ordering test next
    # to the production test target.
    require(
        "struct VulkanResourceRetireFrameState" in vulkan_sync_header
        and "constexpr u64 VulkanResourceRetireFrame(" in vulkan_sync_header
        and "if (state.Recording)" in vulkan_sync_header
        and "if (state.HasSubmittedFrame)" in vulkan_sync_header,
        "Vulkan resource retirement must expose a pure recording/submission policy",
        failures,
    )
    require(
        all(
            f"Get{method}()" in vulkan_sync_header
            for method in (
                "CurrentRecordingFrameNumber",
                "LastSubmittedFrameNumber",
                "ResourceRetireFrame",
            )
        )
        and "GetCurrentRecordingFrameNumber" in vulkan_sync
        and "GetLastSubmittedFrameNumber" in vulkan_sync
        and "GetResourceRetireFrame" in vulkan_sync,
        "FrameRing must distinguish recording, last-submitted, and resource-retire frame APIs",
        failures,
    )
    try:
        create_body = function_body(vulkan_sync, "bool FrameRing::Create(")
        destroy_body = function_body(vulkan_sync, "void FrameRing::Destroy()")
        begin_frame_body = function_body(
            vulkan_sync, "FrameContext* FrameRing::BeginFrameInternal("
        )
        submit_frame_body = function_body(vulkan_sync, "bool FrameRing::SubmitFrame(")
        last_submitted_getter_body = function_body(
            vulkan_sync, "u64 FrameRing::GetLastSubmittedFrameNumber()"
        )
    except ValueError as error:
        failures.append(str(error))
        create_body = destroy_body = begin_frame_body = ""
        submit_frame_body = last_submitted_getter_body = ""

    last_submit_assignment = "LastSubmittedFrameNumber = frame.SubmittedFrame;"
    end_command_buffer_check = submit_frame_body.find(
        'if (!MELONPRIME_VK_CHECK("vkEndCommandBuffer", res))'
    )
    end_command_buffer_failure = submit_frame_body.find(
        "return false;", end_command_buffer_check
    )
    queue_submit_check = submit_frame_body.find(
        'if (!MELONPRIME_VK_CHECK("vkQueueSubmit", res))'
    )
    queue_submit_failure = submit_frame_body.find(
        "return false;", queue_submit_check
    )
    last_submit_assignment_position = submit_frame_body.find(last_submit_assignment)
    require(
        "u64 LastSubmittedFrameNumber = 0;" in vulkan_sync_header
        and create_body.count("LastSubmittedFrameNumber = 0;") == 1
        and destroy_body.count("LastSubmittedFrameNumber = 0;") == 2,
        "FrameRing must reset the independent last-successful-submit number on create and destroy",
        failures,
    )
    require(
        "return HasSubmittedFrame ? LastSubmittedFrameNumber : 0;"
        in last_submitted_getter_body
        and "Frames[LastSubmittedIndex].SubmittedFrame" not in last_submitted_getter_body,
        "FrameRing last-submitted getter must use the independent frame number, not the slot field",
        failures,
    )
    require(
        submit_frame_body.count(last_submit_assignment) == 1
        and "LastSubmittedFrameNumber =" not in begin_frame_body
        and end_command_buffer_check >= 0
        and end_command_buffer_failure > end_command_buffer_check
        and last_submit_assignment_position > end_command_buffer_failure
        and queue_submit_check >= 0
        and queue_submit_failure > queue_submit_check
        and last_submit_assignment_position > queue_submit_failure,
        "FrameRing must update the independent number only after successful EndCommandBuffer and QueueSubmit",
        failures,
    )
    require(
        "BuildResourceRetireFrameState() const noexcept" in vulkan_sync_header
        and "VulkanFrameRingTestAccess" in vulkan_sync_header
        and "BuildResourceRetireFrameState" in vulkan_sync
        and "VulkanResourceRetireFrame(BuildResourceRetireFrameState())" in vulkan_sync,
        "FrameRing resource-retire mapping must be extracted through the production testable helper",
        failures,
    )
    try:
        retire_entry_body = function_body(
            vulkan_texcache_source, "void VulkanTextureHeap::RetireEntry"
        )
        record_upload_body = function_body(
            vulkan_texcache_source, "bool VulkanTextureHeap::RecordUpload"
        )
    except ValueError as error:
        failures.append(str(error))
        retire_entry_body = record_upload_body = ""
    require(
        "GetResourceRetireFrame()" in retire_entry_body
        and "GetAbsoluteFrame()" not in retire_entry_body,
        "Vulkan texcache RetireEntry must not tag resources with the next scheduler frame",
        failures,
    )
    require(
        "GetCurrentRecordingFrameNumber()" in record_upload_body
        and "GetAbsoluteFrame()" not in record_upload_body,
        "Vulkan scratch uploads must retain the current recording-frame lifetime semantics",
        failures,
    )
    require(
        all(
            token in frame_retire_test
            for token in (
                "Case 1: no submission",
                "Case 2: frame 10 submitted",
                "Case 3: frame 11 recording",
                "Case 4: previous frame completion",
                "Case A: the first frame is recording and no successful submit exists.",
                "Case B: a one-slot ring is reused while frame 11 is recording",
                "Case C: QueueSubmit for frame 11 failed",
                "LastSubmittedFrameNumber",
                "ring.AbsoluteFrame = state.Recording",
                "state.LastSubmittedFrame + 1",
                "ring.Frames.resize(1)",
                "VK_ERROR_OUT_OF_DEVICE_MEMORY",
                "DeferredDestroyQueue",
                "VulkanFrameRingTestAccess",
                "ring.GetResourceRetireFrame()",
                "production FrameRing",
                "queue.Collect(10)",
                "materializeCalls == 2",
                "RuntimeFailed",
                "allocate:success",
                "destroy:image_view",
            )
        )
        and "melonprime_vulkan_frame_retire_tests" in vulkan_cmake
        and "melonprime_vulkan_frame_retire_check" in vulkan_cmake,
        "Vulkan frame-retire model/fake-dispatch tests must be built and executed",
        failures,
    )
    require(
        "melonprime_apply_renderer_perf_telemetry_definition(\n        melonprime_vulkan_frame_retire_tests)"
        in vulkan_cmake,
        "Vulkan FrameRing production mapping test must match core telemetry layout",
        failures,
    )
    require(
        all(
            "melonprime_vulkan_frame_retire_check" in workflow_source
            for workflow_source in vulkan_workflows.values()
        ),
        "Windows/Linux/BSD/macOS workflows must explicitly run the production Vulkan retire-frame test",
        failures,
    )
    require(
        "image/resource creation happens after CPU polygon/texture" in generic_texcache
        and "GPU upload recording" in generic_texcache,
        "generic texcache comments must describe pre-fence materialization and post-fence uploads",
        failures,
    )
    require(
        "last frame that may" in vulkan_source
        and "current frame number" not in vulkan_source[vulkan_source.find("void VulkanRenderer3D::Reset()"):],
        "Vulkan Reset comments must describe the last GPU-referencing frame",
        failures,
    )

    # P2-001/P2-002: reserve/identity is CPU-only; driver objects are
    # materialized before the explicit frame slot is acquired, from a
    # pending-only worklist. Keep this check close to the implementation so a
    # future Create/Reserve, full-scan, or unbounded-retry regression fails CI.
    for name, header, source, reserve_signature, materialize_signature in (
        (
            "DX12",
            dx12_texcache_header,
            dx12_texcache_source,
            "u32 DX12TextureHeap::Reserve",
            "TextureMaterializeResult DX12TextureHeap::MaterializePendingCreates",
        ),
        (
            "Vulkan",
            vulkan_texcache_header,
            vulkan_texcache_source,
            "u32 VulkanTextureHeap::Reserve",
            "TextureMaterializeResult VulkanTextureHeap::MaterializePendingCreates",
        ),
    ):
        try:
            reserve_body = function_body(source, reserve_signature)
            materialize_body = function_body(source, materialize_signature)
            failure_body = function_body(
                source,
                f"TextureMaterializeResult {name}TextureHeap::HandleMaterializeFailure",
            )
            destroy_body = function_body(source, f"void {name}TextureHeap::Destroy")
        except ValueError as error:
            failures.append(str(error))
            reserve_body = materialize_body = failure_body = destroy_body = ""

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
        require(
            "TextureMaterializeResult::RetryAfterRetire" in materialize_body + failure_body
            and "TextureMaterializeResult::Fatal" in materialize_body + failure_body
            and "TextureMaterializeResult::Ready" in materialize_body + failure_body
            and "RetryableCreationFailure" in materialize_body + failure_body
            and "MaterializeRetryAttempted" in materialize_body + failure_body,
            f"{name} materialization must classify a bounded retryable failure",
            failures,
        )
        require(
            "std::vector<u32> PendingCreateSlots" in header
            and "PendingCreateSlots.push_back(slot)" in reserve_body
            and "for (u32 slot : PendingCreateSlots)" in materialize_body
            and "for (Entry& entry : Entries)" not in materialize_body
            and "PendingCreateSlots.clear()" in materialize_body
            and "PendingCreateSlots.erase" in destroy_body,
            f"{name} materialization must use a cancellable pending-slot worklist",
            failures,
        )
        require(
            all(
                dependency not in materialize_body
                for dependency in (
                    "Commands",
                    "Uploads",
                    "FrameCommandBuffer",
                    "FrameStaging",
                    "Descriptors",
                )
            ),
            f"{name} MaterializePendingCreates must not touch frame-local recording resources",
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
            "PendingUpload" in source
            and "std::unique_ptr<u32[]> Data" in header
            and "std::size_t CapacityWords" in header
            and "std::size_t UsedWords" in header
            and "EnsurePendingStorage" in source
            and "new (std::nothrow) u32[words]" in source
            and "std::make_unique" not in source
            and "Data.resize(words)" not in source
            and "reinterpret_cast<u32*>" not in source
            and "pending->Data.get(), pending->UsedWords * sizeof(u32)" in source
            and "UploadFailed = true" in source,
            f"{name} pending upload storage must be typed, high-watermark, nothrow, and fail-closed",
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
    require(
        "enum class TextureMaterializeResult" in generic_texcache
        and "enum class TextureMaterializeFailureReason" in generic_texcache,
        "explicit texture materialization must expose typed result and failure enums",
        failures,
    )
    require(
        "HRESULT* outResult" in dx12_context
        and "ClassifyDx12TextureCreationFailure" in dx12_texcache_source
        and "E_OUTOFMEMORY" in dx12_texcache_source,
        "DX12 materialization must preserve HRESULT classification for OOM retry",
        failures,
    )
    require(
        "ClassifyVulkanTextureCreationFailure" in vulkan_texcache_source
        and "VK_ERROR_OUT_OF_DEVICE_MEMORY" in vulkan_texcache_source
        and "VK_ERROR_OUT_OF_HOST_MEMORY" in vulkan_texcache_source
        and "VK_ERROR_DEVICE_LOST" in vulkan_texcache_source,
        "Vulkan materialization must preserve VkResult classification for OOM retry",
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
            "texture_pending_storage_grow_us",
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
            "texture_materialize_pre_fence_fail_count",
            "texture_materialize_retry_after_retire_count",
            "texture_materialize_retry_success_count",
            "texture_materialize_retry_fail_count",
            "texture_materialize_failure_reason",
            "texture_pending_upload_bytes",
            "texture_pending_upload_count",
            "texture_pending_storage_grow_count",
            "texture_pending_storage_grow_bytes",
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

#!/usr/bin/env python3
"""Audit the cross-backend native GPU2D temporal/identity contract."""

from __future__ import annotations

from pathlib import Path
import re


def require(text: str, needle: str, label: str, failures: list[str]) -> None:
    if needle not in text:
        failures.append(f"{label}: missing {needle!r}")


def require_regex(text: str, pattern: str, label: str, failures: list[str]) -> None:
    if re.search(pattern, text, re.MULTILINE) is None:
        failures.append(f"{label}: missing /{pattern}/")


def forbid(text: str, needle: str, label: str, failures: list[str]) -> None:
    if needle in text:
        failures.append(f"{label}: forbidden {needle!r}")


def forbid_regex(text: str, pattern: str, label: str, failures: list[str]) -> None:
    if re.search(pattern, text, re.MULTILINE | re.DOTALL) is not None:
        failures.append(f"{label}: forbidden /{pattern}/")


def main() -> int:
    root = Path(__file__).resolve().parents[3]
    files = {
        "gpu_header": root / "src/GPU.h",
        "gpu_core": root / "src/GPU.cpp",
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
        "vulkan_frame": root / "src/VulkanPresentedFrame.h",
        "dx12_frame": root / "src/DX12PresentedFrame.h",
        "vulkan_presenter": root / "src/frontend/qt_sdl/MelonPrimeVulkanPresenter.h",
        "vulkan_screen": root / "src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp",
        "dx12_presenter": root / "src/frontend/qt_sdl/MelonPrimeDX12SurfacePresenter.h",
        "dx12_screen": root / "src/frontend/qt_sdl/Screen.cpp",
        "screen_header": root / "src/frontend/qt_sdl/Screen.h",
        "gpu_stage_metrics": root / "src/GpuStageMetrics.h",
        "vulkan_sync": root / "src/VulkanSync.cpp",
        "dx12_context": root / "src/DX12Context.cpp",
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
        for needle in (
            "CaptureSyncResult",
            "CaptureBlockProvenance",
            "NativeCaptureStateIdentity",
            "CapturePhysicalBlockBytes",
            "CaptureDirtyBlocksPerPhysicalBlock",
            "RecordGPU2DCaptureSync",
            "CaptureAuthorityTransitionReason",
            "IsAllowedNativeToCpuTransition",
            "CaptureAuthorityDiagnostics",
        ):
            require(text["gpu_header"], needle, "core capture ownership contract", failures)
        for needle in (
            "GetCaptureProvenanceForRange",
            "PublishNativeCaptureBlock",
            "ResetCaptureProvenance",
            "flagsMarkedSynced",
            "flagsCleared",
            "SyncAllVRAMCaptures(CaptureAuthorityTransitionReason reason)",
            "capture_owner_transition",
        ):
            require(text["gpu_core"], needle, "core capture synchronization contract", failures)
        for needle in (
            "NativeToCpuReasonCaptureAllocated",
            "NativeToCpuReasonFrameBegin",
            "NativeToCpuReasonByteDifference",
        ):
            require(text["gpu_header"], needle, "capture authority diagnostics", failures)
        require(text["soft_renderer"], "GPU2DCaptureAuthorityCounters",
            "capture authority runtime counters", failures)

        require(text["native_header"], "IsCurrentFrame", "frame identity", failures)
        require(text["native_header"], "TimelineBlockCount", "timeline ABI", failures)
        require(text["native_header"], "TimelineDeltaCount", "timeline ABI", failures)
        require(text["native_header"], "TimelineHashTableSize", "deduplicated timeline ABI", failures)
        require(text["native_header"], "TimelineRowIds", "sparse timeline ABI", failures)
        require(text["native_header"], "SpriteTimelineRowIds", "private sparse sprite timeline ABI", failures)
        for needle in (
            "NativeCaptureBGMapping",
            "NativeCaptureOBJMapping",
            "NativeCaptureSpriteOBJMapping",
            "PackedNativeCaptureBGMappingBase",
            "PackedNativeCaptureOBJMappingBase",
            "PackedNativeCaptureSpriteOBJMappingBase",
            "MappedCaptureBytes",
            "MappedCaptureViolation",
        ):
            require(text["native_header"], needle,
                "GPU-resident mapped capture overlay ABI", failures)
        require(text["native_header"], "PackFrameRanges", "partial pack ABI", failures)
        require(text["native_recorder"], "CaptureMemoryForLine", "temporal recorder", failures)
        require(text["native_recorder"], "CaptureCaptureStateForLine", "capture write-boundary recorder", failures)
        require(text["native_recorder"], "AppendTimelineDelta", "temporal recorder", failures)
        require(text["native_recorder"], "HashTimelineBlock", "deduplicated timeline", failures)
        require(text["native_recorder"], "HashTimelineWords", "deduplicated timeline rows", failures)
        require(text["native_recorder"], "TimelineRowHashKeys", "deduplicated timeline rows", failures)
        require(text["native_recorder"], "SpriteTimelineRowHashKeys", "deduplicated sprite rows", failures)
        require(text["native_recorder"], "CaptureSpriteLatchForLine", "OBJ/OAM latch timeline", failures)
        require(text["native_recorder"], "state.LCDVRAMMap", "per-line LCDC mapping", failures)
        require(text["native_recorder"], "Input.TimelineOverflow", "timeline overflow gate", failures)
        require(text["gpu_stage_metrics"], "GpuMetricQueryCount", "GPU timestamp query ABI", failures)
        require(text["vulkan_sync"], "LastTimestampWrittenMask", "Vulkan timestamp validity", failures)
        require(text["dx12_context"], "GpuMetricQueryCount", "DX12 timestamp query ABI", failures)
        require(text["native_purity_test"], "RunHighChurnTimeline", "high-churn overflow stress", failures)
        require(text["native_purity_test"], "TimelineOverflow == 0u", "high-churn overflow stress", failures)
        require(text["soft_renderer"], "NativeGPU2DFrame.Reset()", "stale rejection", failures)
        require(text["soft_renderer"], "NativeGPU2DRecordedFrameSerial", "recorded identity", failures)
        require(text["soft_renderer"], "NativeGPU2DFrame.CaptureCaptureStateForLine(line)",
            "capture write-boundary sampling", failures)
        if "&& !GPU.CaptureEnable" in text["soft_renderer"]:
            failures.append("soft renderer: CaptureEnable still disables native GPU2D ownership")
        require(text["native_recorder"], "CaptureJournalWritesForLine", "native recorder journal", failures)
        for needle in (
            "CaptureNativeMappingForLine",
            "NativeOwnedMappedCpuRead",
            "NativeOwnedMappedCpuMaterialized",
            "MELONPRIME_GPU2D_PROOF_MATERIALIZE_MAPPED_CAPTURE",
            "FinalizeMappedCaptureDiagnostics",
            "MaterializeVRAMCaptureBlockForGPU2DProof",
        ):
            require(text["native_recorder"], needle,
                "mapped capture stale-read tripwire", failures)
        require(text["native_recorder"], "LCDVRAMProvenance", "native recorder capture provenance", failures)
        require(text["native_recorder"], "CaptureCoherentLCDVRAMForLine", "native recorder coherent capture path", failures)
        require(text["native_recorder"], "A byte difference between CPU VRAM", "event-driven authority comment", failures)
        require(text["native_header"], "NativeCaptureHostCopyDiagnostics",
            "native host-copy diagnostics", failures)
        for needle in (
            "CaptureOffsetHalfwords",
            "CaptureOffsetBytes",
            "WrapLCDCHalfword",
            "WrapLCDCByte",
            "CaptureAddressDiagnostic",
            "MaxCaptureAddressDiagnostics",
        ):
            require(text["native_header"], needle,
                "capture address unit contract", failures)
        require(text["native_recorder"], "RecordNativeOwnedCaptureCopySkipped",
            "native host-copy skip accounting", failures)
        for needle in (
            "BeginCaptureAddressDiagnostic",
            "RecordCaptureAddressLine",
            "FinalizeCaptureAddressDiagnostics",
            "GPU2DCaptureAddress",
            "destinationAddressMismatch",
            "sourceBAddressMismatch",
            "neighborBankCorruption",
            "provenanceExpectedFirstByte",
        ):
            require(text["native_recorder"], needle,
                "capture address diagnostics", failures)
        require(text["soft_renderer"], "Allocating a Display Capture destination does not make CPU VRAM coherent", "allocation authority comment", failures)
        require(text["soft_renderer"], "CaptureAuthorityTransitionReason reason", "reasoned invalidation", failures)
        require(text["soft_renderer"], "MarkCaptureCpuCoherent(bank, start, len, reason)", "reasoned CPU authority", failures)
        for label in ("gpu_header", "gpu_core", "native_header", "native_recorder", "soft_renderer"):
            for forbidden in (
                "ReconcileNativeCaptureCpuDifferences",
                "MarkCaptureAllocationCpuCoherent",
                "stale_cpu_replay_detected",
                "staleCaptureBlocks",
            ):
                forbid(text[label], forbidden, f"{label} authority hardening", failures)
        soft_alloc_start = text["soft_renderer"].find("void SoftRenderer::AllocCapture(")
        soft_alloc_end = text["soft_renderer"].find("CaptureSyncResult SoftRenderer::SyncVRAMCapture(", soft_alloc_start)
        if soft_alloc_start < 0 or soft_alloc_end < 0:
            failures.append("soft renderer: could not isolate AllocCapture authority boundary")
        else:
            forbid(
                text["soft_renderer"][soft_alloc_start:soft_alloc_end],
                "MarkCaptureCpuCoherent",
                "soft renderer allocation authority boundary",
                failures,
            )
        require(text["native_recorder"], "GPU2DWriteKind::CaptureSync", "native recorder capture-sync journal", failures)
        require(text["native_header"], "LCDVRAMProvenance", "host-only capture provenance", failures)
        if "CaptureNativeDisplayLine" in text["soft_renderer"]:
            failures.append("soft renderer: per-line CPU native capture mirror still present")

        for label in ("vulkan_renderer", "dx12_renderer"):
            require(text[label], "CaptureYOffset", f"{label} line dispatch", failures)
            require(text[label], "NativeGPU2DDispatchCount", f"{label} dispatch accounting", failures)
            require(text[label], "PublishedOutputGeneration", f"{label} published identity", failures)
            require(text[label], "DivRoundUp(256u, 128u)",
                f"{label} logical-width dispatch", failures)
            require(text[label], "384u, 1u);",
                f"{label} logical full-frame dispatch", failures)
            require(text[label], "static_cast<u32>(ScaleFactor), 1u)",
                f"{label} capture line dispatch", failures)
            require(text[label], "LastSemanticFrame", f"{label} semantic frame identity", failures)
            require(text[label], "LastSemanticCaptureGeneration",
                f"{label} semantic capture identity", failures)
            require(text[label], "semanticCaptureGenerationRegressed",
                f"{label} capture-generation regression recovery", failures)
            require(text[label], "LastSemanticEpoch", f"{label} semantic epoch", failures)
            require(text[label], "NativeCaptureStateInitialized",
                f"{label} persistent LCDC mirror ownership", failures)
            require(text[label], "Presentation backpressure is allowed to drop a visible frame",
                f"{label} semantic/presentation separation", failures)
            require(text[label], "RecordNativeOwnedCaptureCopySkipped",
                f"{label} native host-copy skip accounting", failures)
            require(text[label], "RecordNativeOwnedHostReupload",
                f"{label} native host-copy fail-closed counter", failures)
            require(text[label], "assert(!nativeOwner)",
                f"{label} native host-copy assertion", failures)

        vulkan_native = text["vulkan_renderer"].split(
            "bool VulkanRenderer3D::ComposeNativeGPU2D(", 1)[1]
        if "TryBeginFrame" in vulkan_native.split(
            "bool VulkanRenderer3D::CanComposeNativeGPU2D", 1
        )[0]:
            failures.append("Vulkan native GPU2D: presentation TryBeginFrame still gates semantics")
        require(vulkan_native, "ComposeFrames.BeginFrame()",
            "Vulkan native semantic command admission", failures)
        require(text["dx12_renderer"], "semanticSlot.Commands.Begin()",
            "DX12 native semantic command admission", failures)

        for label in ("vulkan_frame", "dx12_frame"):
            require(text[label], "Epoch", f"{label} frame epoch", failures)
            require(text[label], "DirectContentValid", f"{label} direct-content validity", failures)
        require(text["vulkan_presenter"], "IsPresentedFrameIdentityMonotonic",
            "Vulkan presentation serial/epoch rejection", failures)
        require(text["vulkan_screen"], "SetPresentedFrameIdentity",
            "Vulkan presentation identity admission", failures)
        require(text["dx12_presenter"], "IsPresentedFrameIdentityMonotonic",
            "DX12 presentation serial/epoch rejection", failures)
        require(text["dx12_screen"], "SetPresentedFrameIdentity",
            "DX12 presentation identity admission", failures)
        require(text["screen_header"], "NativeVisibilityState",
            "native visibility identity state", failures)
        require(text["screen_header"], "FirstCompleteFrameVisible",
            "native first-complete visibility gate", failures)
        require(text["screen_header"], "LastAcceptedSerial",
            "native accepted serial state", failures)
        require(text["native_header"], "AllocateRendererEpoch",
            "process-wide renderer epoch allocator", failures)
        require(text["native_recorder"], "ConsumeForcedPresentationStallFrame",
            "developer presentation stall hook", failures)
        for label in ("vulkan_renderer", "dx12_renderer"):
            require(text[label], "forcedPresentationStall",
                f"{label} forced stall evidence", failures)
            require(text[label], "mirrorNeedsFullCopy",
                f"{label} persistent mirror resync", failures)

        require(text["vulkan_frontend"], "stale_generation_reject", "Vulkan stale diagnostic", failures)
        require(text["dx12_frontend"], "stale_generation_reject", "DX12 stale diagnostic", failures)
        require(text["vulkan_frontend"], "SyncVRAMCapture", "Vulkan capture ownership", failures)
        require(text["dx12_frontend"], "SyncVRAMCapture", "DX12 capture ownership", failures)
        for label in ("vulkan_frontend", "dx12_frontend"):
            require(text[label], "GetCaptureProvenanceForRange", f"{label} provenance lookup", failures)
            require(text[label], "ReadNativeCapture", f"{label} demand-driven readback", failures)
            require(text[label], "RecordGPU2DCaptureSync", f"{label} readback journal", failures)
            sync_marker = "VulkanRenderer::SyncVRAMCapture(" \
                if label == "vulkan_frontend" else "DX12Renderer::SyncVRAMCapture("
            sync_start = text[label].find(sync_marker)
            sync_end = text[label].find("::InvalidateVRAMCapture(", sync_start)
            if sync_start < 0 or sync_end < 0:
                failures.append(f"{label}: could not isolate capture sync method")
            else:
                sync_body = text[label][sync_start:sync_end]
                if "HasNativeGPU2DFrameForCurrentEmulatedFrame" in sync_body:
                    failures.append(
                        f"{label}: current FrameRecorder identity still gates capture authority")
                require(
                    sync_body,
                    "IsNativeCaptureOwner",
                    f"{label} native-owner branch",
                    failures,
                )
                require(
                    sync_body,
                    "SoftRenderer::SyncVRAMCapture",
                    f"{label} CPU-coherent branch",
                    failures,
                )

        for label in ("vulkan_renderer", "dx12_renderer"):
            require(text[label], "expected.Owner", f"{label} expected capture identity", failures)
            require(text[label], "expected.CaptureGeneration", f"{label} capture generation validation", failures)
            require(text[label], "expected.CompletionValue", f"{label} completion validation", failures)
            require(text[label], "NativeSemanticSubmissionSerial", f"{label} global semantic submission identity", failures)
            require(text[label], "LastNativeCaptureCompletionValue", f"{label} completion provenance", failures)
            require(text[label], "input.LCDVRAMProvenance", f"{label} mirror ownership filter", failures)
            require(
                text[label],
                "MELONPRIME_TEST_GPU2D_CAPTURE_READBACK_FAIL",
                f"{label} fail-closed readback hook",
                failures,
            )
            if label == "dx12_renderer":
                require(text[label], "WaitForSubmittedValue", "DX12 scoped capture fence", failures)
                if "LastNativeCaptureCompletionValue = semanticSlot.Commands.GetSubmittedValue()" in text[label]:
                    failures.append("DX12 native capture provenance: local semantic-slot fence remains the identity")
                readback_start = text[label].find("DX12Renderer3D::ReadNativeCapture(")
                if readback_start >= 0:
                    readback_body = text[label][readback_start:]
                    if "CaptureCommands.WaitIdle()" in readback_body.split(
                        "NativeCaptureStateIdentity DX12Renderer3D::GetNativeCaptureStateIdentity(", 1
                    )[0]:
                        failures.append("DX12 native capture readback: queue/device idle wait remains")
            elif "LastNativeCaptureCompletionValue = ComposeFrames.GetLastSubmittedFrameNumber()" in text[label]:
                failures.append("Vulkan native capture provenance: presentation frame-ring value remains the identity")

        for label in ("vulkan_shader", "dx12_shader"):
            require(text[label], "TimelineVersion", f"{label} temporal shader", failures)
            require(text[label], "SpriteTimelineVersion", f"{label} OBJ/OAM latch shader", failures)
            require(text[label], "RowId", f"{label} sparse timeline shader", failures)
            require(text[label], "LCDVRAMMap", f"{label} per-line LCDC shader", failures)
            require(text[label], "linePass", f"{label} line shader pass", failures)
            require(text[label], "scaledScreens", f"{label} high-resolution line shader", failures)
            require(
                text[label],
                "NativeCachedOAM16" if label == "dx12_shader" else "CachedOAM16",
                f"{label} shared OAM cache",
                failures,
            )
            require(
                text[label],
                "NativeSpriteActiveForLine",
                f"{label} active sprite prefilter",
                failures,
            )
            require(
                text[label],
                "NativePrepareObjRawLine"
                    if label == "dx12_shader"
                    else "PrepareNativeObjRawLine",
                f"{label} scanline OBJ preparation",
                failures,
            )
            require(text[label], "ForcedBlank", f"{label} Stage A blank semantics", failures)
            require(text[label], "UnitEnabled", f"{label} Stage A unit semantics", failures)
            for helper in (
                "CaptureOffsetHalfwords",
                "CaptureOffsetBytes",
                "WrapLCDCHalfword",
                "WrapLCDCByte",
            ):
                require(text[label], helper, f"{label} capture address helper", failures)
            forbid_regex(
                text[label],
                r"\(\s*\(\s*(?:captureCnt|cnt)\s*>>\s*"
                r"(?:18u|26u)\s*\)\s*&\s*3u\s*\)\s*<<\s*14u",
                f"{label} byte-address capture unit audit",
                failures,
            )
            require(text[label], "0x1FFFFu", f"{label} LCDC byte wrap", failures)
            for helper in (
                "NativeCaptureByte",
                "NativeCaptureMappingMask",
                "NativeCaptureOBJMappingBase",
                "NativeCaptureSpriteOBJMappingBase",
                "NativeCaptureOverlayByte",
                "useSpriteLatch",
            ):
                require(text[label], helper,
                    f"{label} GPU-resident mapped capture overlay", failures)
        require(text["vulkan_shader"], "local_size_x = 128, local_size_y = 1",
            "Vulkan scanline workgroup", failures)
        require(text["dx12_shader"], "[numthreads(128, 1, 1)]",
            "DX12 scanline workgroup", failures)
        require_regex(
            text["vulkan_shader"],
            r"uint\s+TimelineVersion\(uint\s+line,\s*uint\s+block\)",
            "Vulkan explicit temporal line",
            failures,
        )
        require_regex(
            text["dx12_shader"],
            r"uint\s+NativeTimelineVersion\(uint\s+line,uint\s+block\)",
            "DX12 explicit temporal line",
            failures,
        )
        vulkan_temporal_prefix = text["vulkan_shader"].split("void main", 1)[0]
        dx12_native_shader = text["dx12_shader"].split(
            "inline const std::string GPU2DNative", 1
        )[1]
        dx12_temporal_prefix = dx12_native_shader.split(
            "#ifdef GPU2DNativeCapture", 1
        )[0]
        if "pc.CaptureYOffset" in vulkan_temporal_prefix:
            failures.append("Vulkan temporal helpers still read CaptureYOffset")
        if "InterpSpanCount" in dx12_temporal_prefix:
            failures.append("DX12 temporal helpers still read InterpSpanCount")
        if ("WriteNativeCaptureSample" not in text["vulkan_shader"]
                and "WriteNativeCapturePair" not in text["vulkan_shader"]):
            failures.append("Vulkan capture feedback: missing native capture sample writer")
        require(text["dx12_shader"], "NativeCaptureSourceA", "DX12 capture feedback", failures)
        require(text["vulkan_shader"], "NativeCaptureReference", "Vulkan capture reference", failures)
        require(text["dx12_shader"], "NativeCaptureReference", "DX12 capture reference", failures)
        require(text["dx12_shader"], "NativeCaptureCnt = 55u", "DX12 capture count ABI", failures)
        require(text["dx12_shader"], "NativeCaptureEnable = 56u", "DX12 capture enable ABI", failures)
        require_regex(
            text["dx12_shader"],
            r"uint cnt=NativeLine\(0u,line,NativeCaptureCnt\);\s*"
            r"if\(cnt==0u\|\|NativeLine\(0u,line,NativeCaptureEnable\)==0u\)return;",
            "DX12 capture control word order",
            failures,
        )

        vulkan_source_b = text["vulkan_shader"].split(
            "uint CaptureSourceB", 1)[1].split("uint CaptureCompositeRaw", 1)[0]
        vulkan_reference = text["vulkan_shader"].split(
            "uint NativeCaptureReference", 1)[1].split(
                "void WriteNativeCaptureSample", 1)[0]
        vulkan_writer = text["vulkan_shader"].split(
            "void WriteNativeCaptureSample", 1)[1].split(
                "void WriteStructuredPixel", 1)[0]
        dx12_source_b = text["dx12_shader"].split(
            "uint NativeCaptureSourceB", 1)[1].split(
                "uint NativeCaptureComposite", 1)[0]
        dx12_reference = text["dx12_shader"].split(
            "uint NativeCaptureReference", 1)[1].split(
                "void NativeWriteCaptureSample", 1)[0]
        dx12_writer = text["dx12_shader"].split(
            "void NativeWriteCaptureSample", 1)[1].split(
                "static const uint NativeStructuredPlaneStride", 1)[0]
        for label, body in (
            ("Vulkan source-B", vulkan_source_b),
            ("DX12 source-B", dx12_source_b),
        ):
            require(body, "CaptureOffsetBytes", f"{label} byte offset helper", failures)
            require(body, "WrapLCDCByte", f"{label} byte wrap helper", failures)
        for label, body in (
            ("Vulkan reference", vulkan_reference),
            ("DX12 reference", dx12_reference),
        ):
            require(body, "CaptureOffsetHalfwords", f"{label} halfword helper", failures)
            forbid(body, "CaptureOffsetBytes", f"{label} halfword unit audit", failures)
        for label, body in (
            ("Vulkan compact writer", vulkan_writer),
            ("DX12 compact writer", dx12_writer),
        ):
            require(body, "CaptureOffsetBytes", f"{label} byte offset helper", failures)
            require(body, "WrapLCDCByte", f"{label} byte wrap helper", failures)
            forbid_regex(
                body,
                r"(?:captureCnt|cnt)\s*>>\s*18u[\s\S]{0,80}<<\s*14u",
                f"{label} direct halfword offset in byte writer",
                failures,
            )

        require(text["opengl_renderer"], "DumpFrameForValidation", "OpenGL pixel gate", failures)
        require(text["opengl_renderer"], "glReadPixels(0, 0, 256, 192", "OpenGL pixel gate", failures)
        require(text["opengl_renderer"], "ScaleFactor != 1", "OpenGL scale gate", failures)
        require(text["opengl_renderer"], "DumpGPU2DFrame", "OpenGL canonical dump", failures)

        for needle in (
            "[switch]$RequireCleanProvenance",
            "final acceptance requires detached HEAD",
            "checkout is dirty",
            "binary git_dirty=",
            "[switch]$NoFrameLimit",
            "LimitFPS = $(if ($NoFrameLimit)",
            "frame_limit =",
            "[ValidateRange(0,600)] [int]$CaptureFrames",
            "[ValidateRange(1,1000)] [int]$CaptureIntervalMs",
            "[ValidateRange(0,600)] [int]$PresentationStallFrames",
            "MELONPRIME_TEST_GPU2D_PRESENTATION_STALL_FRAMES",
            "Capture-ContinuousDisplay",
            "window_capture_frames",
        ):
            require(text["physical_runner"], needle, "clean provenance gate", failures)

        require(text["native_contract_test"], "RunTemporalLineVectors",
            "synthetic temporal vectors", failures)
        require(text["native_contract_test"], "RunCaptureOwnershipVectors",
            "capture ownership vectors", failures)
        require(text["native_contract_test"], "RunCaptureFeedbackVectors",
            "same-bank/display-mode2 vectors", failures)
        require(text["native_contract_test"], "RunMappedCaptureOverlayVectors",
            "mapped capture stale-poison/remap/latch vectors", failures)
        for needle in (
            "RunCaptureAddressVectors",
            "CaptureOffsetHalfwords",
            "CaptureOffsetBytes",
            "WrapLCDCHalfword",
            "WrapLCDCByte",
            "SoftwareCaptureBlockMask",
            "NativeCaptureBlockMask",
            "sourcePatterns",
            "targetBank",
            "scratchBefore",
            "0x18000u",
            "0x10000u",
            "600u",
            "display mode 2",
        ):
            require(text["native_contract_test"], needle,
                "capture address matrix/bank-wrap vectors", failures)
        for needle in (
            "poisoned CPU VRAM",
            "overlapping native capture banks",
            "mid-frame mapped-capture owner transition",
            "OBJ current/latch mapping",
            "scale-invariant",
            "MappedCaptureBytes",
        ):
            require(text["native_contract_test"], needle,
                "mapped capture overlay regression vectors", failures)
        for needle in (
            "CaptureSyncResult::Failed",
            "flagsBeforeFailure",
            "1200u",
            "PresentationStallObserved",
            "cross-frame native capture",
            "same-bank capture allocation",
            "display mode 2",
            "600u",
        ):
            require(text["native_contract_test"], needle, "capture ownership regression vectors", failures)

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

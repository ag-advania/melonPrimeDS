/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef DX12_GPU2D_COMPOSER_H
#define DX12_GPU2D_COMPOSER_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include <algorithm>
#include <array>

#include "DX12Common.h"
#include "DX12CommandContext.h"
#include "DX12DescriptorRing.h"
#include "DX12PresentedFrame.h"
#include "GPU2DNative.h"
#include "MelonPrimeStructuredComposition.h"
#include "RendererOutputRing.h"

namespace melonDS
{

class DX12Context;

// Buffer layout the GPU2D compositor consumes. Declared here rather than in
// the renderer's translation unit because these sizes describe the
// compositor's own resources; the renderer only needs them where it packs
// data for it.
namespace DX12Gpu2D
{

constexpr u32 kStructuredPixelCount = 256u * 192u;
constexpr u32 kStructuredCompositionInputDwords =
    (kStructuredPixelCount * 14u) + (192u * 2u) + (192u * 4u);
constexpr u64 kNativeGPU2DInputBytes = GPU2DNative::PackedFrameBytes();
// One input resource serves both compose paths, so it is sized for whichever
// of the two frame layouts is larger.
constexpr u32 kCompositionInputDwords = std::max<u32>(
    kStructuredCompositionInputDwords, GPU2DNative::PackedFrameWords);
constexpr u32 kCompositorFramesInFlight = 3;
static_assert(kNativeGPU2DInputBytes
        <= static_cast<u64>(kCompositionInputDwords) * sizeof(u32),
    "native GPU2D input must fit the compositor input resource");

} // namespace DX12Gpu2D

// The GPU2D compositor's own GPU objects on the DX12 backend.
//
// The compositor turns the software engines' 2D planes -- or the native GPU2D
// producer's packed frame -- into the composed screens the presenter shows. It
// is a different responsibility from 3D rasterization, and the audit graded it
// FAIL on both backends for living inside Renderer3D.
//
// This owns what is unambiguously the compositor's: its four compute
// pipelines, and the three descriptor rings that back its dispatches.
//
// What it does **not** do is take a descriptor table of its own. Every compute
// dispatch in this backend binds one fourteen-entry UAV table whose slots are
// shared with the rasterizer, and three of those slots mean different things
// depending on which shader runs. Giving the compositor its own table would
// mean recompiling the shaders against a different register layout -- a
// descriptor-strategy change, which the audit rules out folding into a
// responsibility move. So the table is still assembled from borrowed handles,
// exactly as it was, and only ownership of the compositor's own resources
// moved. That is the "structure only, identical behaviour" step the audit asks
// for first.
//
// Members are public because this is a resource owner, not an abstraction: the
// renderer's shared pipeline-build loop writes the pipelines, and the compose
// recording reads the rings. Wrapping each in a pair of accessors would add
// noise without adding a boundary.
class DX12Gpu2DComposer
{
public:
    // One canonical UAV block per compositor slot, plus the work-slot blocks
    // the native path binds. Sized by the caller from the root-signature
    // layout and the compositor's frames-in-flight, both of which belong to
    // the renderer.
    bool CreateDescriptors(
        ID3D12Device* device,
        u32 uavTableSize,
        u32 framesInFlight);

    void ShutdownDescriptors() noexcept;
    void ReleasePipelines() noexcept;

    // --- pipelines ---------------------------------------------------------
    //
    // Built by the renderer's shader-step loop, which owns the generated blob
    // table; their lifetime is owned here.
    DX12::ComPtr<ID3D12PipelineState> Compositor;
    DX12::ComPtr<ID3D12PipelineState> CorrectCoverage;
    DX12::ComPtr<ID3D12PipelineState> Native;
    DX12::ComPtr<ID3D12PipelineState> NativeCapture;

    // --- descriptors -------------------------------------------------------
    DX12DescriptorRing OutputUav;
    DX12DescriptorRing WorkOutputUav;
    DX12DescriptorRing WorkNativeUav;

    // Cached table bases, one per slot, so a dispatch does not re-derive them.
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 3> OutputUavCpu{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 3> WorkNativeUavCpu{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 12> WorkOutputUavCpu{};
};

// The compositor's resolution-dependent resource set: the presentation slots
// it composes into, the work slots Stage A and capture record against, and the
// ring that publishes a finished slot to the presenter.
//
// Separate from DX12Gpu2DComposer because the lifetimes differ. The pipelines
// and descriptor rings above are created once with the renderer; everything
// here is recreated whenever the internal resolution changes, and is held by
// shared_ptr so a lease the presenter still holds keeps its resources alive
// across that recreation.
class DX12Gpu2DOutput
{
public:
    struct ComposeWorkSlot
    {
        DX12CommandContext Commands;
        DX12DescriptorRing Descriptors;
        DX12::ComPtr<ID3D12Resource> NativeStaging;
        DX12::ComPtr<ID3D12Resource> NativeInput;
        DX12::ComPtr<ID3D12Resource> StructuredInput;
        // Developer-only resources are allocated on first diagnostic use.
        DX12::ComPtr<ID3D12Resource> DiagnosticComposed;
        DX12::ComPtr<ID3D12Resource> NativeReadback;
        DX12::ComPtr<ID3D12Resource> StructuredReadback;
        u8* NativeReadbackMapped = nullptr;
        u32* StructuredReadbackMapped = nullptr;
        u32* NativeMapped = nullptr;
        GPU2DNative::FrameGeneration UploadedNativeGeneration{};
        GPU2DNative::SemanticLineCache SemanticLines{};
        bool NativeUploadInitialized = false;

        bool EnsureDiagnosticResources(
            DX12Context& context,
            u64 outputBytes,
            u64 structuredBytes,
            bool needDiagnosticComposed,
            bool needStructuredReadback);
    };

    struct Slot
    {
        DX12CommandContext Commands;
        DX12DescriptorRing Descriptors;
        DX12::ComPtr<ID3D12Resource> StructuredStaging;
        DX12::ComPtr<ID3D12Resource> StructuredInput;
        DX12::ComPtr<ID3D12Resource> Composed;
        DX12::ComPtr<ID3D12Resource> DirectTexture;
        u32* StructuredMapped = nullptr;
        StructuredComposition::GenerationState UploadedContentGeneration{};
        bool StructuredUploadInitialized = false;
        bool DirectTextureInShaderResource = false;
        int PresentationWorkSlot = -1;
        DX12PresentedFrame Frame;
    };

    ~DX12Gpu2DOutput();

    // uavTableSize comes from the root-signature layout, which the renderer
    // owns: a slot's shader-visible ring has to match the table the shaders
    // were compiled against.
    bool Create(
        DX12Context& context, u32 width, u32 height, u32 uavTableSize,
        u64 resourceGeneration, u64 epoch);

    DX12Context* Context = nullptr;
    bool OwnsContextReference = false;
    bool DirectTextureEnabled = false;
    std::array<Slot, DX12Gpu2D::kCompositorFramesInFlight> Slots;
    std::array<ComposeWorkSlot, DX12Gpu2D::kCompositorFramesInFlight> WorkSlots;
    // Published slot, serial sequence, round-robin cursor and the per-slot
    // presenter refcounts. Backend-neutral: the Vulkan compositor runs the
    // same ring protocol against the same class.
    RendererOutputRing Ring{DX12Gpu2D::kCompositorFramesInFlight};
    u64 ResourceGeneration = 0;
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // DX12_GPU2D_COMPOSER_H

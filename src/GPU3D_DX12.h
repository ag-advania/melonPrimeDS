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

#ifndef GPU3D_DX12_H
#define GPU3D_DX12_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "GPU3D.h"
#include "DX12Context.h"
#include "GPU3D_TexcacheDX12.h"

namespace melonDS
{

// DirectX 12 3D renderer.
//
// It is paired with the software 2D renderer (see GPU_DX12.h) rather than a
// DX12 2D compositor: the 3D scene is rasterized on the GPU at the configured
// internal resolution, resolved back down to the DS's native 256x192 in the
// exact word format GetLine() must return, and handed to the existing software
// compositor. That keeps display capture, savestates, the Custom HUD, the OSD
// and both Qt presentation paths working unchanged, and makes internal
// resolution behave as supersampling.
//
// The pipeline is modeled on the OpenGL compute renderer (GPU3D_Compute.cpp),
// not the fixed-function OpenGL one, so the same tile-binned compute design can
// be filled in stage by stage.
class DX12Renderer3D : public Renderer3D
{
public:
    // Returns nullptr when DX12 is unavailable or device-side setup failed, so
    // the caller can fall back instead of constructing a dead renderer.
    static std::unique_ptr<DX12Renderer3D> New(melonDS::GPU3D& gpu3D);

    ~DX12Renderer3D() override;

    bool Init() override;
    void Reset() override;

    // Releases every GPU-visible object while the emulator core is still alive.
    void Stop();

    void SetRenderSettings(int scale, bool betterPolygons, bool hiresCoordinates);
    [[nodiscard]] int GetScaleFactor() const noexcept { return ScaleFactor; }

    void RenderFrame() override;
    u32* GetLine(int line) override;

    bool NeedsShaderCompile() override { return ShaderStepIdx < ShaderStepCount; }
    void ShaderCompileStep(int& current, int& count) override;

private:
    explicit DX12Renderer3D(melonDS::GPU3D& gpu3D);

    // Mirrors the OpenGL compute renderer's MetaUniform. Field order and
    // padding match the HLSL cbuffer in GPU3D_DX12_shaders.h.
    struct MetaUniform
    {
        u32 NumPolygons;
        u32 NumVariants;
        u32 AlphaRef;
        u32 DispCnt;

        u32 ToonTable[4 * 34];

        u32 ClearColor;
        u32 ClearDepth;
        u32 ClearAttr;
        u32 FogOffset;

        u32 FogShift;
        u32 FogColor;
        float ClearBitmapOffset[2];
    };
    static_assert(sizeof(MetaUniform) % 16 == 0, "MetaUniform must stay 16-byte aligned");

    // Root constants, mirroring the HLSL DispatchUniform cbuffer.
    struct DispatchUniform
    {
        u32 CurVariant;
        u32 TexIsCapture;
        float TextureSize[2];
        float CaptureYOffset;
        u32 Pad[3];
    };
    static constexpr u32 DispatchUniformDwords = sizeof(DispatchUniform) / 4;

    enum ShaderStep
    {
        ShaderStep_ClearPlane = 0,
        ShaderStep_FinalPass0,
        ShaderStep_FinalPassLast = ShaderStep_FinalPass0 + 7,
        ShaderStep_Resolve,
        ShaderStepCount,
    };

    bool CreateRootSignature();
    bool CreateScaleDependentResources();
    void ReleaseScaleDependentResources();
    void ReleasePipelines();

    // Assembles the shared prologue (`#define`s + Common) and compiles one
    // compute PSO.
    bool BuildPipeline(
        DX12::ComPtr<ID3D12PipelineState>& pipeline,
        const std::string& body,
        const std::vector<std::string>& defines,
        const char* debugName);

    void UpdateClearBitmap();
    void UploadMetaUniform(ID3D12GraphicsCommandList* list);
    void SetDispatchConstants(ID3D12GraphicsCommandList* list, const DispatchUniform& constants);
    void InsertUavBarrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource);

    // Writes `count` structured-buffer UAV descriptors (null where the resource
    // is missing) and binds them as the UAV table.
    struct ViewEntry
    {
        ID3D12Resource* Resource = nullptr;
        // Structured-buffer element count; 0 marks the entry as a 256x256
        // R32_UINT texture instead.
        u32 Elements = 0;
    };

    bool BindUavTable(ID3D12GraphicsCommandList* list, std::initializer_list<ViewEntry> entries);
    bool BindSrvTable(ID3D12GraphicsCommandList* list, std::initializer_list<ViewEntry> entries);

    void EnsureFrameReadback();

    DX12Context* Context = nullptr;
    DX12CommandContext Commands;
    DX12UploadRing Uploads;
    DX12DescriptorRing Descriptors;
    DX12TextureHeap TextureHeap;

    TexcacheDX12 Texcache;

    DX12::ComPtr<ID3D12RootSignature> RootSignature;
    DX12::ComPtr<ID3D12PipelineState> PipelineClearPlane;
    std::array<DX12::ComPtr<ID3D12PipelineState>, 8> PipelineFinalPass;
    DX12::ComPtr<ID3D12PipelineState> PipelineResolve;

    // Scale-dependent GPU memory.
    DX12::ComPtr<ID3D12Resource> ResultBuffer;   // color/depth/attr, 2 layers each
    DX12::ComPtr<ID3D12Resource> FinalFBBuffer;  // packed r6g6b6a5 at internal res
    DX12::ComPtr<ID3D12Resource> ResolveBuffer;  // packed r6g6b6a5 at 256x192
    DX12::ComPtr<ID3D12Resource> ReadbackBuffer;

    DX12::ComPtr<ID3D12Resource> ClearBitmapTex[2];

    std::unique_ptr<u32[]> ClearBitmap[2];
    u8 ClearBitmapDirty = 0x3;
    // The textures are created in COPY_DEST, so the first upload of each slot
    // must not transition into a state it is already in.
    bool ClearBitmapTexInCopyDest[2] = { true, true };

    int ScaleFactor = -1;
    int ScreenWidth = 256;
    int ScreenHeight = 192;
    bool BetterPolygons = false;
    bool HiresCoordinates = false;

    int ShaderStepIdx = 0;

    // Set when RenderFrame() submitted work whose readback GetLine() still has
    // to wait for.
    bool FrameInFlight = false;
    bool FrameReadbackValid = false;

    alignas(64) std::array<u32, 256 * 192> ColorBuffer{};
    alignas(8) u32 ScrolledLine[256]{};
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // GPU3D_DX12_H

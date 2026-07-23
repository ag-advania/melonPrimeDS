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

#ifndef GPU3D_H
#define GPU3D_H

#include <array>
#include <memory>

#include "Savestate.h"
#include "FIFO.h"

namespace melonDS
{
class GPU;

struct Vertex
{
    s32 Position[4];
    s32 Color[3];
    s16 TexCoords[2];

    bool Clipped;

    // final vertex attributes.
    // allows them to be reused in polygon strips.

    s32 FinalPosition[2];
    s32 FinalColor[3];

    // hi-res position (4-bit fractional part)
    // TODO maybe: hi-res color? (that survives clipping)
    s32 HiresPosition[2];

    void DoSavestate(Savestate* file) noexcept;
};

struct Polygon
{
    Vertex* Vertices[10];
    u32 NumVertices;

    s32 FinalZ[10];
    s32 FinalW[10];
    bool WBuffer;

    u32 Attr;
    u32 TexParam;
    u32 TexPalette;

    bool Degenerate;

    bool FacingView;
    bool Translucent;

    bool IsShadowMask;
    bool IsShadow;

    int Type; // 0=regular 1=line

    u32 VTop, VBottom; // vertex indices
    s32 YTop, YBottom; // Y coords
    s32 XTop, XBottom; // associated X coords

    u32 SortKey;

    void DoSavestate(Savestate* file) noexcept;
};

class Renderer3D;
class NDS;

class GPU3D
{
public:
    GPU3D(melonDS::GPU& gpu) noexcept;
    ~GPU3D() noexcept = default;
    void Reset() noexcept;

    void DoSavestate(Savestate* file) noexcept;

    void SetEnabled(bool geometry, bool rendering) noexcept;

    void ExecuteCommand() noexcept;

    s32 CyclesToRunFor() const noexcept;
    void Run() noexcept;
    void CheckFIFOIRQ() noexcept;
    void CheckFIFODMA() noexcept;

    void VBlank() noexcept;

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // Sapphire-generation (GPU&-parameter) timeline hook, called at the identical
    // VCount==215 dispatch site reference melonDS uses (see GPU::StartHBlank in
    // GPU.cpp). Unlike the reference fork, our GPU3D does not own the active
    // Renderer3D, so this only updates GPU3D-owned state (RenderScreenSwapAt3D);
    // the actual render kickoff continues to happen through Renderer::Start3DRendering(),
    // which VulkanGpuRenderer overrides to reach VulkanRenderer3D::RenderFrame(GPU&).
    void VCount215(melonDS::GPU& gpu) noexcept;
#endif

    void SetRenderXPos(u16 xpos, u16 mask) noexcept;
    [[nodiscard]] u16 GetRenderXPos() const noexcept { return RenderXPos; }

    void WriteToGXFIFO(u32 val) noexcept;

    u8 Read8(u32 addr) noexcept;
    u16 Read16(u32 addr) noexcept;
    u32 Read32(u32 addr) noexcept;
    void Write8(u32 addr, u8 val) noexcept;
    void Write16(u32 addr, u16 val) noexcept;
    void Write32(u32 addr, u32 val) noexcept;

private:

    typedef union
    {
        u64 _contents;
        struct
        {
            u32 Param;
            u8 Command;
        };

    } CmdFIFOEntry;

    void UpdateClipMatrix() noexcept;
    void ResetRenderingState() noexcept;
    void AddCycles(s32 num) noexcept;
    void NextVertexSlot() noexcept;
    void StallPolygonPipeline(s32 delay, s32 nonstalldelay) noexcept;
    void SubmitPolygon() noexcept;
    void SubmitVertex() noexcept;
    void CalculateLighting() noexcept;
    void BoxTest(const u32* params) noexcept;
    void PosTest() noexcept;
    void VecTest(u32 param) noexcept;
    void CmdFIFOWrite(const CmdFIFOEntry& entry) noexcept;
    CmdFIFOEntry CmdFIFORead() noexcept;
    void FinishWork(s32 cycles) noexcept;
    void VertexPipelineSubmitCmd() noexcept
    {
        // vertex commands 0x24, 0x25, 0x26, 0x27, 0x28
        if (!(VertexSlotsFree & 0x1)) NextVertexSlot();
        else                          AddCycles(1);
        NormalPipeline = 0;
    }

    void VertexPipelineCmdDelayed6() noexcept
    {
        // commands 0x20, 0x30, 0x31, 0x72 that can run 6 cycles after a vertex
        if (VertexPipeline > 2) AddCycles((VertexPipeline - 2) + 1);
        else                    AddCycles(NormalPipeline + 1);
        NormalPipeline = 0;
    }

    void VertexPipelineCmdDelayed8() noexcept
    {
        // commands 0x29, 0x2A, 0x2B, 0x33, 0x34, 0x41, 0x60, 0x71 that can run 8 cycles after a vertex
        if (VertexPipeline > 0) AddCycles(VertexPipeline + 1);
        else                    AddCycles(NormalPipeline + 1);
        NormalPipeline = 0;
    }

    void VertexPipelineCmdDelayed4() noexcept
    {
        // all other commands can run 4 cycles after a vertex
        // no need to do much here since that is the minimum
        AddCycles(NormalPipeline + 1);
        NormalPipeline = 0;
    }

public:
    melonDS::NDS& NDS;
    melonDS::GPU& GPU;

    FIFO<CmdFIFOEntry, 256> CmdFIFO {};
    FIFO<CmdFIFOEntry, 4> CmdPIPE {};

    FIFO<CmdFIFOEntry, 64> CmdStallQueue {};

    u32 ZeroDotWLimit = 0xFFFFFF;

    u32 GXStat = 0;

    u32 ExecParams[32] {};
    u32 ExecParamCount = 0;

    s32 CycleCount = 0;
    s32 VertexPipeline = 0;
    s32 NormalPipeline = 0;
    s32 PolygonPipeline = 0;
    s32 VertexSlotCounter = 0;
    u32 VertexSlotsFree = 0;

    u32 NumPushPopCommands = 0;
    u32 NumTestCommands = 0;


    u32 MatrixMode = 0;

    s32 ProjMatrix[16] {};
    s32 PosMatrix[16] {};
    s32 VecMatrix[16] {};
    s32 TexMatrix[16] {};

    s32 ClipMatrix[16] {};
    bool ClipMatrixDirty = false;

    u32 Viewport[6] {};

    s32 ProjMatrixStack[16] {};
    s32 PosMatrixStack[32][16] {};
    s32 VecMatrixStack[32][16] {};
    s32 TexMatrixStack[16] {};
    s32 ProjMatrixStackPointer = 0;
    s32 PosMatrixStackPointer = 0;
    s32 TexMatrixStackPointer = 0;

    u32 NumCommands = 0;
    u32 CurCommand = 0;
    u32 ParamCount = 0;
    u32 TotalParams = 0;

    bool GeometryEnabled = false;
    bool RenderingEnabled = false;

    u32 DispCnt = 0;
    u8 AlphaRefVal = 0;
    u8 AlphaRef = 0;

    u16 ToonTable[32] {};
    u16 EdgeTable[8] {};

    u32 FogColor = 0;
    u32 FogOffset = 0;
    u8 FogDensityTable[32] {};

    u32 ClearAttr1 = 0;
    u32 ClearAttr2 = 0;

    u32 RenderDispCnt = 0;
    u8 RenderAlphaRef = 0;

    u16 RenderToonTable[32] {};
    u16 RenderEdgeTable[8] {};

    u32 RenderFogColor = 0;
    u32 RenderFogOffset = 0;
    u32 RenderFogShift = 0;
    u8 RenderFogDensityTable[34] {};

    u32 RenderClearAttr1 = 0;
    u32 RenderClearAttr2 = 0;

    bool RenderFrameIdentical = false; // not part of the hardware state, don't serialize

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // Verbatim from reference melonDS-android-lib GPU3D.h (~:276): set at VCount==215
    // (see GPU3D::VCount215() / GPU::StartHBlank in GPU.cpp) and consumed by
    // VulkanRenderer3D via gpu.GPU3D.RenderScreenSwapAt3D in GPU3D_Vulkan.cpp.
    bool RenderScreenSwapAt3D = false;
#endif

    u16 RenderXPos = 0;

    bool AbortFrame = false;

    u64 Timestamp = 0;


    u32 PolygonMode = 0;
    s16 CurVertex[3] {};
    u8 VertexColor[3] {};
    s16 TexCoords[2] {};
    s16 RawTexCoords[2] {};
    s16 Normal[3] {};

    s16 LightDirection[4][3] {};
    s32 SpecRecip[4] {};
    u8 LightColor[4][3] {};
    u8 MatDiffuse[3] {};
    u8 MatAmbient[3] {};
    u8 MatSpecular[3] {};
    u8 MatEmission[3] {};

    bool UseShininessTable = false;
    u8 ShininessTable[128] {};

    u32 PolygonAttr = 0;
    u32 CurPolygonAttr = 0;

    u32 TexParam = 0;
    u32 TexPalette = 0;

    s32 PosTestResult[4] {};
    s16 VecTestResult[3] {};

    Vertex TempVertexBuffer[4] {};
    u32 VertexNum = 0;
    u32 VertexNumInPoly = 0;
    u32 NumConsecutivePolygons = 0;
    Polygon* LastStripPolygon = nullptr;
    u32 NumOpaquePolygons = 0;

    Vertex VertexRAM[6144 * 2] {};
    Polygon PolygonRAM[2048 * 2] {};

    Vertex* CurVertexRAM = nullptr;
    Polygon* CurPolygonRAM = nullptr;
    u32 NumVertices = 0;
    u32 NumPolygons = 0;
    u32 CurRAMBank = 0;

    std::array<Polygon*,2048> RenderPolygonRAM {};
    u32 RenderNumPolygons = 0;

    u32 FlushRequest = 0;
    u32 FlushAttributes = 0;
};

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
// Union scaffold: this Renderer3D base now has to serve two renderer generations at
// once -- the original no-arg-method generation (SoftRenderer3D, GLRenderer3D,
// ComputeRenderer3D, MetalRenderer3D, MetalComputeRenderer3D) and the Sapphire-ported
// GPU&-parameter generation (VulkanRenderer3D, in the verbatim-copied, never-edited
// src/GPU3D_Vulkan.h/.cpp). Reset()/RenderFrame() are relaxed from pure virtual to
// default (no-op) bodies so a VulkanRenderer3D -- which only implements the
// GPU&-parameter overload of each -- can be instantiated without also having to
// implement the legacy no-arg overload.
class Renderer3D
{
public:
    virtual ~Renderer3D() = default;

    Renderer3D(const Renderer3D&) = delete;
    Renderer3D& operator=(const Renderer3D&) = delete;
    virtual bool Init() { return true; }
    virtual void Reset() {}

    virtual void RenderFrame() {}
    virtual void FinishRendering() {}
    virtual void RestartFrame() {};

    // return one scanline of the framebuffer, with X scroll applied
    // this is used in software renderers
    virtual u32* GetLine(int line) = 0;

    virtual bool NeedsShaderCompile() { return false; }
    virtual void ShaderCompileStep(int& current, int& count) {}

    // --- Sapphire-generation (GPU&-parameter) surface -----------------------------
    // Mirrors the copied src/GPU3D_Vulkan.h `override`s exactly (signatures, const,
    // and noexcept qualifiers). VulkanRenderer3D is the only current implementor;
    // every other subclass inherits these no-op defaults untouched.
    const bool Accelerated;

    virtual void Reset(GPU& gpu) {}
    virtual void VCount144(GPU& gpu) {}
    virtual void RenderFrame(GPU& gpu) {}
    virtual void RestartFrame(GPU& gpu) {}
    virtual void Blit(const GPU& gpu) {}
    virtual void Stop(const GPU& gpu) {}
    virtual void SetupAccelFrame() {}
    virtual void PrepareCaptureFrame() {}
    virtual void BeginCaptureFrame() {}
    virtual void SetCaptureScreenSwapHint(bool screenSwap) {}
    [[nodiscard]] virtual bool UsesStructured2DMetadata() const noexcept { return false; }

    // MELONPRIME-PORT: reference-generation (melonDS-android-lib) GPU3D.h:360-361
    // (SetOutputTexture/BindOutputTexture), ported verbatim as empty-bodied virtuals. Reference's
    // GLRenderer/ComputeRenderer (old-generation Renderer3D subclasses with no equivalent in this
    // fork -- our GL/Compute Renderer3D subclasses are GLRenderer3D/ComputeRenderer3D, a different
    // hierarchy) are the only reference overriders; the verbatim-copied Sapphire VulkanRenderer3D
    // (src/GPU3D_Vulkan.h/.cpp) doesn't override either, so it inherits these no-op defaults --
    // matching reference's own Sapphire VulkanRenderer3D, which likewise never overrides them.
    // Needed so MelonInstanceVulkan.cpp's `nds->GPU.GetRenderer3D().SetOutputTexture(...)` call
    // (guarded, in practice unreachable for this Vulkan-only MelonInstance -- see call site comment)
    // type-checks against the base Renderer3D& it's called through, without a downcast.
    virtual void SetOutputTexture(int buffer, u32 texture) {}
    virtual void BindOutputTexture(int buffer) {}

protected:
    // Bound by the GPU_Vulkan glue immediately before constructing a Sapphire-generation
    // renderer (whose verbatim base-ctor call is Renderer3D(true), carrying no GPU3D&);
    // see MelonPrimeSetAcceleratedRendererCtorContext() below.
    explicit Renderer3D(bool Accelerated);
};

// Called by the GPU_Vulkan glue immediately before constructing a Sapphire-generation
// Renderer3D (and cleared right after); verified (via assert) by Renderer3D(bool)'s
// definition in GPU3D.cpp as a calling-convention contract. Renderer3D itself has no
// GPU&/GPU3D& members to bind from this context -- see Renderer3DLegacyBase below for
// why -- so today this is a documented hook for any future accelerated-renderer
// generation that needs GPU3D-derived state during construction.
void MelonPrimeSetAcceleratedRendererCtorContext(melonDS::GPU3D* gpu3D) noexcept;

// Base for the legacy (no-arg-generation) renderers: SoftRenderer3D, GLRenderer3D,
// ComputeRenderer3D, MetalRenderer3D, and MetalComputeRenderer3D derive from this
// instead of Renderer3D directly.
//
// Why this indirection exists: GPU3D_Vulkan.h (copied verbatim from Sapphire, never
// edited) declares its Renderer3D overrides using unqualified `GPU`/`GPU3D` as *type*
// names (e.g. `void Reset(GPU& gpu) override;`). Per the ordinary class-name-hiding
// rule ([basic.scope.hiding]), a same-named *data member* declared anywhere in
// Renderer3D's own scope would hide those type names for every class derived from
// Renderer3D -- including VulkanRenderer3D -- breaking compilation of the untouched
// copied file (confirmed with a minimal repro against this toolchain). So the legacy
// GPU&/GPU3D& reference members, and the constructor that binds them, live one level
// below Renderer3D, in the ancestry of the legacy renderers only -- never in
// VulkanRenderer3D's ancestry. Runtime semantics are unchanged from the original
// single-class Renderer3D(melonDS::GPU3D&) constructor this replaces.
class Renderer3DLegacyBase : public Renderer3D
{
public:
    explicit Renderer3DLegacyBase(melonDS::GPU3D& gpu3D) noexcept
        : Renderer3D(false), GPU(gpu3D.GPU), GPU3D(gpu3D) {}

protected:
    melonDS::GPU& GPU;
    melonDS::GPU3D& GPU3D;
};
#else
class Renderer3D
{
public:
    explicit Renderer3D(melonDS::GPU3D& gpu3D) : GPU(gpu3D.GPU), GPU3D(gpu3D) {}
    virtual ~Renderer3D() = default;

    Renderer3D(const Renderer3D&) = delete;
    Renderer3D& operator=(const Renderer3D&) = delete;
    virtual bool Init() { return true; }
    virtual void Reset() = 0;

    virtual void RenderFrame() = 0;
    virtual void FinishRendering() {}
    virtual void RestartFrame() {};

    // return one scanline of the framebuffer, with X scroll applied
    // this is used in software renderers
    virtual u32* GetLine(int line) = 0;

    virtual bool NeedsShaderCompile() { return false; }
    virtual void ShaderCompileStep(int& current, int& count) {}

protected:
    melonDS::GPU& GPU;
    melonDS::GPU3D& GPU3D;
};

// Non-Vulkan builds: Renderer3D above is textually identical to upstream, so the
// legacy renderers can keep deriving from it directly under this alias.
using Renderer3DLegacyBase = Renderer3D;
#endif

}

#endif

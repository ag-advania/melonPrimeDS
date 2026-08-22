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

#ifndef GPU_H
#define GPU_H

#include <algorithm>
#include <array>
#include <memory>

#include "GPU2D.h"
#include "GPU3D.h"
#include "NonStupidBitfield.h"

namespace melonDS
{
class GPU3D;
class ARMJIT;

// Non-destructive producer journal for the native GPU2D recorder. The legacy
// VRAMDirty/PaletteDirty/OAMDirty fields remain owned by their existing
// consumers; this journal is an independent observer feed and is never
// consumed or cleared by a renderer.
enum class GPU2DWriteKind : u16
{
    VRAM = 1,
    Palette = 2,
    OAM = 3,
    FIFO = 4,
    Mapping = 5,
    // A native Display Capture readback changed the CPU-visible physical
    // block.  This is deliberately a journal event rather than a destructive
    // dirty-bit operation so the next native FrameRecorder observes the
    // materialized bytes without stealing ownership from the ordinary VRAM
    // consumers.
    CaptureSync = 6,
};

struct GPU2DWriteJournalEntry
{
    u32 Sequence = 0;
    u16 Kind = 0;
    u16 Bank = 0;
    u32 Block = 0;
};

inline constexpr u32 GPU2DWriteJournalCapacity = 16384u;

enum class RendererOutputKind
{
    CpuBgra,
    OpenGLTextureArray,
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    VulkanBuffer,
#endif
#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    DX12Buffer,
#endif
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_METAL)
    MetalTexture,
#endif
    None,
};

struct RendererOutput
{
    RendererOutputKind Kind = RendererOutputKind::None;
    void* Top = nullptr;
    void* Bottom = nullptr;
    u32 Width = 0;
    u32 Height = 0;
#if defined(MELONPRIME_DS) && (defined(MELONPRIME_ENABLE_VULKAN) \
    || defined(MELONPRIME_ENABLE_METAL) \
    || (defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)))
    u64 FrameSerial = 0;
    u64 FrameEpoch = 0;
#endif

    static RendererOutput CpuBgra(void* top, void* bottom, u32 width = 256, u32 height = 192) noexcept
    {
        RendererOutput output;
        output.Kind = RendererOutputKind::CpuBgra;
        output.Top = top;
        output.Bottom = bottom;
        output.Width = width;
        output.Height = height;
        return output;
    }

    static RendererOutput OpenGLTextureArray(void* texture) noexcept
    {
        RendererOutput output;
        output.Kind = RendererOutputKind::OpenGLTextureArray;
        output.Top = texture;
        return output;
    }

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    static RendererOutput VulkanBuffer(
        void* frame, u32 width, u32 height, u64 frameSerial = 0,
        u64 frameEpoch = 0) noexcept
    {
        RendererOutput output;
        output.Kind = RendererOutputKind::VulkanBuffer;
        output.Top = frame;
        output.Width = width;
        output.Height = height;
        output.FrameSerial = frameSerial;
        output.FrameEpoch = frameEpoch;
        return output;
    }
#endif

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    static RendererOutput DX12Buffer(
        void* frame, u32 width, u32 height, u64 frameSerial = 0,
        u64 frameEpoch = 0) noexcept
    {
        RendererOutput output;
        output.Kind = RendererOutputKind::DX12Buffer;
        output.Top = frame;
        output.Width = width;
        output.Height = height;
        output.FrameSerial = frameSerial;
        output.FrameEpoch = frameEpoch;
        return output;
    }
#endif

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_METAL)
    static RendererOutput MetalTexture(void* texture, u64 frameSerial = 0) noexcept
    {
        RendererOutput output;
        output.Kind = RendererOutputKind::MetalTexture;
        output.Top = texture;
        output.FrameSerial = frameSerial;
        return output;
    }
#endif
};

#if defined(MELONPRIME_DS) && (defined(MELONPRIME_ENABLE_VULKAN) \
    || defined(MELONPRIME_ENABLE_METAL) \
    || (defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)))
// GPU-native output is consumed asynchronously by a presenter command. Keep
// its ring slot immutable until that command's completion fence retires.
struct RendererOutputLease
{
    RendererOutput Output;
    void* Context = nullptr;
    void (*ReleaseFn)(void*) = nullptr;
    std::shared_ptr<void> Owner;

    RendererOutputLease() = default;
    RendererOutputLease(
        RendererOutput output,
        void* context,
        void (*releaseFn)(void*),
        std::shared_ptr<void> owner = {}) noexcept
        : Output(output), Context(context), ReleaseFn(releaseFn), Owner(std::move(owner))
    {
    }

    RendererOutputLease(const RendererOutputLease&) = delete;
    RendererOutputLease& operator=(const RendererOutputLease&) = delete;

    RendererOutputLease(RendererOutputLease&& other) noexcept
        : Output(other.Output),
          Context(other.Context),
          ReleaseFn(other.ReleaseFn),
          Owner(std::move(other.Owner))
    {
        other.Context = nullptr;
        other.ReleaseFn = nullptr;
    }

    RendererOutputLease& operator=(RendererOutputLease&& other) noexcept
    {
        if (this != &other)
        {
            ReleaseNow();
            Output = other.Output;
            Context = other.Context;
            ReleaseFn = other.ReleaseFn;
            Owner = std::move(other.Owner);
            other.Context = nullptr;
            other.ReleaseFn = nullptr;
        }
        return *this;
    }

    ~RendererOutputLease()
    {
        ReleaseNow();
    }

    void ReleaseNow() noexcept
    {
        void* context = Context;
        void (*releaseFn)(void*) = ReleaseFn;
        // Keep the backing state alive until after ReleaseFn has retired the
        // slot. Vulkan uses this to avoid a heap allocation per frame lease.
        std::shared_ptr<void> retainedOwner = std::move(Owner);
        (void)retainedOwner;
        Context = nullptr;
        ReleaseFn = nullptr;
        if (context && releaseFn)
            releaseFn(context);
    }
};
#endif

// Display Capture ownership is tracked at the same physical granularity as
// GPU::VRAMCaptureBlockFlags: four 32 KiB blocks in each LCDC bank A..D.
// It intentionally outlives the FrameRecorder that produced a semantic frame.
enum class CaptureOwner : u8
{
    None = 0,
    CpuCoherent,
    NativeVulkan,
    NativeDX12,
};

[[nodiscard]] constexpr bool IsNativeCaptureOwner(CaptureOwner owner) noexcept
{
    return owner == CaptureOwner::NativeVulkan
        || owner == CaptureOwner::NativeDX12;
}

[[nodiscard]] constexpr const char* CaptureOwnerName(CaptureOwner owner) noexcept
{
    switch (owner)
    {
    case CaptureOwner::CpuCoherent: return "CpuCoherent";
    case CaptureOwner::NativeVulkan: return "NativeVulkan";
    case CaptureOwner::NativeDX12: return "NativeDX12";
    default: return "None";
    }
}

struct CaptureBlockProvenance
{
    CaptureOwner Owner = CaptureOwner::None;
    u64 Epoch = 0;
    u64 SemanticFrame = 0;
    u64 CaptureGeneration = 0;
    u64 CompletionValue = 0;
};

struct NativeCaptureStateIdentity
{
    bool Valid = false;
    CaptureOwner Owner = CaptureOwner::None;
    u64 Epoch = 0;
    u64 SemanticFrame = 0;
    u64 CaptureGeneration = 0;
    u64 CompletionValue = 0;
};

enum class CaptureSyncResult : u8
{
    Synchronized = 0,
    AlreadyCoherent,
    Failed,
};

// Capture authority changes are event-driven.  In particular, allocation,
// frame rollover, presentation pressure, and a byte comparison are not
// evidence that CPU VRAM is newer than a native capture mirror.
enum class CaptureAuthorityTransitionReason : u8
{
    NativeSemanticWrite = 0,
    NativeReadbackMaterialized,
    CpuWrite,
    CaptureRetired,
    SavestateLoad,
    SavestateSave,
    RendererReset,
    RendererSwitch,
    SessionReset,
};

[[nodiscard]] constexpr bool IsAllowedNativeToCpuTransition(
    CaptureAuthorityTransitionReason reason) noexcept
{
    switch (reason)
    {
    case CaptureAuthorityTransitionReason::NativeReadbackMaterialized:
    case CaptureAuthorityTransitionReason::CpuWrite:
    case CaptureAuthorityTransitionReason::CaptureRetired:
    case CaptureAuthorityTransitionReason::SavestateLoad:
    case CaptureAuthorityTransitionReason::SavestateSave:
    case CaptureAuthorityTransitionReason::RendererReset:
    case CaptureAuthorityTransitionReason::RendererSwitch:
    case CaptureAuthorityTransitionReason::SessionReset:
        return true;
    case CaptureAuthorityTransitionReason::NativeSemanticWrite:
        return false;
    }
    return false;
}

[[nodiscard]] constexpr const char* CaptureAuthorityTransitionReasonName(
    CaptureAuthorityTransitionReason reason) noexcept
{
    switch (reason)
    {
    case CaptureAuthorityTransitionReason::NativeSemanticWrite:
        return "NativeSemanticWrite";
    case CaptureAuthorityTransitionReason::NativeReadbackMaterialized:
        return "NativeReadbackMaterialized";
    case CaptureAuthorityTransitionReason::CpuWrite:
        return "CpuWrite";
    case CaptureAuthorityTransitionReason::CaptureRetired:
        return "CaptureRetired";
    case CaptureAuthorityTransitionReason::SavestateLoad:
        return "SavestateLoad";
    case CaptureAuthorityTransitionReason::SavestateSave:
        return "SavestateSave";
    case CaptureAuthorityTransitionReason::RendererReset:
        return "RendererReset";
    case CaptureAuthorityTransitionReason::RendererSwitch:
        return "RendererSwitch";
    case CaptureAuthorityTransitionReason::SessionReset:
        return "SessionReset";
    }
    return "Unknown";
}

struct CaptureAuthorityDiagnostics
{
    u64 NativeToCpuReasonCaptureAllocated = 0;
    u64 NativeToCpuReasonFrameBegin = 0;
    u64 NativeToCpuReasonByteDifference = 0;
    u64 NativeOwnedHostReupload = 0;
};

[[nodiscard]] constexpr const char* CaptureSyncResultName(
    CaptureSyncResult result) noexcept
{
    switch (result)
    {
    case CaptureSyncResult::Synchronized: return "Synchronized";
    case CaptureSyncResult::AlreadyCoherent: return "AlreadyCoherent";
    default: return "Failed";
    }
}

inline constexpr u32 CapturePhysicalBanks = 4u;
inline constexpr u32 CapturePhysicalBlocksPerBank = 4u;
inline constexpr u32 CapturePhysicalBlockCount =
    CapturePhysicalBanks * CapturePhysicalBlocksPerBank;

static constexpr u32 VRAMDirtyGranularity = 512;
inline constexpr u32 CapturePhysicalBlockBytes = 32u * 1024u;
inline constexpr u32 CaptureDirtyBlocksPerPhysicalBlock =
    CapturePhysicalBlockBytes / VRAMDirtyGranularity;
class GPU;

template <u32 Size, u32 MappingGranularity>
struct VRAMTrackingSet
{
    u16 Mapping[Size / MappingGranularity];

    const u32 VRAMBitsPerMapping = MappingGranularity / VRAMDirtyGranularity;

    void Reset()
    {
        for (u32 i = 0; i < Size / MappingGranularity; i++)
        {
            // this is not a real VRAM bank
            // so it will always be a mismatch => the bank will be completely invalidated
            Mapping[i] = 0x8000;
        }
    }
    NonStupidBitField<Size/VRAMDirtyGranularity> DeriveState(const u32* currentMappings, GPU& gpu);
};

class Renderer;

class GPU
{
public:
    explicit GPU(melonDS::NDS& nds, std::unique_ptr<Renderer>&& renderer = nullptr) noexcept;
    ~GPU() noexcept;
    void Reset() noexcept;
    void Stop() noexcept;

    void DoSavestate(Savestate* file) noexcept;

    void SetRenderer(std::unique_ptr<Renderer>&& renderer) noexcept;
    const Renderer& GetRenderer() const noexcept { return *Rend; }
    Renderer& GetRenderer() noexcept { return *Rend; }
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    // Developer-only proof hook. It materializes one native-owned capture
    // block through the normal renderer authority path; shipping code has no
    // mapped-capture CPU readback entry point.
    void MaterializeVRAMCaptureBlockForGPU2DProof(u32 block);
#endif

    // return value for GetFramebuffers:
    // true -> pointers to RAM framebuffers are returned via the parameters
    // false -> this renderer doesn't use RAM framebuffers
    //          - values are renderer-specific (ie. OpenGL texture handle)
    bool GetFramebuffers(void** top, void** bottom);
    RendererOutput GetRendererOutput();
#if defined(MELONPRIME_DS) && (defined(MELONPRIME_ENABLE_VULKAN) \
    || defined(MELONPRIME_ENABLE_METAL) \
    || (defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)))
    RendererOutputLease AcquireRendererOutputLease();
#endif

    u8* GetUniqueBankPtr(u32 mask, u32 offset) noexcept;
    const u8* GetUniqueBankPtr(u32 mask, u32 offset) const noexcept;

    u8 Read8(u32 addr);
    u16 Read16(u32 addr);
    u32 Read32(u32 addr);
    void Write8(u32 addr, u8 val);
    void Write16(u32 addr, u16 val);
    void Write32(u32 addr, u32 val);

    void MapVRAM_AB(u32 bank, u8 cnt) noexcept;
    void MapVRAM_CD(u32 bank, u8 cnt) noexcept;
    void MapVRAM_E(u32 bank, u8 cnt) noexcept;
    void MapVRAM_FG(u32 bank, u8 cnt) noexcept;
    void MapVRAM_H(u32 bank, u8 cnt) noexcept;
    void MapVRAM_I(u32 bank, u8 cnt) noexcept;

    /*
        VRAM syncing code for display capture blocks

        The software renderer will write display captures straight to VRAM, making this unnecessary.
        However, hardware-accelerated renderers may want to keep display captures in GPU memory unless
        it is necessary to read them back. This syncing system assists with that.

        Those checks are limited to banks A..D, since those are the only ones that can be used for
        display capture.

        TODO: make checks more efficient
    */

    void SyncVRAM_LCDC(u32 addr, bool write)
    {
        u32 bank = (addr >> 17) & 0x7;
        if (bank >= 4) return;

        if (VRAMMap_LCDC & (1<<bank))
            SyncVRAMCaptureBlock((addr >> 15) & 0xF, write);
    }

    void SyncVRAM_ABG(u32 addr, bool write)
    {
        u32 mask = VRAMMap_ABG[(addr >> 14) & 0x1F];
        addr = (addr >> 15) & 0x3;
        if (mask & (1<<0)) SyncVRAMCaptureBlock((0<<2) | addr, write);
        if (mask & (1<<1)) SyncVRAMCaptureBlock((1<<2) | addr, write);
        if (mask & (1<<2)) SyncVRAMCaptureBlock((2<<2) | addr, write);
        if (mask & (1<<3)) SyncVRAMCaptureBlock((3<<2) | addr, write);
    }

    void SyncVRAM_AOBJ(u32 addr, bool write)
    {
        u32 mask = VRAMMap_AOBJ[(addr >> 14) & 0xF];
        addr = (addr >> 15) & 0x3;
        if (mask & (1<<0)) SyncVRAMCaptureBlock((0<<2) | addr, write);
        if (mask & (1<<1)) SyncVRAMCaptureBlock((1<<2) | addr, write);
    }

    void SyncVRAM_BBG(u32 addr, bool write)
    {
        u32 mask = VRAMMap_BBG[(addr >> 14) & 0x7];
        addr = (addr >> 15) & 0x3;
        if (mask & (1<<2)) SyncVRAMCaptureBlock((2<<2) | addr, write);
    }

    void SyncVRAM_BOBJ(u32 addr, bool write)
    {
        u32 mask = VRAMMap_BOBJ[(addr >> 14) & 0x7];
        addr = (addr >> 15) & 0x3;
        if (mask & (1<<3)) SyncVRAMCaptureBlock((3<<2) | addr, write);
    }

    int GetCaptureBlock_LCDC(u32 offset);

    void GetCaptureInfo_ABG(int* info);
    void GetCaptureInfo_AOBJ(int* info);
    void GetCaptureInfo_BBG(int* info);
    void GetCaptureInfo_BOBJ(int* info);
    void GetCaptureInfo_Texture(int* info);

    template<typename T>
    T ReadVRAM_LCDC(u32 addr) const noexcept
    {
        int bank;

        switch (addr & 0xFF8FC000)
        {
        case 0x06800000: case 0x06804000: case 0x06808000: case 0x0680C000:
        case 0x06810000: case 0x06814000: case 0x06818000: case 0x0681C000:
            bank = 0;
            addr &= 0x1FFFF;
            break;

        case 0x06820000: case 0x06824000: case 0x06828000: case 0x0682C000:
        case 0x06830000: case 0x06834000: case 0x06838000: case 0x0683C000:
            bank = 1;
            addr &= 0x1FFFF;
            break;

        case 0x06840000: case 0x06844000: case 0x06848000: case 0x0684C000:
        case 0x06850000: case 0x06854000: case 0x06858000: case 0x0685C000:
            bank = 2;
            addr &= 0x1FFFF;
            break;

        case 0x06860000: case 0x06864000: case 0x06868000: case 0x0686C000:
        case 0x06870000: case 0x06874000: case 0x06878000: case 0x0687C000:
            bank = 3;
            addr &= 0x1FFFF;
            break;

        case 0x06880000: case 0x06884000: case 0x06888000: case 0x0688C000:
            bank = 4;
            addr &= 0xFFFF;
            break;

        case 0x06890000:
            bank = 5;
            addr &= 0x3FFF;
            break;

        case 0x06894000:
            bank = 6;
            addr &= 0x3FFF;
            break;

        case 0x06898000:
        case 0x0689C000:
            bank = 7;
            addr &= 0x7FFF;
            break;

        case 0x068A0000:
            bank = 8;
            addr &= 0x3FFF;
            break;

        default: return 0;
        }

        if (VRAMMap_LCDC & (1<<bank)) return *(T*)&VRAM[bank][addr];

        return 0;
    }

    template<typename T>
    void WriteVRAM_LCDC(u32 addr, T val)
    {
        int bank;

        switch (addr & 0xFF8FC000)
        {
        case 0x06800000: case 0x06804000: case 0x06808000: case 0x0680C000:
        case 0x06810000: case 0x06814000: case 0x06818000: case 0x0681C000:
            bank = 0;
            addr &= 0x1FFFF;
            break;

        case 0x06820000: case 0x06824000: case 0x06828000: case 0x0682C000:
        case 0x06830000: case 0x06834000: case 0x06838000: case 0x0683C000:
            bank = 1;
            addr &= 0x1FFFF;
            break;

        case 0x06840000: case 0x06844000: case 0x06848000: case 0x0684C000:
        case 0x06850000: case 0x06854000: case 0x06858000: case 0x0685C000:
            bank = 2;
            addr &= 0x1FFFF;
            break;

        case 0x06860000: case 0x06864000: case 0x06868000: case 0x0686C000:
        case 0x06870000: case 0x06874000: case 0x06878000: case 0x0687C000:
            bank = 3;
            addr &= 0x1FFFF;
            break;

        case 0x06880000: case 0x06884000: case 0x06888000: case 0x0688C000:
            bank = 4;
            addr &= 0xFFFF;
            break;

        case 0x06890000:
            bank = 5;
            addr &= 0x3FFF;
            break;

        case 0x06894000:
            bank = 6;
            addr &= 0x3FFF;
            break;

        case 0x06898000:
        case 0x0689C000:
            bank = 7;
            addr &= 0x7FFF;
            break;

        case 0x068A0000:
            bank = 8;
            addr &= 0x3FFF;
            break;

        default: return;
        }

        if (VRAMMap_LCDC & (1<<bank))
        {
            *(T*)&VRAM[bank][addr] = val;
            VRAMDirty[bank][addr / VRAMDirtyGranularity] = true;
            RecordGPU2DWrite(GPU2DWriteKind::VRAM, static_cast<u32>(bank),
                addr / VRAMDirtyGranularity);
        }
    }


    template<typename T>
    T ReadVRAM_ABG(u32 addr) const noexcept
    {
        u8* ptr = VRAMPtr_ABG[(addr >> 14) & 0x1F];
        if (ptr) return *(T*)&ptr[addr & 0x3FFF];

        T ret = 0;
        u32 mask = VRAMMap_ABG[(addr >> 14) & 0x1F];

        if (mask & (1<<0)) ret |= *(T*)&VRAM_A[addr & 0x1FFFF];
        if (mask & (1<<1)) ret |= *(T*)&VRAM_B[addr & 0x1FFFF];
        if (mask & (1<<2)) ret |= *(T*)&VRAM_C[addr & 0x1FFFF];
        if (mask & (1<<3)) ret |= *(T*)&VRAM_D[addr & 0x1FFFF];
        if (mask & (1<<4)) ret |= *(T*)&VRAM_E[addr & 0xFFFF];
        if (mask & (1<<5)) ret |= *(T*)&VRAM_F[addr & 0x3FFF];
        if (mask & (1<<6)) ret |= *(T*)&VRAM_G[addr & 0x3FFF];

        return ret;
    }

    template<typename T>
    void WriteVRAM_ABG(u32 addr, T val)
    {
        u32 mask = VRAMMap_ABG[(addr >> 14) & 0x1F];

        if (mask & (1<<0))
        {
            VRAMDirty[0][(addr & 0x1FFFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_A[addr & 0x1FFFF] = val;
            RecordGPU2DWrite(GPU2DWriteKind::VRAM, 0u,
                (addr & 0x1FFFF) / VRAMDirtyGranularity);
        }
        if (mask & (1<<1))
        {
            VRAMDirty[1][(addr & 0x1FFFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_B[addr & 0x1FFFF] = val;
            RecordGPU2DWrite(GPU2DWriteKind::VRAM, 1u,
                (addr & 0x1FFFF) / VRAMDirtyGranularity);
        }
        if (mask & (1<<2))
        {
            VRAMDirty[2][(addr & 0x1FFFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_C[addr & 0x1FFFF] = val;
            RecordGPU2DWrite(GPU2DWriteKind::VRAM, 2u,
                (addr & 0x1FFFF) / VRAMDirtyGranularity);
        }
        if (mask & (1<<3))
        {
            VRAMDirty[3][(addr & 0x1FFFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_D[addr & 0x1FFFF] = val;
            RecordGPU2DWrite(GPU2DWriteKind::VRAM, 3u,
                (addr & 0x1FFFF) / VRAMDirtyGranularity);
        }
        if (mask & (1<<4))
        {
            VRAMDirty[4][(addr & 0xFFFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_E[addr & 0xFFFF] = val;
            RecordGPU2DWrite(GPU2DWriteKind::VRAM, 4u,
                (addr & 0xFFFF) / VRAMDirtyGranularity);
        }
        if (mask & (1<<5))
        {
            VRAMDirty[5][(addr & 0x3FFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_F[addr & 0x3FFF] = val;
            RecordGPU2DWrite(GPU2DWriteKind::VRAM, 5u,
                (addr & 0x3FFF) / VRAMDirtyGranularity);
        }
        if (mask & (1<<6))
        {
            VRAMDirty[6][(addr & 0x3FFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_G[addr & 0x3FFF] = val;
            RecordGPU2DWrite(GPU2DWriteKind::VRAM, 6u,
                (addr & 0x3FFF) / VRAMDirtyGranularity);
        }
    }


    template<typename T>
    T ReadVRAM_AOBJ(u32 addr) const noexcept
    {
        u8* ptr = VRAMPtr_AOBJ[(addr >> 14) & 0xF];
        if (ptr) return *(T*)&ptr[addr & 0x3FFF];

        T ret = 0;
        u32 mask = VRAMMap_AOBJ[(addr >> 14) & 0xF];

        if (mask & (1<<0)) ret |= *(T*)&VRAM_A[addr & 0x1FFFF];
        if (mask & (1<<1)) ret |= *(T*)&VRAM_B[addr & 0x1FFFF];
        if (mask & (1<<4)) ret |= *(T*)&VRAM_E[addr & 0xFFFF];
        if (mask & (1<<5)) ret |= *(T*)&VRAM_F[addr & 0x3FFF];
        if (mask & (1<<6)) ret |= *(T*)&VRAM_G[addr & 0x3FFF];

        return ret;
    }

    template<typename T>
    void WriteVRAM_AOBJ(u32 addr, T val)
    {
        u32 mask = VRAMMap_AOBJ[(addr >> 14) & 0xF];

        if (mask & (1<<0))
        {
            VRAMDirty[0][(addr & 0x1FFFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_A[addr & 0x1FFFF] = val;
            RecordGPU2DWrite(GPU2DWriteKind::VRAM, 0u,
                (addr & 0x1FFFF) / VRAMDirtyGranularity);
        }
        if (mask & (1<<1))
        {
            VRAMDirty[1][(addr & 0x1FFFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_B[addr & 0x1FFFF] = val;
            RecordGPU2DWrite(GPU2DWriteKind::VRAM, 1u,
                (addr & 0x1FFFF) / VRAMDirtyGranularity);
        }
        if (mask & (1<<4))
        {
            VRAMDirty[4][(addr & 0xFFFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_E[addr & 0xFFFF] = val;
            RecordGPU2DWrite(GPU2DWriteKind::VRAM, 4u,
                (addr & 0xFFFF) / VRAMDirtyGranularity);
        }
        if (mask & (1<<5))
        {
            VRAMDirty[5][(addr & 0x3FFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_F[addr & 0x3FFF] = val;
            RecordGPU2DWrite(GPU2DWriteKind::VRAM, 5u,
                (addr & 0x3FFF) / VRAMDirtyGranularity);
        }
        if (mask & (1<<6))
        {
            VRAMDirty[6][(addr & 0x3FFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_G[addr & 0x3FFF] = val;
            RecordGPU2DWrite(GPU2DWriteKind::VRAM, 6u,
                (addr & 0x3FFF) / VRAMDirtyGranularity);
        }
    }


    template<typename T>
    T ReadVRAM_BBG(u32 addr) const noexcept
    {
        u8* ptr = VRAMPtr_BBG[(addr >> 14) & 0x7];
        if (ptr) return *(T*)&ptr[addr & 0x3FFF];

        T ret = 0;
        u32 mask = VRAMMap_BBG[(addr >> 14) & 0x7];

        if (mask & (1<<2)) ret |= *(T*)&VRAM_C[addr & 0x1FFFF];
        if (mask & (1<<7)) ret |= *(T*)&VRAM_H[addr & 0x7FFF];
        if (mask & (1<<8)) ret |= *(T*)&VRAM_I[addr & 0x3FFF];

        return ret;
    }

    template<typename T>
    void WriteVRAM_BBG(u32 addr, T val)
    {
        u32 mask = VRAMMap_BBG[(addr >> 14) & 0x7];

        if (mask & (1<<2))
        {
            VRAMDirty[2][(addr & 0x1FFFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_C[addr & 0x1FFFF] = val;
            RecordGPU2DWrite(GPU2DWriteKind::VRAM, 2u,
                (addr & 0x1FFFF) / VRAMDirtyGranularity);
        }
        if (mask & (1<<7))
        {
            VRAMDirty[7][(addr & 0x7FFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_H[addr & 0x7FFF] = val;
            RecordGPU2DWrite(GPU2DWriteKind::VRAM, 7u,
                (addr & 0x7FFF) / VRAMDirtyGranularity);
        }
        if (mask & (1<<8))
        {
            VRAMDirty[8][(addr & 0x3FFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_I[addr & 0x3FFF] = val;
            RecordGPU2DWrite(GPU2DWriteKind::VRAM, 8u,
                (addr & 0x3FFF) / VRAMDirtyGranularity);
        }
    }


    template<typename T>
    T ReadVRAM_BOBJ(u32 addr) const noexcept
    {
        u8* ptr = VRAMPtr_BOBJ[(addr >> 14) & 0x7];
        if (ptr) return *(T*)&ptr[addr & 0x3FFF];

        T ret = 0;
        u32 mask = VRAMMap_BOBJ[(addr >> 14) & 0x7];

        if (mask & (1<<3)) ret |= *(T*)&VRAM_D[addr & 0x1FFFF];
        if (mask & (1<<8)) ret |= *(T*)&VRAM_I[addr & 0x3FFF];

        return ret;
    }

    template<typename T>
    void WriteVRAM_BOBJ(u32 addr, T val)
    {
        u32 mask = VRAMMap_BOBJ[(addr >> 14) & 0x7];

        if (mask & (1<<3))
        {
            VRAMDirty[3][(addr & 0x1FFFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_D[addr & 0x1FFFF] = val;
            RecordGPU2DWrite(GPU2DWriteKind::VRAM, 3u,
                (addr & 0x1FFFF) / VRAMDirtyGranularity);
        }
        if (mask & (1<<8))
        {
            VRAMDirty[8][(addr & 0x3FFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_I[addr & 0x3FFF] = val;
            RecordGPU2DWrite(GPU2DWriteKind::VRAM, 8u,
                (addr & 0x3FFF) / VRAMDirtyGranularity);
        }
    }

    template<typename T>
    T ReadVRAM_ARM7(u32 addr) const noexcept
    {
        T ret = 0;
        u32 mask = VRAMMap_ARM7[(addr >> 17) & 0x1];

        if (mask & (1<<2)) ret |= *(T*)&VRAM_C[addr & 0x1FFFF];
        if (mask & (1<<3)) ret |= *(T*)&VRAM_D[addr & 0x1FFFF];

        return ret;
    }

    template<typename T>
    void WriteVRAM_ARM7(u32 addr, T val)
    {
        u32 mask = VRAMMap_ARM7[(addr >> 17) & 0x1];

        if (mask & (1<<2))
        {
            *(T*)&VRAM_C[addr & 0x1FFFF] = val;
            RecordGPU2DWrite(
                GPU2DWriteKind::VRAM, 2u,
                (addr & 0x1FFFF) / VRAMDirtyGranularity);
        }
        if (mask & (1<<3))
        {
            *(T*)&VRAM_D[addr & 0x1FFFF] = val;
            RecordGPU2DWrite(
                GPU2DWriteKind::VRAM, 3u,
                (addr & 0x1FFFF) / VRAMDirtyGranularity);
        }
    }


    template<typename T>
    T ReadVRAM_BG(u32 addr) const noexcept
    {
        if ((addr & 0xFFE00000) == 0x06000000)
            return ReadVRAM_ABG<T>(addr);
        else
            return ReadVRAM_BBG<T>(addr);
    }

    template<typename T>
    T ReadVRAM_OBJ(u32 addr) const noexcept
    {
        if ((addr & 0xFFE00000) == 0x06400000)
            return ReadVRAM_AOBJ<T>(addr);
        else
            return ReadVRAM_BOBJ<T>(addr);
    }


    template<typename T>
    T ReadVRAM_Texture(u32 addr) const noexcept
    {
        T ret = 0;
        u32 mask = VRAMMap_Texture[(addr >> 17) & 0x3];

        if (mask & (1<<0)) ret |= *(T*)&VRAM_A[addr & 0x1FFFF];
        if (mask & (1<<1)) ret |= *(T*)&VRAM_B[addr & 0x1FFFF];
        if (mask & (1<<2)) ret |= *(T*)&VRAM_C[addr & 0x1FFFF];
        if (mask & (1<<3)) ret |= *(T*)&VRAM_D[addr & 0x1FFFF];

        return ret;
    }

    template<typename T>
    T ReadVRAM_TexPal(u32 addr) const noexcept
    {
        T ret = 0;
        u32 mask = VRAMMap_TexPal[(addr >> 14) & 0x7];

        if (mask & (1<<4)) ret |= *(T*)&VRAM_E[addr & 0xFFFF];
        if (mask & (1<<5)) ret |= *(T*)&VRAM_F[addr & 0x3FFF];
        if (mask & (1<<6)) ret |= *(T*)&VRAM_G[addr & 0x3FFF];

        return ret;
    }

    template<typename T>
    T ReadPalette(u32 addr) const noexcept
    {
        return *(T*)&Palette[addr & 0x7FF];
    }

    template<typename T>
    void WritePalette(u32 addr, T val)
    {
        addr &= 0x7FF;

        *(T*)&Palette[addr] = val;
        if (addr & 0x3FE)
            PaletteDirty |= 1 << (addr / VRAMDirtyGranularity);
        else
            PaletteDirty |= 0x10 << (addr / VRAMDirtyGranularity);
        RecordGPU2DWrite(
            GPU2DWriteKind::Palette, 0u, addr / VRAMDirtyGranularity);
    }

    template<typename T>
    T ReadOAM(u32 addr) const noexcept
    {
        return *(T*)&OAM[addr & 0x7FF];
    }

    template<typename T>
    void WriteOAM(u32 addr, T val)
    {
        addr &= 0x7FF;

        *(T*)&OAM[addr] = val;
        OAMDirty |= 1 << (addr / 1024);
        RecordGPU2DWrite(
            GPU2DWriteKind::OAM, 0u, addr / VRAMDirtyGranularity);
    }

    void RecordGPU2DWrite(GPU2DWriteKind kind, u32 bank, u32 block) noexcept
    {
        const u32 sequence = ++GPU2DWriteJournalSequence;
        GPU2DWriteJournal[sequence % GPU2DWriteJournalCapacity] = {
            sequence,
            static_cast<u16>(kind),
            static_cast<u16>(bank),
            block,
        };
    }

    // Publish a native readback as an ordinary non-destructive journal event.
    // FrameRecorder consumes this event just like a CPU/DMA VRAM write, so a
    // readback that lands after frame N+1's baseline cannot be silently
    // omitted from the next native input snapshot.
    void RecordGPU2DCaptureSync(u32 bank, u32 start, u32 len) noexcept
    {
        if (bank >= CapturePhysicalBanks || start >= CapturePhysicalBlocksPerBank)
            return;
        const u32 blockCount = len == 0u ? 1u : std::min<u32>(len, 3u);
        for (u32 i = 0; i < blockCount; ++i)
        {
            const u32 physicalBlock =
                (start + i) & (CapturePhysicalBlocksPerBank - 1u);
            // Journal blocks are 512-byte VRAM dirty blocks, while capture
            // ownership is tracked at 32 KiB. Publish every dirty sub-block
            // so FrameRecorder can repair all mapped views after native
            // readback, not just the first 2 KiB of the capture block.
            for (u32 subblock = 0;
                subblock < CaptureDirtyBlocksPerPhysicalBlock;
                ++subblock)
            {
                RecordGPU2DWrite(
                    GPU2DWriteKind::CaptureSync,
                    bank,
                    physicalBlock * CaptureDirtyBlocksPerPhysicalBlock
                        + subblock);
            }
        }
    }

    [[nodiscard]] u32 GetGPU2DWriteJournalSequence() const noexcept
    {
        return GPU2DWriteJournalSequence;
    }

    // Returns entries strictly after `afterSequence`. If the fixed ring
    // wrapped before the caller consumed it, `overflow` is set and the caller
    // must take a one-time baseline snapshot. No dirty bit or journal entry is
    // cleared here.
    u32 ReadGPU2DWriteJournal(
        u32 afterSequence,
        GPU2DWriteJournalEntry* destination,
        u32 capacity,
        bool& overflow) const noexcept
    {
        overflow = false;
        if (!destination || capacity == 0u
            || afterSequence >= GPU2DWriteJournalSequence)
            return 0u;

        const u32 first = afterSequence + 1u;
        const u32 oldest = GPU2DWriteJournalSequence >= GPU2DWriteJournalCapacity
            ? GPU2DWriteJournalSequence - GPU2DWriteJournalCapacity + 1u
            : 1u;
        u32 begin = first;
        if (begin < oldest)
        {
            begin = oldest;
            overflow = true;
        }
        const u32 count = std::min(
            GPU2DWriteJournalSequence - begin + 1u, capacity);
        for (u32 i = 0; i < count; ++i)
            destination[i] = GPU2DWriteJournal[(begin + i) % GPU2DWriteJournalCapacity];
        if (count < GPU2DWriteJournalSequence - begin + 1u)
            overflow = true;
        return count;
    }

    template <typename T>
    inline T ReadVRAMFlat_Texture(u32 addr) const
    {
        return *(T*)&VRAMFlat_Texture[addr & 0x7FFFF];
    }
    template <typename T>
    inline T ReadVRAMFlat_TexPal(u32 addr) const
    {
        return *(T*)&VRAMFlat_TexPal[addr & 0x1FFFF];
    }

    void SetPowerCnt(u32 val) noexcept;

    void StartFrame() noexcept;
    void FinishFrame(u32 lines) noexcept;
    void BlankFrame() noexcept;
    void StartScanline(u32 line) noexcept;
    void StartHBlank(u32 line) noexcept;

    void Restart3DFrame() noexcept;

    void DisplayFIFO(u32 x) noexcept;

    void SetDispStat(u32 cpu, u16 val, u16 mask) noexcept;
    void SetVCount(u16 val, u16 mask) noexcept;

    bool MakeVRAMFlat_ABGCoherent(NonStupidBitField<512*1024/VRAMDirtyGranularity>& dirty) noexcept;
    bool MakeVRAMFlat_BBGCoherent(NonStupidBitField<128*1024/VRAMDirtyGranularity>& dirty) noexcept;

    bool MakeVRAMFlat_AOBJCoherent(NonStupidBitField<256*1024/VRAMDirtyGranularity>& dirty) noexcept;
    bool MakeVRAMFlat_BOBJCoherent(NonStupidBitField<128*1024/VRAMDirtyGranularity>& dirty) noexcept;

    bool MakeVRAMFlat_ABGExtPalCoherent(NonStupidBitField<32*1024/VRAMDirtyGranularity>& dirty) noexcept;
    bool MakeVRAMFlat_BBGExtPalCoherent(NonStupidBitField<32*1024/VRAMDirtyGranularity>& dirty) noexcept;

    bool MakeVRAMFlat_AOBJExtPalCoherent(NonStupidBitField<8*1024/VRAMDirtyGranularity>& dirty) noexcept;
    bool MakeVRAMFlat_BOBJExtPalCoherent(NonStupidBitField<8*1024/VRAMDirtyGranularity>& dirty) noexcept;

    bool MakeVRAMFlat_TextureCoherent(NonStupidBitField<512*1024/VRAMDirtyGranularity>& dirty) noexcept;
    bool MakeVRAMFlat_TexPalCoherent(NonStupidBitField<128*1024/VRAMDirtyGranularity>& dirty) noexcept;

    melonDS::NDS& NDS;

    bool ScreensEnabled = false;
    bool ScreenSwap = false;

    u16 VCount = 0;
    u16 TotalScanlines = 0;
    u16 DispStat[2] {};
    u8 VRAMCNT[9] {};
    u8 VRAMSTAT = 0;

    u16 MasterBrightnessA;
    u16 MasterBrightnessB;

    u16 DispFIFO[16];
    u8 DispFIFOReadPtr;
    u8 DispFIFOWritePtr;
    alignas(8) u16 DispFIFOBuffer[256];

    u32 CaptureCnt;
    bool CaptureEnable;

    alignas(u64) u8 Palette[2*1024] {};
    alignas(u64) u8 OAM[2*1024] {};

    alignas(u64) u8 VRAM_A[128*1024] {};
    alignas(u64) u8 VRAM_B[128*1024] {};
    alignas(u64) u8 VRAM_C[128*1024] {};
    alignas(u64) u8 VRAM_D[128*1024] {};
    alignas(u64) u8 VRAM_E[ 64*1024] {};
    alignas(u64) u8 VRAM_F[ 16*1024] {};
    alignas(u64) u8 VRAM_G[ 16*1024] {};
    alignas(u64) u8 VRAM_H[ 32*1024] {};
    alignas(u64) u8 VRAM_I[ 16*1024] {};

    u8* const VRAM[9]     = {VRAM_A,  VRAM_B,  VRAM_C,  VRAM_D,  VRAM_E, VRAM_F, VRAM_G, VRAM_H, VRAM_I};
    u32 const VRAMMask[9] = {0x1FFFF, 0x1FFFF, 0x1FFFF, 0x1FFFF, 0xFFFF, 0x3FFF, 0x3FFF, 0x7FFF, 0x3FFF};

    u32 VRAMMap_LCDC = 0;
    u32 VRAMMap_ABG[0x20] {};
    u32 VRAMMap_AOBJ[0x10] {};
    u32 VRAMMap_BBG[0x8] {};
    u32 VRAMMap_BOBJ[0x8] {};
    u32 VRAMMap_ABGExtPal[4] {};
    u32 VRAMMap_AOBJExtPal {};
    u32 VRAMMap_BBGExtPal[4] {};
    u32 VRAMMap_BOBJExtPal {};
    u32 VRAMMap_Texture[4] {};
    u32 VRAMMap_TexPal[8] {};
    u32 VRAMMap_ARM7[2] {};

    u8* VRAMPtr_ABG[0x20] {};
    u8* VRAMPtr_AOBJ[0x10] {};
    u8* VRAMPtr_BBG[0x8] {};
    u8* VRAMPtr_BOBJ[0x8] {};

    melonDS::GPU2D GPU2D_A;
    melonDS::GPU2D GPU2D_B;
    melonDS::GPU3D GPU3D;

    NonStupidBitField<128*1024/VRAMDirtyGranularity> VRAMDirty[9] {};
    VRAMTrackingSet<512*1024, 16*1024> VRAMDirty_ABG {};
    VRAMTrackingSet<256*1024, 16*1024> VRAMDirty_AOBJ {};
    VRAMTrackingSet<128*1024, 16*1024> VRAMDirty_BBG {};
    VRAMTrackingSet<128*1024, 16*1024> VRAMDirty_BOBJ {};

    VRAMTrackingSet<32*1024, 8*1024> VRAMDirty_ABGExtPal {};
    VRAMTrackingSet<32*1024, 8*1024> VRAMDirty_BBGExtPal {};
    VRAMTrackingSet<8*1024, 8*1024> VRAMDirty_AOBJExtPal {};
    VRAMTrackingSet<8*1024, 8*1024> VRAMDirty_BOBJExtPal {};

    VRAMTrackingSet<512*1024, 128*1024> VRAMDirty_Texture {};
    VRAMTrackingSet<128*1024, 16*1024> VRAMDirty_TexPal {};

    u8 VRAMFlat_ABG[512*1024] {};
    u8 VRAMFlat_BBG[128*1024] {};
    u8 VRAMFlat_AOBJ[256*1024] {};
    u8 VRAMFlat_BOBJ[128*1024] {};

    alignas(u16) u8 VRAMFlat_ABGExtPal[32*1024] {};
    alignas(u16) u8 VRAMFlat_BBGExtPal[32*1024] {};

    alignas(u16) u8 VRAMFlat_AOBJExtPal[8*1024] {};
    alignas(u16) u8 VRAMFlat_BOBJExtPal[8*1024] {};

    alignas(u64) u8 VRAMFlat_Texture[512*1024] {};
    alignas(u64) u8 VRAMFlat_TexPal[128*1024] {};

    u32 OAMDirty = 0;
    u32 PaletteDirty = 0;

    std::array<GPU2DWriteJournalEntry, GPU2DWriteJournalCapacity>
        GPU2DWriteJournal{};
    u32 GPU2DWriteJournalSequence = 0;

private:
    void ResetVRAMCache() noexcept;

    template<typename T>
    T ReadVRAM_ABGExtPal(u32 addr) const noexcept
    {
        u32 mask = VRAMMap_ABGExtPal[(addr >> 13) & 0x3];

        T ret = 0;
        if (mask & (1<<4)) ret |= *(T*)&VRAM_E[addr & 0x7FFF];
        if (mask & (1<<5)) ret |= *(T*)&VRAM_F[addr & 0x3FFF];
        if (mask & (1<<6)) ret |= *(T*)&VRAM_G[addr & 0x3FFF];

        return ret;
    }

    template<typename T>
    T ReadVRAM_BBGExtPal(u32 addr) const noexcept
    {
        u32 mask = VRAMMap_BBGExtPal[(addr >> 13) & 0x3];

        T ret = 0;
        if (mask & (1<<7)) ret |= *(T*)&VRAM_H[addr & 0x7FFF];

        return ret;
    }

    template<typename T>
    T ReadVRAM_AOBJExtPal(u32 addr) const noexcept
    {
        u32 mask = VRAMMap_AOBJExtPal;

        T ret = 0;
        if (mask & (1<<4)) ret |= *(T*)&VRAM_F[addr & 0x1FFF];
        if (mask & (1<<5)) ret |= *(T*)&VRAM_G[addr & 0x1FFF];

        return ret;
    }

    template<typename T>
    T ReadVRAM_BOBJExtPal(u32 addr) const noexcept
    {
        u32 mask = VRAMMap_BOBJExtPal;

        T ret = 0;
        if (mask & (1<<8)) ret |= *(T*)&VRAM_I[addr & 0x1FFF];

        return ret;
    }

    template <u32 MappingGranularity, u32 Size>
    constexpr bool CopyLinearVRAM(u8* flat, const u32* mappings, NonStupidBitField<Size>& dirty, u64 (GPU::* const slowAccess)(u32) const noexcept) noexcept
    {
        const u32 VRAMBitsPerMapping = MappingGranularity / VRAMDirtyGranularity;

        bool change = false;

        typename NonStupidBitField<Size>::Iterator it = dirty.Begin();
        while (it != dirty.End())
        {
            u32 offset = *it * VRAMDirtyGranularity;
            u8* dst = flat + offset;
            u8* fastAccess = GetUniqueBankPtr(mappings[*it / VRAMBitsPerMapping], offset);
            if (fastAccess)
            {
                memcpy(dst, fastAccess, VRAMDirtyGranularity);
            }
            else
            {
                for (u32 i = 0; i < VRAMDirtyGranularity; i += 8)
                    *(u64*)&dst[i] = (this->*slowAccess)(offset + i);
            }
            change = true;
            it++;
        }
        return change;
    }

    u16* GetUniqueBankCBF(u32 mask, u32 offset);
    void VRAMCBFlagsSet(u32 bank, u32 block, u16 val);
    void VRAMCBFlagsClear(u32 bank, u32 block);
    void VRAMCBFlagsOr(u32 bank, u32 block, u16 val);
    void CheckCaptureStart();
    void CheckCaptureEnd();
    void SyncVRAMCaptureBlock(u32 block, bool write);
    bool SyncAllVRAMCaptures(
        CaptureAuthorityTransitionReason reason);
    void LogCaptureSync(
        u32 bank,
        u32 start,
        u32 len,
        u16 flags,
        const CaptureBlockProvenance& owner,
        u64 cpuVRAMHashBefore,
        u64 nativeCaptureHash,
        u64 cpuVRAMHashAfter,
        CaptureSyncResult result,
        bool flagsMarkedSynced,
        bool flagsCleared) const noexcept;
    void GetCaptureInfo(int* info, u16** cbf, int len);

    void SetDispStatIRQ(int cpu, int num);

    bool UsesDisplayFIFO();
    void SampleDisplayFIFO(u32 offset, u32 num);

    bool VCountOverride = false;
    u16 NextVCount = 0;

    bool RunFIFO = false;

    u16 VMatch[2] {};

    std::unique_ptr<Renderer> Rend = nullptr;

    u16 VRAMCaptureBlockFlags[16];

    u16* VRAMCBF_ABG[0x20] {};
    u16* VRAMCBF_AOBJ[0x10] {};
    u16* VRAMCBF_BBG[0x8] {};
    u16* VRAMCBF_BOBJ[0x8] {};
};


struct RendererSettings
{
    // scale factor, for renderers that support upscaling
    int ScaleFactor;

    // whether to use separate threads for rendering
    bool Threaded;

    // whether to use hi-res vertex coordinates when applying upscaling
    bool HiresCoordinates;

    // "improved polygon splitting" (regular OpenGL renderer)
    bool BetterPolygons;

#if defined(MELONPRIME_DS) && (defined(MELONPRIME_ENABLE_VULKAN) \
    || (defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)))
    // 0=Off, 1=Reflex low latency, 2=Reflex low latency + GPU clock boost.
    int NvidiaReflexMode;
    // AMD Radeon Anti-Lag 2. The backend always passes maxFPS=0 so this does
    // not add a second frame-rate limiter.
    bool AmdAntiLag2Enabled;
#if defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    // Intel Xe Low Latency. The DX12 backend uses minimumIntervalUs=0 because
    // MelonPrime's existing limiter runs before xellSleep.
    bool IntelXeLLEnabled;
    // Developer builds may select an unvalidated XeLL pacing experiment.
    // Release builds always pass Compatibility regardless of stored config.
    int IntelXeLLPacingPolicy;
#endif
#endif
};

class Renderer
{
public:
    explicit Renderer(melonDS::GPU& gpu) : GPU(gpu), BackBuffer(0)
    {
        ResetCaptureProvenance(CaptureAuthorityTransitionReason::SessionReset);
    }
    virtual ~Renderer() {}
    virtual bool Init() = 0;
    virtual void Reset() = 0;
    virtual void Stop() = 0;

    virtual void PreSavestate() {}
    virtual void PostSavestate() {}
    virtual void InvalidateHighResCaptureState(
        HighResCaptureInvalidationReason reason) noexcept
    {
        if (Rend3D)
            Rend3D->InvalidateHighResCaptureState(reason);
    }

    virtual void SetRenderSettings(RendererSettings& settings) = 0;

    virtual void DrawScanline(u32 line) = 0;
    virtual void DrawSprites(u32 line) = 0;

    virtual void Start3DRendering() { Rend3D->RenderFrame(); }
    virtual void Finish3DRendering() { Rend3D->FinishRendering(); }
    virtual void Restart3DRendering() { Rend3D->RestartFrame(); }

    virtual void VBlank() = 0;
    virtual void VBlankEnd() = 0;

    virtual void AllocCapture(u32 bank, u32 start, u32 len) = 0;
    // Display Capture ownership outlives the FrameRecorder that produced it.
    // SyncVRAMCapture must select the authoritative source from capture
    // provenance, not from whether the current emulated frame has already
    // finalized a native GPU2D recorder. A native-owned capture must never
    // fall through to the SoftRenderer no-op sync path.
    virtual CaptureSyncResult SyncVRAMCapture(
        u32 bank, u32 start, u32 len, bool complete) = 0;
    virtual void InvalidateVRAMCapture(
        u32 bank,
        u32 start,
        u32 len,
        CaptureAuthorityTransitionReason reason)
    {
        MarkCaptureCpuCoherent(bank, start, len, reason);
    }

    [[nodiscard]] const CaptureBlockProvenance& GetCaptureBlockProvenance(
        u32 bank, u32 block) const noexcept;
    // Monotonic event serial for the retained native/CPU ownership map. GPU2D
    // frame recording uses it to invalidate its per-line mapping summary only
    // when authority actually changes, without rescanning all provenance in
    // the mapped-memory hot loop.
    [[nodiscard]] u64 GetCaptureProvenanceSerial() const noexcept
    {
        return CaptureProvenanceSerial;
    }
    // Returns false when a requested capture range mixes native/CPU owners or
    // contains native blocks from different identities. A range with no
    // native owner is considered CPU-coherent even if its non-native metadata
    // differs, because no GPU mirror is authoritative for that request.
    [[nodiscard]] bool GetCaptureProvenanceForRange(
        u32 bank,
        u32 start,
        u32 len,
        CaptureBlockProvenance& representative) const noexcept;
    void MarkCaptureCpuCoherent(
        u32 bank,
        u32 start,
        u32 len,
        CaptureAuthorityTransitionReason reason) noexcept;
    void PublishNativeCaptureBlock(
        CaptureOwner owner,
        const NativeCaptureStateIdentity& identity,
        u32 bank,
        u32 start,
        u32 len,
        CaptureAuthorityTransitionReason reason =
            CaptureAuthorityTransitionReason::NativeSemanticWrite) noexcept;
    void ResetCaptureProvenance(
        CaptureAuthorityTransitionReason reason) noexcept;

    [[nodiscard]] const CaptureAuthorityDiagnostics&
    GetCaptureAuthorityDiagnostics() const noexcept
    {
        return CaptureAuthorityStats;
    }

    [[nodiscard]] virtual NativeCaptureStateIdentity
    GetNativeCaptureStateIdentity() const noexcept
    {
        return {};
    }
    [[nodiscard]] virtual const char* GetCaptureBackendName() const noexcept
    {
        return "Unknown";
    }
    // FrameRecorder needs to distinguish a native GPU2D producer from the
    // exact-validation oracle.  The latter still composes a native frame but
    // deliberately keeps Software as the producer, so a current-frame capture
    // write must not be advertised as a native owner in that mode.
    [[nodiscard]] virtual bool UsesNativeGPU2DProducerForFrame() const noexcept
    {
        return false;
    }
#ifdef MELONPRIME_DS
    // Backend-neutral lookup for a retained high-resolution display-capture
    // pixel. The emulated VRAM remains authoritative; a zero result means the
    // caller must use the ordinary native texture cache.
    [[nodiscard]] virtual u32 GetCaptureTextureReference(u32 bank, u32 address) const noexcept
    {
        return 0;
    }
#endif

    // a renderer may render to RAM buffers, or to something else (ie. OpenGL)
    // if the renderer uses RAM buffers, they should be 32-bit BGRA, 256x192 for each screen
    virtual bool GetFramebuffers(void** top, void** bottom) = 0;
    virtual RendererOutput GetOutput()
    {
        void* top = nullptr;
        void* bottom = nullptr;
        if (GetFramebuffers(&top, &bottom))
            return RendererOutput::CpuBgra(top, bottom);
        if (top)
            return RendererOutput::OpenGLTextureArray(top);
        return {};
    }
#if defined(MELONPRIME_DS) && (defined(MELONPRIME_ENABLE_VULKAN) \
    || defined(MELONPRIME_ENABLE_METAL) \
    || (defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)))
    virtual RendererOutputLease AcquireOutputLease()
    {
        return RendererOutputLease(GetOutput(), nullptr, nullptr);
    }
#endif
    virtual void SwapBuffers() { BackBuffer ^= 1; }

    virtual bool NeedsShaderCompile() { return false; }
    virtual void ShaderCompileStep(int& current, int& count) {}

protected:
    melonDS::GPU& GPU;

    int BackBuffer;

    std::array<CaptureBlockProvenance, CapturePhysicalBlockCount>
        CaptureProvenance{};
    u64 CaptureProvenanceSerial = 1u;
    CaptureAuthorityDiagnostics CaptureAuthorityStats{};

    std::unique_ptr<Renderer2D> Rend2D_A;
    std::unique_ptr<Renderer2D> Rend2D_B;
    std::unique_ptr<Renderer3D> Rend3D;
};

}

#endif

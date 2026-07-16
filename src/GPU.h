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

#include <memory>
#include <type_traits>

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
#include <vulkan/vulkan.h>
#endif

#include "GPU2D.h"
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
#include "GPU2D_Structured.h"
#include "SapphireGPU2DCore/SapphireGPU2DRenderer2D.h"
#include "SapphireGPU2DCore/GPU2D_Soft.h"
#include "SapphireGPU2DSoftAccess.h"
#include "SapphirePublished2DFrame.h"
#endif
#include "GPU3D.h"
#include "NonStupidBitfield.h"

namespace melonDS
{
class GPU3D;
class ARMJIT;

enum class RendererOutputKind
{
    CpuBgra,
    OpenGLTextureArray,
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_METAL)
    MetalTexture,
#endif
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    VulkanImage,
#endif
    None,
};

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
struct VulkanRendererOutput
{
    VkImage Image = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    VkExtent2D Extent{};
    VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    u32 LayerCount = 0;
    u32 EngineALayer = 0;
    u64 FrameSerial = 0;
    u64 Generation = 0;
    VkSemaphore ProducerTimeline = VK_NULL_HANDLE;
    u64 ProducerValue = 0;
    void* LeaseContext = nullptr;
};
#endif

struct RendererOutput
{
    RendererOutputKind Kind = RendererOutputKind::None;
    void* Top = nullptr;
    void* Bottom = nullptr;
#if defined(MELONPRIME_DS)
    u64 FrameSerial = 0;
    u64 Generation = 0;

    bool MatchesProducerFrame(u64 frameSerial, u64 generation) const noexcept
    {
        return FrameSerial == frameSerial && Generation == generation;
    }
#endif

    static RendererOutput CpuBgra(void* top, void* bottom) noexcept
    {
#if defined(MELONPRIME_DS)
        return { RendererOutputKind::CpuBgra, top, bottom, 0, 0 };
#else
        return { RendererOutputKind::CpuBgra, top, bottom };
#endif
    }

    static RendererOutput OpenGLTextureArray(void* texture) noexcept
    {
#if defined(MELONPRIME_DS)
        return { RendererOutputKind::OpenGLTextureArray, texture, nullptr, 0, 0 };
#else
        return { RendererOutputKind::OpenGLTextureArray, texture, nullptr };
#endif
    }

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_METAL)
    static RendererOutput MetalTexture(
        void* texture, u64 frameSerial = 0, u64 generation = 0) noexcept
    {
        return { RendererOutputKind::MetalTexture, texture, nullptr, frameSerial, generation };
    }
#endif

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    static RendererOutput VulkanImage(VulkanRendererOutput* descriptor) noexcept
    {
        return {
            RendererOutputKind::VulkanImage,
            descriptor,
            nullptr,
            descriptor ? descriptor->FrameSerial : 0,
            descriptor ? descriptor->Generation : 0};
    }
#endif
};

#if defined(MELONPRIME_DS)
// Native GPU output may be consumed by a command queue separate from the
// renderer queue. Keep its ring slot immutable until the presenter command
// completes. CPU and OpenGL outputs use the same zero-cost type with no
// release callback.
struct RendererOutputLease
{
    RendererOutput Output;
    void* Context = nullptr;
    void (*ReleaseFn)(void*) = nullptr;

    RendererOutputLease() = default;
    RendererOutputLease(
        RendererOutput output,
        void* context,
        void (*releaseFn)(void*)) noexcept
        : Output(output), Context(context), ReleaseFn(releaseFn)
    {
    }

    RendererOutputLease(const RendererOutputLease&) = delete;
    RendererOutputLease& operator=(const RendererOutputLease&) = delete;

    RendererOutputLease(RendererOutputLease&& other) noexcept
        : Output(other.Output),
          Context(other.Context),
          ReleaseFn(other.ReleaseFn)
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
        Context = nullptr;
        ReleaseFn = nullptr;
        if (context && releaseFn)
            releaseFn(context);
    }
};

static_assert(!std::is_copy_constructible_v<RendererOutputLease>);
static_assert(std::is_nothrow_move_constructible_v<RendererOutputLease>);
#endif

static constexpr u32 VRAMDirtyGranularity = 512;
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

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
class SapphireGpu2DState;
#endif

class GPU
{
public:
    explicit GPU(melonDS::NDS& nds, std::unique_ptr<Renderer>&& renderer = nullptr) noexcept;
    ~GPU() noexcept;
    void Reset() noexcept;
    void Stop() noexcept;

    void DoSavestate(Savestate* file) noexcept;

    void SetRenderer(std::unique_ptr<Renderer>&& renderer) noexcept;
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    [[nodiscard]] bool LastRendererInitializationSucceeded() const noexcept
    {
        return LastRendererInitSucceeded;
    }
#endif
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    void SetRenderer3D(std::unique_ptr<Renderer3D>&& renderer) noexcept
    {
        GPU3D.SetCurrentRenderer(std::move(renderer));
    }
#endif
    const Renderer& GetRenderer() const noexcept { return *Rend; }
    Renderer& GetRenderer() noexcept { return *Rend; }
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    [[nodiscard]] SapphireGPU2D::SoftRenderer& GetSapphireRenderer2D() noexcept;
    [[nodiscard]] const SapphireGPU2D::SoftRenderer& GetSapphireRenderer2D() const noexcept;
    [[nodiscard]] SapphireGPU2D::SoftRenderer* TryGetSapphireRenderer2D() noexcept;
    [[nodiscard]] const SapphireGPU2D::SoftRenderer* TryGetSapphireRenderer2D() const noexcept;
    [[nodiscard]] SapphireGpu2DState* TryGetSapphireGpu2DState() noexcept;
    [[nodiscard]] const SapphireGpu2DState* TryGetSapphireGpu2DState() const noexcept;
    void SetRenderer2D(
        std::unique_ptr<SapphireGPU2DCore::GPU2D::Renderer2D>&& renderer) noexcept;
    [[nodiscard]] SapphireGPU2DCore::GPU2D::Renderer2D& GetRenderer2D() noexcept;
    [[nodiscard]] const SapphireGPU2DCore::GPU2D::Renderer2D& GetRenderer2D() const noexcept;
    [[nodiscard]] SapphireGPU2DCore::GPU2D::SoftRenderer* TryGetGpu2DSoftRenderer() noexcept;
    [[nodiscard]] const SapphireGPU2DCore::GPU2D::SoftRenderer* TryGetGpu2DSoftRenderer() const noexcept;
    [[nodiscard]] bool ActivateSapphireVulkan2D(u64 rendererGeneration) noexcept;
    void DeactivateSapphireVulkan2D() noexcept;
    void RefreshSapphireVulkanBindings() noexcept;
    void InvalidateSapphirePublication() noexcept;
    void InvalidateSapphireFramebufferBindings() noexcept;
    [[nodiscard]] const SapphirePublished2DFrame& GetPublished2DFrame() const noexcept
    {
        return Published2DFrame;
    }

    int FrontBuffer = 0;
    u32* Framebuffer[2][2]{};
    SapphirePublished2DFrame Published2DFrame{};
#endif

    // return value for GetFramebuffers:
    // true -> pointers to RAM framebuffers are returned via the parameters
    // false -> this renderer doesn't use RAM framebuffers
    //          - values are renderer-specific (ie. OpenGL texture handle)
    bool GetFramebuffers(void** top, void** bottom);
    RendererOutput GetRendererOutput();
#if defined(MELONPRIME_DS)
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
        }
        if (mask & (1<<1))
        {
            VRAMDirty[1][(addr & 0x1FFFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_B[addr & 0x1FFFF] = val;
        }
        if (mask & (1<<2))
        {
            VRAMDirty[2][(addr & 0x1FFFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_C[addr & 0x1FFFF] = val;
        }
        if (mask & (1<<3))
        {
            VRAMDirty[3][(addr & 0x1FFFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_D[addr & 0x1FFFF] = val;
        }
        if (mask & (1<<4))
        {
            VRAMDirty[4][(addr & 0xFFFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_E[addr & 0xFFFF] = val;
        }
        if (mask & (1<<5))
        {
            VRAMDirty[5][(addr & 0x3FFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_F[addr & 0x3FFF] = val;
        }
        if (mask & (1<<6))
        {
            VRAMDirty[6][(addr & 0x3FFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_G[addr & 0x3FFF] = val;
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
        }
        if (mask & (1<<1))
        {
            VRAMDirty[1][(addr & 0x1FFFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_B[addr & 0x1FFFF] = val;
        }
        if (mask & (1<<4))
        {
            VRAMDirty[4][(addr & 0xFFFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_E[addr & 0xFFFF] = val;
        }
        if (mask & (1<<5))
        {
            VRAMDirty[5][(addr & 0x3FFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_F[addr & 0x3FFF] = val;
        }
        if (mask & (1<<6))
        {
            VRAMDirty[6][(addr & 0x3FFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_G[addr & 0x3FFF] = val;
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
        }
        if (mask & (1<<7))
        {
            VRAMDirty[7][(addr & 0x7FFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_H[addr & 0x7FFF] = val;
        }
        if (mask & (1<<8))
        {
            VRAMDirty[8][(addr & 0x3FFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_I[addr & 0x3FFF] = val;
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
        }
        if (mask & (1<<8))
        {
            VRAMDirty[8][(addr & 0x3FFF) / VRAMDirtyGranularity] = true;
            *(T*)&VRAM_I[addr & 0x3FFF] = val;
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

        if (mask & (1<<2)) *(T*)&VRAM_C[addr & 0x1FFFF] = val;
        if (mask & (1<<3)) *(T*)&VRAM_D[addr & 0x1FFFF] = val;
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
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // Shared transient identity for one complete software-2D/Vulkan-3D frame.
    u64 VulkanFrameSerial = 0;
#endif
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
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // Display capture control is latched at line 0 for the full capture frame.
    // Register writes during the frame affect the next capture, not the active one.
    u32 CaptureFrameCnt = 0;
    u32 CaptureFrameDispCntA = 0;
    bool CaptureFrameScreenSwap = false;
#endif
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

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    SapphireGPU2DCore::GPU2D::Unit GPU2D_A;
    SapphireGPU2DCore::GPU2D::Unit GPU2D_B;
#else
    melonDS::GPU2D GPU2D_A;
    melonDS::GPU2D GPU2D_B;
#endif
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
    void SyncAllVRAMCaptures();
    void GetCaptureInfo(int* info, u16** cbf, int len);

    void SetDispStatIRQ(int cpu, int num);

    bool UsesDisplayFIFO();
    void SampleDisplayFIFO(u32 offset, u32 num);

    bool VCountOverride = false;
    u16 NextVCount = 0;

    bool RunFIFO = false;

    u16 VMatch[2] {};

    std::unique_ptr<Renderer> Rend = nullptr;
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    bool LastRendererInitSucceeded = true;
    std::unique_ptr<SapphireGpu2DState> Sapphire2D;
    std::unique_ptr<SapphireGPU2DCore::GPU2D::Renderer2D> GPU2D_Renderer;
    std::unique_ptr<SapphireGPU2D::SoftRenderer> SapphireVulkan2DAccess;
#endif

    // Core safety fix, intentionally shared with non-MelonPrime builds:
    // capture metadata must be deterministic before the first renderer install.
    u16 VRAMCaptureBlockFlags[16] {};

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
};

class Renderer
{
public:
    explicit Renderer(melonDS::GPU& gpu) : GPU(gpu), BackBuffer(0) {}
    virtual ~Renderer() {}
    virtual bool Init() = 0;
    virtual void Reset() = 0;
    virtual void Stop() = 0;

    virtual void PreSavestate() {}
    virtual void PostSavestate() {}

    virtual void SetRenderSettings(RendererSettings& settings) = 0;

    virtual void DrawScanline(u32 line) = 0;
    virtual void DrawSprites(u32 line) = 0;

    Renderer3D& GetRenderer3D() noexcept
    {
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        if (GPU.GPU3D.HasCurrentRenderer())
            return GPU.GPU3D.GetCurrentRenderer();
#endif
        return *Rend3D;
    }
    const Renderer3D& GetRenderer3D() const noexcept
    {
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        if (GPU.GPU3D.HasCurrentRenderer())
            return GPU.GPU3D.GetCurrentRenderer();
#endif
        return *Rend3D;
    }
    virtual void Start3DRendering() { GetRenderer3D().RenderFrame(); }
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    virtual void VCount1443D() { GetRenderer3D().VCount144(); }
#endif
    virtual void Finish3DRendering() { GetRenderer3D().FinishRendering(); }
    virtual void Restart3DRendering() { GetRenderer3D().RestartFrame(); }

    virtual void VBlank() = 0;
    virtual void VBlankEnd() = 0;

    virtual void AllocCapture(u32 bank, u32 start, u32 len) = 0;
    virtual void SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete) = 0;

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
#if defined(MELONPRIME_DS)
    virtual RendererOutputLease AcquireOutputLease()
    {
        return RendererOutputLease(GetOutput(), nullptr, nullptr);
    }
#endif
    virtual void SwapBuffers() { BackBuffer ^= 1; }

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    virtual bool NeedsShaderCompile() { return GetRenderer3D().NeedsShaderCompile(); }
    virtual void ShaderCompileStep(int& current, int& count) { GetRenderer3D().ShaderCompileStep(current, count); }
#else
    virtual bool NeedsShaderCompile() { return false; }
    virtual void ShaderCompileStep(int& current, int& count) {}
#endif

protected:
    melonDS::GPU& GPU;

    int BackBuffer;

    std::unique_ptr<Renderer2D> Rend2D_A;
    std::unique_ptr<Renderer2D> Rend2D_B;
    std::unique_ptr<Renderer3D> Rend3D;
};

}

#endif

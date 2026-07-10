// MelonPrimeDS - experimental Metal 2D renderer scaffold (Metal-plan Phase 4)

#if defined(MELONPRIME_ENABLE_METAL)

#import <Metal/Metal.h>

#include "GPU.h"
#include "GPU2D_Metal.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace melonDS
{

namespace
{
constexpr int kScreenW = 256;
constexpr int kScreenH = 192;
constexpr int kBGLayerCount = 22;

constexpr uint8_t kBGBaseIndex[4][4] = {
    {2, 10, 6, 14},
    {0, 4, 16, 20},
    {0, 4, 12, 16},
    {18, 19, 12, 16},
};

struct LayerConfigCpu
{
    uint32_t vramMask = 0;
    uint32_t pad0[3] = {};
    struct BGConfig
    {
        uint32_t size[2] = {};
        uint32_t type = 0;
        uint32_t palOffset = 0;
        uint32_t tileOffset = 0;
        uint32_t mapOffset = 0;
        uint32_t clamp = 0;
        uint32_t pad0[1] = {};
    } bgConfig[4];
};

struct SpriteConfigCpu
{
    uint32_t vramMask = 0;
    uint32_t pad0[3] = {};
    int32_t rotscale[32][4] = {};
    struct OAMConfig
    {
        int32_t position[2] = {};
        int32_t flip[2] = {};
        int32_t size[2] = {};
        int32_t boundSize[2] = {};
        uint32_t objMode = 0;
        uint32_t type = 0;
        uint32_t palOffset = 0;
        uint32_t tileOffset = 0;
        uint32_t tileStride = 0;
        uint32_t rotscale = 0;
        uint32_t bgPrio = 0;
        uint32_t mosaic = 0;
    } oam[128];
};

struct ScanlineConfigCpu
{
    struct Scanline
    {
        int32_t bgOffset[4][4] = {};
        int32_t bgRotscale[2][4] = {};
        uint32_t backColor = 0;
        uint32_t winRegs = 0;
        uint32_t winMask = 0;
        uint32_t pad0[1] = {};
        int32_t winPos[4] = {};
        uint32_t bgMosaicEnable[4] = {};
        int32_t mosaicSize[4] = {};
    } scanline[192];
};

struct SpriteScanlineConfigCpu
{
    int32_t mosaicLine[192] = {};
};

struct CompositorConfigCpu
{
    uint32_t bgPrio[4] = {};
    uint32_t enableOBJ = 0;
    uint32_t enable3D = 0;
    uint32_t blendCnt = 0;
    uint32_t blendEffect = 0;
    uint32_t blendCoef[4] = {};
};

static_assert((sizeof(LayerConfigCpu) & 15) == 0);
static_assert((sizeof(SpriteConfigCpu) & 15) == 0);
static_assert((sizeof(ScanlineConfigCpu) & 15) == 0);
static_assert((sizeof(SpriteScanlineConfigCpu) & 15) == 0);
static_assert((sizeof(CompositorConfigCpu) & 15) == 0);
}

struct MetalRenderer2D::Metal2DState
{
    id<MTLDevice> Device = nil;
    id<MTLTexture> OutputTex = nil;
    id<MTLTexture> OBJLayerTex = nil;
    id<MTLTexture> OBJDepthTex = nil;
    id<MTLTexture> AllBGLayerTex[kBGLayerCount] = {};
    id<MTLTexture> BGLayerTex[4] = {};
    id<MTLTexture> VRAMTexBG = nil;
    id<MTLTexture> VRAMTexOBJ = nil;
    id<MTLTexture> PalTexBG = nil;
    id<MTLTexture> PalTexOBJ = nil;
    id<MTLTexture> MosaicTex = nil;
    id<MTLTexture> SpriteTex = nil;
    id<MTLBuffer> LayerConfigBuffer = nil;
    id<MTLBuffer> SpriteConfigBuffer = nil;
    id<MTLBuffer> ScanlineConfigBuffer = nil;
    id<MTLBuffer> SpriteScanlineConfigBuffer = nil;
    id<MTLBuffer> CompositorConfigBuffer = nil;
    LayerConfigCpu LayerConfig;
    uint32_t BGVRAMRange[4][4] = {};
    int Scale = 0;
    bool LoggedFirstAllocation = false;
};

MetalRenderer2D::MetalRenderer2D(melonDS::GPU2D& gpu2D) noexcept
    : Renderer2D(gpu2D),
      State(std::make_unique<Metal2DState>())
{
}

MetalRenderer2D::~MetalRenderer2D() = default;

bool MetalRenderer2D::Configure(void* preferredDevice, int scale) noexcept
{
    if (!State)
        return false;

    Metal2DState& state = *State;
    id<MTLDevice> preferredMetalDevice = preferredDevice ? (__bridge id<MTLDevice>)preferredDevice : nil;
    if (preferredMetalDevice && state.Device && state.Device != preferredMetalDevice)
    {
        state.OutputTex = nil;
        state.OBJLayerTex = nil;
        state.OBJDepthTex = nil;
        for (id<MTLTexture>& texture : state.AllBGLayerTex)
            texture = nil;
        for (id<MTLTexture>& texture : state.BGLayerTex)
            texture = nil;
        state.VRAMTexBG = nil;
        state.VRAMTexOBJ = nil;
        state.PalTexBG = nil;
        state.PalTexOBJ = nil;
        state.MosaicTex = nil;
        state.SpriteTex = nil;
        state.LayerConfigBuffer = nil;
        state.SpriteConfigBuffer = nil;
        state.ScanlineConfigBuffer = nil;
        state.SpriteScanlineConfigBuffer = nil;
        state.CompositorConfigBuffer = nil;
        state.Scale = 0;
        state.Device = preferredMetalDevice;
    }

    if (!state.Device)
    {
        state.Device = preferredMetalDevice ? preferredMetalDevice : MTLCreateSystemDefaultDevice();
        if (!state.Device)
        {
            std::fprintf(stderr, "[MelonPrime] metal 2d: failed to create Metal device\n");
            return false;
        }
    }

    ScaleFactor = std::max(1, scale);
    if (state.OutputTex && state.OBJLayerTex && state.OBJDepthTex &&
        state.AllBGLayerTex[0] && state.VRAMTexBG && state.VRAMTexOBJ &&
        state.PalTexBG && state.PalTexOBJ && state.MosaicTex && state.SpriteTex &&
        state.LayerConfigBuffer && state.SpriteConfigBuffer && state.ScanlineConfigBuffer &&
        state.SpriteScanlineConfigBuffer && state.CompositorConfigBuffer &&
        state.Scale == ScaleFactor)
    {
        return true;
    }

    const NSUInteger width = static_cast<NSUInteger>(kScreenW * ScaleFactor);
    const NSUInteger height = static_cast<NSUInteger>(kScreenH * ScaleFactor);

    MTLTextureDescriptor* outputDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    outputDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    outputDesc.storageMode = MTLStorageModePrivate;

    id<MTLTexture> newOutput = [state.Device newTextureWithDescriptor:outputDesc];
    MTLTextureDescriptor* objLayerDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    objLayerDesc.textureType = MTLTextureType2DArray;
    objLayerDesc.arrayLength = 2;
    objLayerDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    objLayerDesc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> newOBJLayer = [state.Device newTextureWithDescriptor:objLayerDesc];

    MTLTextureDescriptor* objDepthDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    objDepthDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    objDepthDesc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> newOBJDepth = [state.Device newTextureWithDescriptor:objDepthDesc];

    MTLTextureDescriptor* bgVRAMDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Uint
                                                           width:1024
                                                          height:(GPU2D.Num == 0) ? 512 : 128
                                                       mipmapped:NO];
    bgVRAMDesc.usage = MTLTextureUsageShaderRead;
    bgVRAMDesc.storageMode = MTLStorageModeShared;
    id<MTLTexture> newVRAMBG = [state.Device newTextureWithDescriptor:bgVRAMDesc];

    MTLTextureDescriptor* objVRAMDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Uint
                                                           width:1024
                                                          height:(GPU2D.Num == 0) ? 256 : 128
                                                       mipmapped:NO];
    objVRAMDesc.usage = MTLTextureUsageShaderRead;
    objVRAMDesc.storageMode = MTLStorageModeShared;
    id<MTLTexture> newVRAMOBJ = [state.Device newTextureWithDescriptor:objVRAMDesc];

    MTLTextureDescriptor* bgPalDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR16Uint
                                                           width:256
                                                          height:1 + (4 * 16)
                                                       mipmapped:NO];
    bgPalDesc.usage = MTLTextureUsageShaderRead;
    bgPalDesc.storageMode = MTLStorageModeShared;
    id<MTLTexture> newPalBG = [state.Device newTextureWithDescriptor:bgPalDesc];

    MTLTextureDescriptor* objPalDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR16Uint
                                                           width:256
                                                          height:1 + 16
                                                       mipmapped:NO];
    objPalDesc.usage = MTLTextureUsageShaderRead;
    objPalDesc.storageMode = MTLStorageModeShared;
    id<MTLTexture> newPalOBJ = [state.Device newTextureWithDescriptor:objPalDesc];

    MTLTextureDescriptor* mosaicDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Sint
                                                           width:256
                                                          height:16
                                                       mipmapped:NO];
    mosaicDesc.usage = MTLTextureUsageShaderRead;
    mosaicDesc.storageMode = MTLStorageModeShared;
    id<MTLTexture> newMosaic = [state.Device newTextureWithDescriptor:mosaicDesc];

    MTLTextureDescriptor* spriteDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:1024
                                                          height:512
                                                       mipmapped:NO];
    spriteDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    spriteDesc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> newSprite = [state.Device newTextureWithDescriptor:spriteDesc];

    id<MTLBuffer> newLayerConfig =
        [state.Device newBufferWithLength:sizeof(LayerConfigCpu) options:MTLResourceStorageModeShared];
    id<MTLBuffer> newSpriteConfig =
        [state.Device newBufferWithLength:sizeof(SpriteConfigCpu) options:MTLResourceStorageModeShared];
    id<MTLBuffer> newScanlineConfig =
        [state.Device newBufferWithLength:sizeof(ScanlineConfigCpu) options:MTLResourceStorageModeShared];
    id<MTLBuffer> newSpriteScanlineConfig =
        [state.Device newBufferWithLength:sizeof(SpriteScanlineConfigCpu) options:MTLResourceStorageModeShared];
    id<MTLBuffer> newCompositorConfig =
        [state.Device newBufferWithLength:sizeof(CompositorConfigCpu) options:MTLResourceStorageModeShared];

    id<MTLTexture> newBGLayers[kBGLayerCount] = {};
    const uint16_t bgSizes[8][3] = {
        {128, 128, 2},
        {256, 256, 4},
        {256, 512, 4},
        {512, 256, 4},
        {512, 512, 4},
        {512, 1024, 1},
        {1024, 512, 1},
        {1024, 1024, 2},
    };

    int bgLayer = 0;
    for (const auto& size : bgSizes)
    {
        for (int variant = 0; variant < size[2]; variant++)
        {
            MTLTextureDescriptor* bgDesc =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                    width:size[0]
                                                                   height:size[1]
                                                                mipmapped:NO];
            bgDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            bgDesc.storageMode = MTLStorageModePrivate;
            newBGLayers[bgLayer++] = [state.Device newTextureWithDescriptor:bgDesc];
        }
    }

    bool bgLayersReady = bgLayer == kBGLayerCount;
    for (id<MTLTexture> texture : newBGLayers)
        bgLayersReady = bgLayersReady && texture;

    if (!newOutput || !newOBJLayer || !newOBJDepth || !newVRAMBG || !newVRAMOBJ ||
        !newPalBG || !newPalOBJ || !newMosaic || !newSprite ||
        !newLayerConfig || !newSpriteConfig || !newScanlineConfig ||
        !newSpriteScanlineConfig || !newCompositorConfig || !bgLayersReady)
    {
        std::fprintf(stderr, "[MelonPrime] metal 2d: failed to allocate scaffold targets for engine %u\n", GPU2D.Num);
        return false;
    }

    int8_t mosaicPixels[256 * 16] = {};
    for (int m = 0; m < 16; m++)
    {
        int mosx = 0;
        for (int x = 0; x < 256; x++)
        {
            mosaicPixels[(m * 256) + x] = static_cast<int8_t>(mosx);
            mosx = (mosx == m) ? 0 : (mosx + 1);
        }
    }
    [newMosaic replaceRegion:MTLRegionMake2D(0, 0, 256, 16)
                 mipmapLevel:0
                   withBytes:mosaicPixels
                 bytesPerRow:256];

    state.OutputTex = newOutput;
    state.OBJLayerTex = newOBJLayer;
    state.OBJDepthTex = newOBJDepth;
    for (int i = 0; i < kBGLayerCount; i++)
        state.AllBGLayerTex[i] = newBGLayers[i];
    state.VRAMTexBG = newVRAMBG;
    state.VRAMTexOBJ = newVRAMOBJ;
    state.PalTexBG = newPalBG;
    state.PalTexOBJ = newPalOBJ;
    state.MosaicTex = newMosaic;
    state.SpriteTex = newSprite;
    state.LayerConfigBuffer = newLayerConfig;
    state.SpriteConfigBuffer = newSpriteConfig;
    state.ScanlineConfigBuffer = newScanlineConfig;
    state.SpriteScanlineConfigBuffer = newSpriteScanlineConfig;
    state.CompositorConfigBuffer = newCompositorConfig;
    state.Scale = ScaleFactor;
    if (!UploadRawVRAMInputs())
    {
        std::fprintf(stderr, "[MelonPrime] metal 2d: failed to upload initial raw VRAM inputs for engine %u\n", GPU2D.Num);
        return false;
    }
    if (!UploadPaletteInputs())
    {
        std::fprintf(stderr, "[MelonPrime] metal 2d: failed to upload initial palette inputs for engine %u\n", GPU2D.Num);
        return false;
    }
    if (!RefreshLayerConfig())
    {
        std::fprintf(stderr, "[MelonPrime] metal 2d: failed to refresh initial layer config for engine %u\n", GPU2D.Num);
        return false;
    }

    if (!state.LoggedFirstAllocation)
    {
        state.LoggedFirstAllocation = true;
        std::fprintf(stderr,
            "[MelonPrime] metal 2d: scaffold targets engine=%u scale=%d size=%zux%zu bgLayers=%d objLayers=2 configBuffers=5 visible=0\n",
            GPU2D.Num,
            state.Scale,
            static_cast<size_t>(width),
            static_cast<size_t>(height),
            kBGLayerCount);
    }

    return true;
}

void MetalRenderer2D::DrawScanline(u32 line)
{
    (void)line;
}

void MetalRenderer2D::DrawSprites(u32 line)
{
    (void)line;
}

bool MetalRenderer2D::UploadRawVRAMInputs() noexcept
{
    if (!State || !State->VRAMTexBG || !State->VRAMTexOBJ)
        return false;

    uint8_t* bgVRAM = nullptr;
    uint32_t bgMask = 0;
    GPU2D.GetBGVRAM(bgVRAM, bgMask);
    if (!bgVRAM)
        return false;

    const NSUInteger bgHeight = State->VRAMTexBG.height;
    const NSUInteger bgBytesPerRow = State->VRAMTexBG.width;
    const size_t bgBytes = static_cast<size_t>(bgBytesPerRow) * static_cast<size_t>(bgHeight);
    if (bgBytes != static_cast<size_t>(bgMask) + 1u)
        return false;

    [State->VRAMTexBG replaceRegion:MTLRegionMake2D(0, 0, State->VRAMTexBG.width, bgHeight)
                        mipmapLevel:0
                          withBytes:bgVRAM
                        bytesPerRow:bgBytesPerRow];

    uint8_t* objVRAM = nullptr;
    uint32_t objMask = 0;
    GPU2D.GetOBJVRAM(objVRAM, objMask);
    if (!objVRAM)
        return false;

    const NSUInteger objHeight = State->VRAMTexOBJ.height;
    const NSUInteger objBytesPerRow = State->VRAMTexOBJ.width;
    const size_t objBytes = static_cast<size_t>(objBytesPerRow) * static_cast<size_t>(objHeight);
    if (objBytes != static_cast<size_t>(objMask) + 1u)
        return false;

    [State->VRAMTexOBJ replaceRegion:MTLRegionMake2D(0, 0, State->VRAMTexOBJ.width, objHeight)
                         mipmapLevel:0
                           withBytes:objVRAM
                         bytesPerRow:objBytesPerRow];

    return true;
}

bool MetalRenderer2D::UploadPaletteInputs() noexcept
{
    if (!State || !State->PalTexBG || !State->PalTexOBJ)
        return false;

    std::array<uint16_t, 256 * (1 + (4 * 16))> bgPalette = {};
    std::memcpy(bgPalette.data(), &GPU.Palette[GPU2D.Num ? 0x400 : 0], 256 * sizeof(uint16_t));
    for (int slot = 0; slot < 4; slot++)
    {
        for (int pal = 0; pal < 16; pal++)
        {
            uint16_t* extPal = GPU2D.GetBGExtPal(slot, pal);
            if (extPal)
            {
                std::memcpy(&bgPalette[(1 + ((slot * 16) + pal)) * 256],
                            extPal,
                            256 * sizeof(uint16_t));
            }
        }
    }
    [State->PalTexBG replaceRegion:MTLRegionMake2D(0, 0, 256, 1 + (4 * 16))
                        mipmapLevel:0
                          withBytes:bgPalette.data()
                        bytesPerRow:256 * sizeof(uint16_t)];

    std::array<uint16_t, 256 * (1 + 16)> objPalette = {};
    std::memcpy(objPalette.data(), &GPU.Palette[GPU2D.Num ? 0x600 : 0x200], 256 * sizeof(uint16_t));
    uint16_t* objExtPal = GPU2D.GetOBJExtPal();
    if (objExtPal)
    {
        std::memcpy(&objPalette[256],
                    objExtPal,
                    256 * 16 * sizeof(uint16_t));
    }
    [State->PalTexOBJ replaceRegion:MTLRegionMake2D(0, 0, 256, 1 + 16)
                         mipmapLevel:0
                           withBytes:objPalette.data()
                         bytesPerRow:256 * sizeof(uint16_t)];

    return true;
}

bool MetalRenderer2D::RefreshLayerConfig() noexcept
{
    if (!State || !State->LayerConfigBuffer)
        return false;

    Metal2DState& state = *State;
    state.LayerConfig = {};
    for (auto& range : state.BGVRAMRange)
    {
        for (uint32_t& entry : range)
            entry = 0xFFFFFFFFu;
    }
    for (id<MTLTexture>& texture : state.BGLayerTex)
        texture = nil;

    int captureMask = GPU2D.Num ? 0x7 : 0x1F;
    int captureInfo[32] = {};
    GPU2D.GetCaptureInfo_BG(captureInfo);

    uint32_t tileBase = 0;
    uint32_t mapBase = 0;
    if (!GPU2D.Num)
    {
        tileBase = ((GPU2D.DispCnt >> 24) & 0x7) << 16;
        mapBase = ((GPU2D.DispCnt >> 27) & 0x7) << 16;
    }

    int layerType[4] = {1, 1, 0, 0};
    switch (GPU2D.DispCnt & 0x7)
    {
    case 0: layerType[2] = 1; layerType[3] = 1; break;
    case 1: layerType[2] = 1; layerType[3] = 2; break;
    case 2: layerType[2] = 2; layerType[3] = 2; break;
    case 3: layerType[2] = 1; layerType[3] = 3; break;
    case 4: layerType[2] = 2; layerType[3] = 3; break;
    case 5: layerType[2] = 3; layerType[3] = 3; break;
    case 6:
        layerType[0] = 0;
        layerType[1] = 0;
        layerType[2] = 4;
        layerType[3] = 0;
        break;
    case 7:
        layerType[2] = 0;
        layerType[3] = 0;
        break;
    }

    for (int layer = 0; layer < 4; layer++)
    {
        const int type = layerType[layer];
        if (!type)
            continue;

        const uint16_t bgCnt = GPU2D.BGCnt[layer];
        auto& cfg = state.LayerConfig.bgConfig[layer];

        cfg.tileOffset = tileBase + (((bgCnt >> 2) & 0xF) << 14);
        cfg.mapOffset = mapBase + (((bgCnt >> 8) & 0x1F) << 11);
        cfg.palOffset = 0;

        state.BGVRAMRange[layer][0] = cfg.tileOffset;
        state.BGVRAMRange[layer][2] = cfg.mapOffset;

        if ((layer == 0) && (GPU2D.DispCnt & (1 << 3)))
        {
            cfg.size[0] = 256;
            cfg.size[1] = 192;
            cfg.type = 6;
            cfg.clamp = 1;
            continue;
        }

        if (type == 1)
        {
            uint32_t tileSize = 0;
            uint32_t mapSize = 0;
            switch (bgCnt >> 14)
            {
            case 0: cfg.size[0] = 256; cfg.size[1] = 256; mapSize = 0x800; break;
            case 1: cfg.size[0] = 512; cfg.size[1] = 256; mapSize = 0x1000; break;
            case 2: cfg.size[0] = 256; cfg.size[1] = 512; mapSize = 0x1000; break;
            case 3: cfg.size[0] = 512; cfg.size[1] = 512; mapSize = 0x2000; break;
            }

            if (bgCnt & (1 << 7))
            {
                cfg.type = 1;
                if (GPU2D.DispCnt & (1 << 30))
                {
                    int palOff = layer;
                    if ((layer < 2) && (bgCnt & (1 << 13)))
                        palOff += 2;
                    cfg.palOffset = 1 + (16 * palOff);
                }
                tileSize = 0x10000;
            }
            else
            {
                cfg.type = 0;
                tileSize = 0x8000;
            }

            cfg.clamp = 0;
            const int target = kBGBaseIndex[0][bgCnt >> 14] + layer;
            state.BGLayerTex[layer] = state.AllBGLayerTex[target];
            state.BGVRAMRange[layer][1] = tileSize;
            state.BGVRAMRange[layer][3] = mapSize;
        }
        else if (type == 2)
        {
            uint32_t mapSize = 0;
            switch (bgCnt >> 14)
            {
            case 0: cfg.size[0] = 128; cfg.size[1] = 128; mapSize = 0x100; break;
            case 1: cfg.size[0] = 256; cfg.size[1] = 256; mapSize = 0x400; break;
            case 2: cfg.size[0] = 512; cfg.size[1] = 512; mapSize = 0x1000; break;
            case 3: cfg.size[0] = 1024; cfg.size[1] = 1024; mapSize = 0x4000; break;
            }

            cfg.type = 2;
            cfg.clamp = !(bgCnt & (1 << 13));
            const int target = kBGBaseIndex[1][bgCnt >> 14] + layer - 2;
            state.BGLayerTex[layer] = state.AllBGLayerTex[target];
            state.BGVRAMRange[layer][1] = 0x4000;
            state.BGVRAMRange[layer][3] = mapSize;
        }
        else if (type == 3)
        {
            if (bgCnt & (1 << 7))
            {
                uint32_t mapSize = 0;
                switch (bgCnt >> 14)
                {
                case 0: cfg.size[0] = 128; cfg.size[1] = 128; mapSize = 0x4000; break;
                case 1: cfg.size[0] = 256; cfg.size[1] = 256; mapSize = 0x10000; break;
                case 2: cfg.size[0] = 512; cfg.size[1] = 256; mapSize = 0x20000; break;
                case 3: cfg.size[0] = 512; cfg.size[1] = 512; mapSize = 0x40000; break;
                }

                uint32_t tileOffset = 0;
                uint32_t mapOffset = ((bgCnt >> 8) & 0x1F) << 14;
                state.BGVRAMRange[layer][0] = 0xFFFFFFFFu;
                state.BGVRAMRange[layer][1] = 0xFFFFFFFFu;
                state.BGVRAMRange[layer][2] = mapOffset;
                state.BGVRAMRange[layer][3] = mapSize;

                if (bgCnt & (1 << 2))
                {
                    mapSize <<= 1;
                    int capBlock = -1;
                    if ((cfg.size[0] == 128) || (cfg.size[0] == 256))
                    {
                        uint32_t startAddr = mapOffset;
                        uint32_t endAddr = startAddr + mapSize;
                        startAddr >>= 14;
                        endAddr = (endAddr + 0x3FFF) >> 14;
                        for (uint32_t block = startAddr; block < endAddr; block++)
                        {
                            const int captured = captureInfo[block & captureMask];
                            if (captured != -1)
                                capBlock = captured;
                        }
                    }

                    if (capBlock != -1)
                    {
                        if (cfg.size[0] == 128)
                        {
                            cfg.type = 7;
                            tileOffset = capBlock;
                            mapOffset = (mapOffset >> 8) & 0x7F;
                        }
                        else
                        {
                            cfg.type = 8;
                            tileOffset = capBlock >> 2;
                            mapOffset = (mapOffset >> 9) & 0xFF;
                        }
                    }
                    else
                        cfg.type = 5;
                }
                else
                    cfg.type = 4;

                cfg.tileOffset = tileOffset;
                cfg.mapOffset = mapOffset;
                const int target = kBGBaseIndex[2][bgCnt >> 14] + layer - 2;
                state.BGLayerTex[layer] = state.AllBGLayerTex[target];
            }
            else
            {
                uint32_t mapSize = 0;
                switch (bgCnt >> 14)
                {
                case 0: cfg.size[0] = 128; cfg.size[1] = 128; mapSize = 0x200; break;
                case 1: cfg.size[0] = 256; cfg.size[1] = 256; mapSize = 0x800; break;
                case 2: cfg.size[0] = 512; cfg.size[1] = 512; mapSize = 0x2000; break;
                case 3: cfg.size[0] = 1024; cfg.size[1] = 1024; mapSize = 0x8000; break;
                }

                cfg.type = 3;
                if (GPU2D.DispCnt & (1 << 30))
                {
                    int palOff = layer;
                    if ((layer < 2) && (bgCnt & (1 << 13)))
                        palOff += 2;
                    cfg.palOffset = 1 + (16 * palOff);
                }

                const int target = kBGBaseIndex[1][bgCnt >> 14] + layer - 2;
                state.BGLayerTex[layer] = state.AllBGLayerTex[target];
                state.BGVRAMRange[layer][1] = 0x10000;
                state.BGVRAMRange[layer][3] = mapSize;
            }

            cfg.clamp = !(bgCnt & (1 << 13));
        }
        else
        {
            uint32_t mapSize = 0;
            switch (bgCnt >> 14)
            {
            case 0: cfg.size[0] = 512; cfg.size[1] = 1024; mapSize = 0x80000; break;
            case 1: cfg.size[0] = 1024; cfg.size[1] = 512; mapSize = 0x80000; break;
            case 2: cfg.size[0] = 512; cfg.size[1] = 256; mapSize = 0x20000; break;
            case 3: cfg.size[0] = 512; cfg.size[1] = 512; mapSize = 0x40000; break;
            }

            cfg.type = 4;
            cfg.tileOffset = 0;
            cfg.mapOffset = 0;
            cfg.clamp = !(bgCnt & (1 << 13));
            const int target = kBGBaseIndex[3][bgCnt >> 14];
            state.BGLayerTex[layer] = state.AllBGLayerTex[target];
            state.BGVRAMRange[layer][0] = 0xFFFFFFFFu;
            state.BGVRAMRange[layer][1] = 0xFFFFFFFFu;
            state.BGVRAMRange[layer][3] = mapSize;
        }
    }

    std::memcpy([state.LayerConfigBuffer contents], &state.LayerConfig, sizeof(state.LayerConfig));
    return true;
}

void MetalRenderer2D::Reset() noexcept
{
    if (!State)
        return;

    State->OutputTex = nil;
    State->OBJLayerTex = nil;
    State->OBJDepthTex = nil;
    for (id<MTLTexture>& texture : State->AllBGLayerTex)
        texture = nil;
    for (id<MTLTexture>& texture : State->BGLayerTex)
        texture = nil;
    State->VRAMTexBG = nil;
    State->VRAMTexOBJ = nil;
    State->PalTexBG = nil;
    State->PalTexOBJ = nil;
    State->MosaicTex = nil;
    State->SpriteTex = nil;
    State->LayerConfigBuffer = nil;
    State->SpriteConfigBuffer = nil;
    State->ScanlineConfigBuffer = nil;
    State->SpriteScanlineConfigBuffer = nil;
    State->CompositorConfigBuffer = nil;
    State->Scale = 0;
}

void* MetalRenderer2D::GetOutputTexture() const noexcept
{
    if (!State || !State->OutputTex)
        return nullptr;
    return (__bridge void*)State->OutputTex;
}

void* MetalRenderer2D::GetOBJLayerTexture() const noexcept
{
    if (!State || !State->OBJLayerTex)
        return nullptr;
    return (__bridge void*)State->OBJLayerTex;
}

void* MetalRenderer2D::GetOBJDepthTexture() const noexcept
{
    if (!State || !State->OBJDepthTex)
        return nullptr;
    return (__bridge void*)State->OBJDepthTex;
}

void* MetalRenderer2D::GetBGLayerTexture(int index) const noexcept
{
    if (!State || index < 0 || index >= kBGLayerCount || !State->AllBGLayerTex[index])
        return nullptr;
    return (__bridge void*)State->AllBGLayerTex[index];
}

void* MetalRenderer2D::GetBGVRAMTexture() const noexcept
{
    if (!State || !State->VRAMTexBG)
        return nullptr;
    return (__bridge void*)State->VRAMTexBG;
}

void* MetalRenderer2D::GetOBJVRAMTexture() const noexcept
{
    if (!State || !State->VRAMTexOBJ)
        return nullptr;
    return (__bridge void*)State->VRAMTexOBJ;
}

void* MetalRenderer2D::GetBGPaletteTexture() const noexcept
{
    if (!State || !State->PalTexBG)
        return nullptr;
    return (__bridge void*)State->PalTexBG;
}

void* MetalRenderer2D::GetOBJPaletteTexture() const noexcept
{
    if (!State || !State->PalTexOBJ)
        return nullptr;
    return (__bridge void*)State->PalTexOBJ;
}

void* MetalRenderer2D::GetMosaicTexture() const noexcept
{
    if (!State || !State->MosaicTex)
        return nullptr;
    return (__bridge void*)State->MosaicTex;
}

void* MetalRenderer2D::GetSpriteTexture() const noexcept
{
    if (!State || !State->SpriteTex)
        return nullptr;
    return (__bridge void*)State->SpriteTex;
}

int MetalRenderer2D::GetScaleFactor() const noexcept
{
    return ScaleFactor;
}

int MetalRenderer2D::GetTargetWidth() const noexcept
{
    return State && State->OutputTex ? static_cast<int>(State->OutputTex.width) : 0;
}

int MetalRenderer2D::GetTargetHeight() const noexcept
{
    return State && State->OutputTex ? static_cast<int>(State->OutputTex.height) : 0;
}

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL

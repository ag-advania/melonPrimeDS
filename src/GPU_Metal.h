// MelonPrimeDS - experimental Metal renderer shell (Metal-plan Phase 7)

#ifndef GPU_METAL_H
#define GPU_METAL_H

#if defined(MELONPRIME_ENABLE_METAL)

#include "GPU.h"

namespace melonDS
{

class MetalRenderer : public Renderer
{
public:
    explicit MetalRenderer(melonDS::NDS& nds) noexcept;
    ~MetalRenderer() override = default;

    bool Init() override;
    void Reset() override {}
    void Stop() override {}

    void SetRenderSettings(RendererSettings& settings) override { (void)settings; }

    void DrawScanline(u32 line) override { (void)line; }
    void DrawSprites(u32 line) override { (void)line; }

    void VBlank() override {}
    void VBlankEnd() override {}

    void AllocCapture(u32 bank, u32 start, u32 len) override
    {
        (void)bank;
        (void)start;
        (void)len;
    }

    void SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete) override
    {
        (void)bank;
        (void)start;
        (void)len;
        (void)complete;
    }

    bool GetFramebuffers(void** top, void** bottom) override;
    RendererOutput GetOutput() override { return {}; }
};

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
#endif // GPU_METAL_H

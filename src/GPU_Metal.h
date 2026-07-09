// MelonPrimeDS - experimental Metal renderer (Metal-plan Phase 8)

#ifndef GPU_METAL_H
#define GPU_METAL_H

#if defined(MELONPRIME_ENABLE_METAL)

#include "GPU_Soft.h"

#include <memory>

namespace melonDS
{

class MetalRenderer : public SoftRenderer
{
public:
    explicit MetalRenderer(melonDS::NDS& nds) noexcept;
    ~MetalRenderer() override;

    bool Init() override;
    void PreSavestate() override;
    void PostSavestate() override;
    void SetRenderSettings(RendererSettings& settings) override;
    void VBlank() override;
    RendererOutput GetOutput() override;

private:
    struct MetalFinalState;

    std::unique_ptr<MetalFinalState> FinalState;
    int ScaleFactor = 1;

    bool EnsureFinalOutput();
    bool EnsureFinalOutputForDevice(void* preferredDevice);
    bool ComposeFinalOutputForCompletedFrame();
    RendererOutput GetSoftwareFallbackOutput();
};

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
#endif // GPU_METAL_H

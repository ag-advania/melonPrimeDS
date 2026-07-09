// MelonPrimeDS - experimental Metal 2D renderer scaffold (Metal-plan Phase 4)

#ifndef GPU2D_METAL_H
#define GPU2D_METAL_H

#if defined(MELONPRIME_ENABLE_METAL)

#include <memory>

#include "GPU2D.h"

namespace melonDS
{

class MetalRenderer2D final
{
public:
    explicit MetalRenderer2D(melonDS::GPU2D& gpu2D) noexcept;
    ~MetalRenderer2D();

    bool Configure(void* preferredDevice, int scale) noexcept;
    void Reset() noexcept;

    [[nodiscard]] void* GetOutputTexture() const noexcept;
    [[nodiscard]] void* GetOBJLayerTexture() const noexcept;
    [[nodiscard]] void* GetOBJDepthTexture() const noexcept;
    [[nodiscard]] int GetScaleFactor() const noexcept;
    [[nodiscard]] int GetTargetWidth() const noexcept;
    [[nodiscard]] int GetTargetHeight() const noexcept;

private:
    struct Metal2DState;

    melonDS::GPU2D& GPU2D;
    std::unique_ptr<Metal2DState> State;
    int ScaleFactor = 1;
};

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
#endif // GPU2D_METAL_H

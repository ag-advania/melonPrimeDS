#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "MelonPrimeSapphireGpu2DAdapter.h"

#include "GPU.h"
#include "GPU_Soft.h"

namespace melonDS::MelonPrimeSapphireGpu2DAdapter
{

void ForwardRegisterWrite8(GPU& gpu, u32 engineNum, u32 addr, u8 val) noexcept
{
    if (auto* renderer = dynamic_cast<SoftRenderer*>(&gpu.GetRenderer()))
        renderer->ForwardSapphireGpu2DRegisterWrite8(engineNum, addr, val);
}

void ForwardRegisterWrite16(GPU& gpu, u32 engineNum, u32 addr, u16 val) noexcept
{
    if (auto* renderer = dynamic_cast<SoftRenderer*>(&gpu.GetRenderer()))
        renderer->ForwardSapphireGpu2DRegisterWrite16(engineNum, addr, val);
}

void ForwardRegisterWrite32(GPU& gpu, u32 engineNum, u32 addr, u32 val) noexcept
{
    if (auto* renderer = dynamic_cast<SoftRenderer*>(&gpu.GetRenderer()))
        renderer->ForwardSapphireGpu2DRegisterWrite32(engineNum, addr, val);
}

void ForwardWindowCheck(GPU& gpu, u32 line) noexcept
{
    if (auto* renderer = dynamic_cast<SoftRenderer*>(&gpu.GetRenderer()))
        renderer->ForwardSapphireGpu2DWindowCheck(line);
}

} // namespace melonDS::MelonPrimeSapphireGpu2DAdapter

#endif

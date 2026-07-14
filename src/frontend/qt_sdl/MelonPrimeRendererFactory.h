#pragma once

#ifdef MELONPRIME_DS

#include <memory>
#include <string>

namespace melonDS
{
class NDS;
class Renderer;
class Renderer3D;
}

namespace MelonPrime::VideoBackend
{

struct BackendCreationReport
{
    int requested = 0;
    int normalized = 0;
    int actual = 0;
    std::string failedStage;
    std::string fallbackReason;
};

std::unique_ptr<melonDS::Renderer> CreateRendererForSelection(
    melonDS::NDS& nds,
    int configuredRenderer,
    BackendCreationReport& report);

// MELONPRIME_SAPPHIRE_VULKAN_RENDERER3D_OWNERSHIP_A1
std::unique_ptr<melonDS::Renderer3D> CreateRenderer3DOverrideForSelection(
    melonDS::NDS& nds,
    int configuredRenderer,
    BackendCreationReport& report);

} // namespace MelonPrime::VideoBackend

#endif // MELONPRIME_DS

#pragma once

#ifdef MELONPRIME_DS

#include <memory>
#include <string>

namespace melonDS
{
class NDS;
class Renderer;
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

} // namespace MelonPrime::VideoBackend

#endif // MELONPRIME_DS

#include "MelonPrimeVulkanDebugIsolation.h"

#include <cstdlib>

namespace MelonPrime::VulkanDebugIsolation
{

namespace
{

bool envEnabled(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

} // namespace

ProducerBeginGate producerBeginGate()
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES) && defined(MELONPRIME_ENABLE_VULKAN)
    if (envEnabled("MELONPRIME_VULKAN_DEBUG_DISABLE_PRODUCER_BEGIN"))
        return ProducerBeginGate::Disabled;
#endif
    return ProducerBeginGate::Enabled;
}

Sapphire2DGate sapphire2DGate()
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES) && defined(MELONPRIME_ENABLE_VULKAN)
    if (envEnabled("MELONPRIME_VULKAN_DEBUG_DISABLE_SAPPHIRE_2D"))
        return Sapphire2DGate::Disabled;
#endif
    return Sapphire2DGate::Enabled;
}

} // namespace MelonPrime::VulkanDebugIsolation

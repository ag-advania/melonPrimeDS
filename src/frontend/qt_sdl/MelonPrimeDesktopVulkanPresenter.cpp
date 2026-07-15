#include "MelonPrimeDesktopVulkanPresenter.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <cstdio>

namespace MelonPrime::DesktopVulkan {

void LogBuildIdentity()
{
    printf(
        "[BuildIdentity] commit=%s vulkan=1 sapphireFrontend=%s sapphireCore=%s build=%s %s\n",
        MELONPRIME_GIT_COMMIT,
        kSapphireFrontendPin,
        kSapphireCorePin,
        kBuildStamp,
        MELONPRIMEDS_BUILD_TZ);
}

} // namespace MelonPrime::DesktopVulkan

#endif

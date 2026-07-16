#include "MelonPrimeDesktopVulkanPresenter.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <cstdio>

namespace MelonPrime::DesktopVulkan {

void LogBuildIdentity()
{
    printf(
        "[BuildIdentity] commit=%s commitFull=%s branch=%s dirty=%d "
        "vulkan=1 sapphireFrontend=%s sapphireCore=%s build=%s %s\n",
        MELONPRIME_GIT_COMMIT,
        MELONPRIME_GIT_COMMIT_FULL,
        MELONPRIME_GIT_BRANCH,
        MELONPRIME_GIT_DIRTY,
        kSapphireFrontendPin,
        kSapphireCorePin,
        kBuildStamp,
        MELONPRIMEDS_BUILD_TZ);
}

} // namespace MelonPrime::DesktopVulkan

#endif

// This file is a unity-build fragment included only by EmuThread.cpp.
// Keep MelonPrime-specific include clutter out of the upstream-facing header block.
#ifdef MELONPRIME_DS
#include "MelonPrime.h"
#include "MelonPrimePerfProbe.h"
#include "MelonPrimeVideoBackend.h"
#include "MelonPrimeRendererFactory.h"
#if defined(MELONPRIME_ENABLE_VULKAN)
#include "MelonPrimeVulkanFrontendSession.h"
#endif
#ifdef _WIN32
#include "MelonPrimeRawWinInternal.h"  // P-11: WinInternal::SetHighTimerResolution()
#endif
#endif // MELONPRIME_DS

/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef MELONPRIME_VULKAN_FEATURE_CHECK_H
#define MELONPRIME_VULKAN_FEATURE_CHECK_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <string>

namespace MelonPrime::VulkanFeatureCheck
{

// Everything the frontend needs to decide whether to offer the Vulkan renderer,
// and to explain the answer when it does not. Mirrors DX12FeatureCheck::Result
// field for field so VideoSettingsDialog can treat the two identically.
struct Result
{
    bool Available = false;
    bool NvidiaReflexAvailable = false;
    bool AmdAntiLag2Available = false;

    // Never empty when Available is false. This is the string the settings
    // dialog shows as the Vulkan radio button's tooltip, so "Vulkan is
    // unavailable" is not an acceptable value -- the probe always names the
    // requirement that failed and the value it observed.
    std::string Reason;
    std::string NvidiaReflexReason;
    std::string AmdAntiLag2Reason;

    // Physical device the probe selected, for the log and the tooltip.
    std::string AdapterName;
};

// Probes the Vulkan runtime, instance and physical device once, then caches the
// answer.
//
// This deliberately creates no renderer, no logical device and no swapchain:
// the settings dialog calls it from paint/enable paths, and building a device
// there would stall the UI and fight the live renderer for the GPU. It opens
// the loader, creates (or reuses) the shared VkInstance, scores the physical
// devices and releases its instance reference again before returning, so a
// probe can never keep the renderer from creating a presentation instance.
//
// Safe to call from the GUI thread and the emulation thread.
const Result& Probe();

// Convenience for the renderer-normalization path (MelonPrimeVideoBackend):
// a Vulkan renderer value carried over from another machine must not reach the
// renderer factory on one with no usable Vulkan device.
bool IsRuntimeAvailable();

// Marks Vulkan unavailable after a runtime failure -- a 3D renderer that failed
// to initialize, a presenter that could not create a surface or swapchain, or a
// device loss mid-session -- so the settings dialog stops offering it until the
// user explicitly retries.
void ReportRuntimeFailure(std::string reason);

// Drops the cached answer, including a sticky runtime failure, so the next
// Probe() re-runs from scratch.
void ResetProbeForRetry();

} // namespace MelonPrime::VulkanFeatureCheck

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // MELONPRIME_VULKAN_FEATURE_CHECK_H

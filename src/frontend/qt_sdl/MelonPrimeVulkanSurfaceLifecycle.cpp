/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "MelonPrimeVulkanSurfaceLifecycle.h"

namespace MelonPrime
{

void VulkanSurfaceLifecycle::publishSnapshotState(
    const VulkanSurface::NativeWindowSnapshot& snapshot,
    bool valid,
    bool hostShown)
{
    std::lock_guard<std::mutex> lock(Mutex);
    Snapshot = snapshot;
    Snapshot.Valid = valid && snapshot.IsValid();
    HostShown = hostShown;

    // A presenter retirement remains authoritative until the emulation thread
    // has quiesced the old Vulkan objects. DestroySafe is also preserved for
    // an invalid hidden snapshot so a SurfaceAboutToBeDestroyed caller cannot
    // accidentally downgrade the hard teardown barrier to Hidden.
    if (StateValue == State::RetireRequested || StateValue == State::Retiring)
    {
        RetireCondition.notify_all();
        return;
    }
    if (StateValue == State::DestroySafe && !Snapshot.IsValid() && !HostShown)
    {
        RetireCondition.notify_all();
        return;
    }

    if (Snapshot.IsValid() && HostShown)
        StateValue = State::Ready;
    else if (Snapshot.IsValid())
        StateValue = State::WaitingForShow;
    else if (HostShown)
        StateValue = State::WaitingForNativeSurface;
    else
        StateValue = State::Hidden;
    RetireCondition.notify_all();
}


void VulkanSurfaceLifecycle::requestRetire()
{
    std::lock_guard<std::mutex> lock(Mutex);
    if (StateValue == State::RetireRequested || StateValue == State::Retiring)
        return;

    Snapshot.Valid = false;
    if (!PresenterActive && ActiveFrames == 0)
        StateValue = State::DestroySafe;
    else
        StateValue = State::RetireRequested;
    RetireCondition.notify_all();
}


void VulkanSurfaceLifecycle::requestNativeTransitionRetire()
{
    std::lock_guard<std::mutex> lock(Mutex);
    HostShown = false;
    Snapshot.Valid = false;
    if (StateValue == State::RetireRequested || StateValue == State::Retiring)
    {
        RetireCondition.notify_all();
        return;
    }

    if (!PresenterActive && ActiveFrames == 0)
        StateValue = State::DestroySafe;
    else
        StateValue = State::RetireRequested;
    RetireCondition.notify_all();
}


bool VulkanSurfaceLifecycle::waitForDestroySafe(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(Mutex);
    return RetireCondition.wait_for(lock, timeout, [this]() {
        return StateValue == State::DestroySafe && ActiveFrames == 0;
    });
}


void VulkanSurfaceLifecycle::waitForDestroySafe()
{
    std::unique_lock<std::mutex> lock(Mutex);
    RetireCondition.wait(lock, [this]() {
        return StateValue == State::DestroySafe && ActiveFrames == 0;
    });
}


bool VulkanSurfaceLifecycle::beginFrame(VulkanSurface::NativeWindowSnapshot& snapshot)
{
    std::lock_guard<std::mutex> lock(Mutex);
    if ((StateValue != State::Ready && StateValue != State::Bound)
        || !HostShown || !Snapshot.IsValid())
    {
        return false;
    }

    snapshot = Snapshot;
    ++ActiveFrames;
    return true;
}


void VulkanSurfaceLifecycle::endFrame()
{
    std::lock_guard<std::mutex> lock(Mutex);
    if (ActiveFrames > 0)
        --ActiveFrames;
    RetireCondition.notify_all();
}


void VulkanSurfaceLifecycle::markBound(std::uint64_t generation)
{
    std::lock_guard<std::mutex> lock(Mutex);
    BoundGeneration = generation;
    PresenterActive = true;
    if (StateValue == State::Ready)
        StateValue = State::Bound;
    RetireCondition.notify_all();
}


void VulkanSurfaceLifecycle::markSurfaceLost()
{
    std::lock_guard<std::mutex> lock(Mutex);
    Snapshot.Valid = false;
    if (StateValue != State::RetireRequested && StateValue != State::Retiring)
        StateValue = State::WaitingForNativeSurface;
    RetireCondition.notify_all();
}


void VulkanSurfaceLifecycle::beginRetiring()
{
    std::lock_guard<std::mutex> lock(Mutex);
    if (StateValue == State::RetireRequested)
        StateValue = State::Retiring;
    RetireCondition.notify_all();
}


void VulkanSurfaceLifecycle::markPresenterRetired()
{
    std::lock_guard<std::mutex> lock(Mutex);
    BoundGeneration = 0;
    PresenterActive = false;
    if (StateValue == State::RetireRequested || StateValue == State::Retiring)
    {
        StateValue = Snapshot.IsValid() && HostShown ? State::Ready : State::DestroySafe;
    }
    RetireCondition.notify_all();
}


bool VulkanSurfaceLifecycle::retireRequested() const
{
    std::lock_guard<std::mutex> lock(Mutex);
    return StateValue == State::RetireRequested || StateValue == State::Retiring;
}


bool VulkanSurfaceLifecycle::presentationReady() const
{
    std::lock_guard<std::mutex> lock(Mutex);
    return HostShown && Snapshot.IsValid()
        && (StateValue == State::Ready || StateValue == State::Bound);
}


VulkanSurfaceLifecycle::State VulkanSurfaceLifecycle::state() const
{
    std::lock_guard<std::mutex> lock(Mutex);
    return StateValue;
}

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN

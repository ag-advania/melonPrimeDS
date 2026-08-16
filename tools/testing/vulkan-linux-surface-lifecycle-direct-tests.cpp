/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "MelonPrimeVulkanSurfaceLifecycle.h"

namespace
{

using MelonPrime::VulkanSurface::NativeWindowSnapshot;
using MelonPrime::VulkanSurfaceLifecycle;

NativeWindowSnapshot MakeSnapshot(std::uint64_t generation)
{
    NativeWindowSnapshot snapshot;
    snapshot.Generation = generation;
    snapshot.Platform = "test";
    snapshot.WindowId = 1;
    snapshot.Width = 1;
    snapshot.Height = 1;
    snapshot.Valid = true;
    return snapshot;
}

void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void TestPreShowSnapshotIsNotAdmitted()
{
    VulkanSurfaceLifecycle lifecycle;
    const NativeWindowSnapshot preShow = MakeSnapshot(1);

    lifecycle.publishSnapshotState(preShow, true, false);

    Require(
        lifecycle.state() == VulkanSurfaceLifecycle::State::WaitingForShow,
        "pre-Show snapshot did not remain WaitingForShow");
    Require(!lifecycle.presentationReady(), "pre-Show snapshot became ready");

    NativeWindowSnapshot frame;
    Require(!lifecycle.beginFrame(frame), "pre-Show snapshot admitted a frame");
}

void TestPostShowPublicationCarriesNewGenerationAtomically()
{
    VulkanSurfaceLifecycle lifecycle;
    const NativeWindowSnapshot preShow = MakeSnapshot(10);
    const NativeWindowSnapshot postShow = MakeSnapshot(11);

    lifecycle.publishSnapshotState(preShow, true, false);
    NativeWindowSnapshot frame;
    Require(!lifecycle.beginFrame(frame), "old pre-Show generation was admitted");

    lifecycle.publishSnapshotState(postShow, true, true);
    Require(lifecycle.presentationReady(), "post-Show generation was not ready");
    Require(lifecycle.beginFrame(frame), "post-Show generation was not admitted");
    Require(
        frame.Generation == postShow.Generation,
        "post-Show frame used the previous generation");
    lifecycle.endFrame();
}

void TestDestroyBarrierWaitsForPresenterAndActiveFrame()
{
    VulkanSurfaceLifecycle lifecycle;
    const NativeWindowSnapshot snapshot = MakeSnapshot(20);
    lifecycle.publishSnapshotState(snapshot, true, true);
    lifecycle.markBound(snapshot.Generation);

    NativeWindowSnapshot frame;
    Require(lifecycle.beginFrame(frame), "bound frame lease could not start");

    lifecycle.requestNativeTransitionRetire();
    Require(
        lifecycle.state() == VulkanSurfaceLifecycle::State::RetireRequested,
        "native transition did not stop new presentation");
    Require(!lifecycle.beginFrame(frame), "retiring lifecycle admitted a new frame");

    lifecycle.beginRetiring();
    lifecycle.markPresenterRetired();
    Require(
        lifecycle.state() == VulkanSurfaceLifecycle::State::DestroySafe,
        "presenter retirement did not reach DestroySafe");
    Require(
        !lifecycle.waitForDestroySafe(std::chrono::milliseconds(1)),
        "destruction barrier ignored an active frame lease");

    lifecycle.endFrame();
    Require(
        lifecycle.waitForDestroySafe(std::chrono::milliseconds(100)),
        "destruction barrier did not open after the active frame ended");
}

void TestDestroyBarrierCompletesWithoutPresenter()
{
    VulkanSurfaceLifecycle lifecycle;
    lifecycle.publishSnapshotState(MakeSnapshot(30), true, true);
    lifecycle.requestNativeTransitionRetire();

    Require(
        lifecycle.state() == VulkanSurfaceLifecycle::State::DestroySafe,
        "idle native transition did not reach DestroySafe");
    lifecycle.waitForDestroySafe();
    Require(
        lifecycle.state() == VulkanSurfaceLifecycle::State::DestroySafe,
        "idle destruction barrier changed state unexpectedly");
}

} // namespace

int main()
{
    try
    {
        TestPreShowSnapshotIsNotAdmitted();
        TestPostShowPublicationCarriesNewGenerationAtomically();
        TestDestroyBarrierWaitsForPresenterAndActiveFrame();
        TestDestroyBarrierCompletesWithoutPresenter();
        std::cout << "Vulkan production surface lifecycle direct tests: PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Vulkan production surface lifecycle direct tests: FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}

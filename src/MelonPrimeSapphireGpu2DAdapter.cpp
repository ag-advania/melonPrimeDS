#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "MelonPrimeSapphireGpu2DAdapter.h"

#include <cassert>

#include "GPU.h"
#include "MelonPrimeSapphireGpu2DState.h"

namespace melonDS::MelonPrimeSapphireGpu2DAdapter
{

namespace
{

SapphireGpu2DState* GetState(GPU& gpu) noexcept
{
    return gpu.TryGetSapphireGpu2DState();
}

bool IsActiveForRendering(const GPU& gpu) noexcept
{
    const SapphireGpu2DState* state = gpu.TryGetSapphireGpu2DState();
    return state != nullptr && state->IsActiveForRendering(gpu);
}

SapphireGPU2DCore::GPU2D::Unit& UnitForEngine(SapphireGpu2DState& state, u32 engineNum) noexcept
{
    return engineNum == 0 ? state.UnitA : state.UnitB;
}

void AssertEnabledParity(const GPU& gpu, const SapphireGpu2DState& state) noexcept
{
#ifndef NDEBUG
    assert(state.UnitA.Enabled == gpu.GPU2D_A.Enabled);
    assert(state.UnitB.Enabled == gpu.GPU2D_B.Enabled);
#else
    (void)gpu;
    (void)state;
#endif
}

} // namespace

void ForwardRegisterWrite8(GPU& gpu, u32 engineNum, u32 addr, u8 val) noexcept
{
    if (auto* state = GetState(gpu))
    {
        AssertEnabledParity(gpu, *state);
        UnitForEngine(*state, engineNum).Write8(addr, val);
    }
}

void ForwardRegisterWrite16(GPU& gpu, u32 engineNum, u32 addr, u16 val) noexcept
{
    if (auto* state = GetState(gpu))
    {
        AssertEnabledParity(gpu, *state);
        UnitForEngine(*state, engineNum).Write16(addr, val);
    }
}

void ForwardRegisterWrite32(GPU& gpu, u32 engineNum, u32 addr, u32 val) noexcept
{
    if (auto* state = GetState(gpu))
    {
        AssertEnabledParity(gpu, *state);
        UnitForEngine(*state, engineNum).Write32(addr, val);
    }
}

void ForwardWindowCheck(GPU& gpu, u32 line) noexcept
{
    if (!IsActiveForRendering(gpu))
        return;

    if (auto* state = GetState(gpu))
    {
        state->UnitA.CheckWindows(line);
        state->UnitB.CheckWindows(line);
    }
}

void ForwardVBlank(GPU& gpu) noexcept
{
    if (!IsActiveForRendering(gpu))
        return;

    if (auto* state = GetState(gpu))
    {
        state->UnitA.VBlank();
        state->UnitB.VBlank();
    }
}

void ForwardVBlankEnd(GPU& gpu) noexcept
{
    if (!IsActiveForRendering(gpu))
        return;

    if (auto* state = GetState(gpu))
    {
        state->Renderer.VBlankEnd(&state->UnitA, &state->UnitB);
        state->UnitA.VBlankEnd();
        state->UnitB.VBlankEnd();
    }
}

} // namespace melonDS::MelonPrimeSapphireGpu2DAdapter

#endif

#!/usr/bin/env python3
"""Static contract checks for the mouse-input and savestate timeline fixes.

These checks intentionally complement the runtime savestate tests: they pin the
ordering and ownership boundaries that cannot be exercised on every build host.
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"{label}: missing {needle!r}")


def function_body(text: str, signature: str, next_signature: str) -> str:
    start = text.index(signature)
    end = text.index(next_signature, start + len(signature))
    return text[start:end]


def main() -> None:
    raw_state = source("src/frontend/qt_sdl/MelonPrimeRawInputState.cpp")
    raw_filter = source("src/frontend/qt_sdl/MelonPrimeRawInputWinFilter.cpp")
    raw_filter_h = source("src/frontend/qt_sdl/MelonPrimeRawInputWinFilter.h")
    game_input = source("src/frontend/qt_sdl/MelonPrimeGameInput.cpp")
    lifecycle = source("src/frontend/qt_sdl/MelonPrimeLifecycle.cpp")
    patch_lifecycle = source("src/frontend/qt_sdl/MelonPrimePatchLifecycle.cpp")
    patch_lifecycle_h = source("src/frontend/qt_sdl/MelonPrimePatchLifecycle.h")
    core = source("src/frontend/qt_sdl/MelonPrime.cpp")
    emu_thread = source("src/frontend/qt_sdl/EmuThread.cpp")

    # Raw mouse, side-button and wheel events share InputState's snapshot.
    for needle in (
        "RI_MOUSE_WHEEL",
        "m_accumWheelSteps",
        "m_accumWheelSteps.exchange(0, std::memory_order_acq_rel)",
        "DefRawInputProc(&pri",
        "outHk.wheelDelta = outWheelSteps",
    ):
        require(raw_state, needle, "Raw Input state")

    require(raw_filter_h, "int& outWheelSteps", "Raw Input wheel snapshot API")
    for needle in (
        "BeginRegistrationGeneration",
        "baselineReady = false",
        "subscription->baselineReady = true",
        "syncPhysicalState()",
        "Raw mouse delta, button edge history and wheel impulses",
        "registration generation together",
    ):
        require(raw_filter, needle, "Raw Input registration transaction")

    reconfigure = function_body(
        raw_filter,
        "void RawInputWinFilter::ReconfigureActiveRegistration(",
        "void RawInputWinFilter::DeactivateActiveRegistration(",
    )
    for needle in (
        "drainPendingMessages()",
        "subscription->baselineReady = false",
        "BeginRegistrationGeneration",
        "UnregisterDevices()",
        "ApplyOwnerRegistration(subscription)",
        "subscription->state->discardDeltas()",
        "subscription->state->resetAll()",
        "subscription->state->syncPhysicalState()",
        "subscription->baselineReady = true",
    ):
        require(reconfigure, needle, "registration reconfigure order")
    if not (
        reconfigure.index("drainPendingMessages()")
        < reconfigure.index("subscription->baselineReady = false")
        < reconfigure.index("BeginRegistrationGeneration")
        < reconfigure.index("UnregisterDevices()")
    ):
        raise AssertionError("registration boundary order is not drain -> not-ready -> generation -> unregister")

    for setter in (
        "setJoy2KeySupport",
        "setRawInputTarget",
        "setQtFilterRequested",
    ):
        body = function_body(raw_filter, f"void RawInputWinFilter::{setter}(", "void RawInputWinFilter::")
        require(body, "ReconfigureActiveRegistration(subscription, false)", f"{setter} route")

    require(game_input, "SetInputGenerationFromEmu", "input generation publication")
    require(game_input, "hk.baselineReady", "input readiness gate")
    require(game_input, "hk.generation == m_inputSubscription.generation", "input generation gate")
    require(game_input, "ConsumeWheelForEmu(\n                    m_inputSubscription.generation)", "generation-tagged wheel mailbox")
    require(game_input, "never OR Raw and Qt wheel pulses", "exclusive wheel source")

    thread_bridge = source("src/frontend/qt_sdl/MelonPrimeThreadBridge.h")
    require(thread_bridge, "m_wheelMailbox.compare_exchange_weak", "wheel mailbox atomic publication")
    require(thread_bridge, "m_inputGeneration.load(std::memory_order_acquire)\n                    == generationValue", "wheel generation race retry")

    callback = function_body(
        lifecycle,
        "void MelonPrimeCore::OnSavestateLoaded()",
        "    // Called by EmuThread on every video-settings",
    )
    for needle in (
        "m_timelineGeneration",
        "m_postSavestateReconcilePending = true",
        "BIT_IN_GAME_INIT",
        "BIT_BATTLE_RUNTIME_MODE",
        "BIT_END_OF_GAME_PATCH_RESTORED",
        "TR_AimResiduals",
        "TR_WeaponSwitchPending",
        "ResetMorphBoostSwipePulseState()",
        "PatchLifecycle::ReconcileAfterSavestateLoad",
        "CustomHud_ReconcilePatchAfterSavestateLoad",
    ):
        require(callback, needle, "savestate callback")
    if "NDS::RunFrame" in callback or "loadState" in callback:
        raise AssertionError("savestate callback must not advance or reload the guest timeline")

    if not (
        core.index("m_postSavestateReconcilePending = false")
        < core.index("const bool isInGame", core.index("m_postSavestateReconcilePending = false"))
        < core.index("HandleGameJoinInit()")
    ):
        raise AssertionError("savestate marker is not consumed before loaded lifecycle reconstruction")

    reconcile = function_body(
        patch_lifecycle,
        "void ReconcileAfterSavestateLoad(",
        "void ApplyOutOfGameFrame(",
    )
    for needle in (
        "ARM9Hook_Uninstall",
        "Patches_ResetAll(core->PatchState())",
        "ARM9Hook_ResetPatchState()",
        "LowLatencyAim uses NDS::SetARM9InstructionHook",
        "active address set and JIT",
    ):
        require(reconcile, needle, "savestate patch reconciliation")
    if "Patches_Restore" in reconcile or "NDS::RunFrame" in reconcile:
        raise AssertionError("savestate reconciliation must not restore guest RAM or run a frame")
    require(patch_lifecycle_h, "ReconcileAfterSavestateLoad", "savestate lifecycle API")

    # The production load path remains load -> callback, with no callback-side
    # synthetic frame. Keep this check narrow so diagnostic paths can evolve.
    if not re.search(r"if\s*\(msgResult\)\s*melonPrime->OnSavestateLoaded\(\);", emu_thread):
        raise AssertionError("successful load callback: missing load-success callback")

    print("mouse input + savestate contract: PASS")


if __name__ == "__main__":
    main()

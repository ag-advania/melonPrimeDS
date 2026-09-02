/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <QKeyEvent>
#include <SDL2/SDL.h>
#include <cassert>
#include <cstdlib>

#include "Platform.h"
#include "SDL_gamecontroller.h"
#include "SDL_sensor.h"
#include "main.h"
#include "Config.h"
#include "MelonPrimeQtKeyBinding.h"

#ifdef MELONPRIME_DS
#include "MelonPrimeCompilerHints.h"
#include "MelonPrimeDef.h"
#include "MelonPrimeMouseButton.h"
#include "MelonPrimePerfProbe.h"
#endif

using namespace melonDS;

const char* EmuInstance::buttonNames[12] =
{
    "A",
    "B",
    "Select",
    "Start",
    "Right",
    "Left",
    "Up",
    "Down",
    "R",
    "L",
    "X",
    "Y"
};

const char* EmuInstance::hotkeyNames[HK_MAX] =
{
    "HK_Lid",
    "HK_Mic",
    "HK_Pause",
    "HK_Reset",
    "HK_FastForward",
    "HK_FrameLimitToggle",
    "HK_FullscreenToggle",
    "HK_SwapScreens",
    "HK_SwapScreenEmphasis",
    "HK_SolarSensorDecrease",
    "HK_SolarSensorIncrease",
    "HK_FrameStep",
    "HK_PowerButton",
    "HK_VolumeUp",
    "HK_VolumeDown",
    "HK_AudioMuteToggle",
    "HK_SlowMo",
    "HK_FastForwardToggle",
    "HK_SlowMoToggle",
    "HK_GuitarGripGreen",
    "HK_GuitarGripRed",
    "HK_GuitarGripYellow",
    "HK_GuitarGripBlue",

#ifdef MELONPRIME_DS
    // Metroid Prime specific hotkeys
    "HK_MetroidMoveForward",
    "HK_MetroidMoveBack",
    "HK_MetroidMoveLeft",
    "HK_MetroidMoveRight",
    "HK_MetroidJump",
    "HK_MetroidMorphBall",
    "HK_MetroidZoom",
    "HK_MetroidHoldMorphBallBoost",
    "HK_MetroidScanVisor",
    "HK_MetroidUILeft",
    "HK_MetroidUIRight",
    "HK_MetroidUIOk",
    "HK_MetroidUIYes",
    "HK_MetroidUINo",
    "HK_MetroidShootScan",
    "HK_MetroidScanShoot",
    "HK_MetroidWeaponBeam",
    "HK_MetroidWeaponMissile",
    "HK_MetroidWeaponSpecial",
    "HK_MetroidWeaponNext",
    "HK_MetroidWeaponPrevious",
    "HK_MetroidWeapon1",
    "HK_MetroidWeapon2",
    "HK_MetroidWeapon3",
    "HK_MetroidWeapon4",
    "HK_MetroidWeapon5",
    "HK_MetroidWeapon6",
    "HK_MetroidWeaponCheck",
    "HK_MetroidMenu",
    "HK_MetroidIngameSensiUp",
    "HK_MetroidIngameSensiDown",
    "HK_MetroidWeaponNextSecondary",
    "HK_MetroidWeaponPreviousSecondary",
    "HK_MetroidScanShootStylus",
    "HK_MetroidStylusTouch",
#endif
};

#ifdef MELONPRIME_DS
namespace {
int TrackedMouseButtonIndex(Qt::MouseButton button) noexcept
{
    return MelonPrime::MouseButtonIndex(button);
}
}
#endif


void EmuInstance::inputInit()
{
#ifdef MELONPRIME_DS
    // The device component owns a distinct lifetime mutex per EmuInstance.
    // SDL's process-wide update/enumeration serialization is kept inside the
    // component and never covers another device's source reads.
    joyMutex = joystickDevice.Mutex();
#else
    SDL_mutex* mutex = SDL_CreateMutex();
    joyMutex = std::shared_ptr<SDL_mutex>(mutex, SDL_DestroyMutex);
#endif
#ifdef MELONPRIME_DS
    static_assert(HK_MAX <= 64, "HK_MAX exceeds uint64_t capacity");
    static_assert(kMaxJoystickCompiledEntries <= 255,
        "compiled joystick source/rule count exceeds uint8_t capacity");

    keyInputMask.store(0xFFF, std::memory_order_relaxed);
    inputMask = 0xFFF;

    keyHotkeyMask.store(0, std::memory_order_relaxed);
    hotkeyMask = 0;
    lastHotkeyMask = 0;
    controllerCommandHotkeyMask = 0;
    controllerCommandSnapshotValid = false;
    controllerCommandNeedsBaseline = true;
    lateJoystick = {};
    previousLateJoystickHotkeyMask = 0;
    lateJoystickNeedsBaseline = true;
    joystickGameplayResetPending.store(false, std::memory_order_relaxed);
    joystickLifecycleCheckCounter = 0;
    pendingJoystickBindingProgram = JoystickBindingProgram{};
    activeJoystickBindingProgram = JoystickBindingProgram{};
    joystickBindingProgramGeneration = 0;
    activeJoystickBindingProgramGeneration = 0;
    qtGlobalCommandPressPending.store(0, std::memory_order_relaxed);
    qtGameplayPressPending.store(0, std::memory_order_relaxed);
    qtWheelLevelPulsePending.store(0, std::memory_order_relaxed);
#else
    keyInputMask = 0xFFF;
    joyInputMask = 0xFFF;
    inputMask = 0xFFF;

    keyHotkeyMask = 0;
    joyHotkeyMask = 0;
    hotkeyMask = 0;
    lastHotkeyMask = 0;
#endif

    isTouching = false;
    touchX = 0;
    touchY = 0;

#ifdef MELONPRIME_DS
    joystickPresent.store(false, std::memory_order_relaxed);
#else
    joystick = nullptr;
    controller = nullptr;
    hasRumble = false;
    hasAccelerometer = false;
    hasGyroscope = false;
    isRumbling = false;
#endif

    inputLoadConfig();
}

void EmuInstance::inputDeInit()
{
    SDL_LockMutex(joyMutex.get());
    closeJoystick();
    SDL_UnlockMutex(joyMutex.get());
}

void EmuInstance::inputLoadConfig()
{
#ifdef MELONPRIME_DS
    // A mapping generation boundary is neutral. Old held levels and pending
    // edges must not be interpreted through the newly loaded bindings.
    keyReleaseAll();
#else
    // Preserve upstream's legacy serialization while it rewrites the live
    // mapping arrays. MelonPrime compiles a private immutable program first
    // and publishes it under the mutex below.
    SDL_LockMutex(joyMutex.get());
#endif

    Config::Table keycfg = localCfg.GetTable("Keyboard");
    Config::Table joycfg = localCfg.GetTable("Joystick");

    for (int i = 0; i < 12; i++)
    {
        keyMapping[i] = keycfg.GetInt(buttonNames[i]);
        joyMapping[i] = joycfg.GetInt(buttonNames[i]);
    }

#ifdef MELONPRIME_DS
    uint64_t wheelUpMask = 0;
    uint64_t wheelDownMask = 0;
#endif
    for (int i = 0; i < HK_MAX; i++)
    {
        hkKeyMapping[i] = keycfg.GetInt(hotkeyNames[i]);
        hkJoyMapping[i] = joycfg.GetInt(hotkeyNames[i]);
#ifdef MELONPRIME_DS
        if (hkKeyMapping[i] == MelonPrime::InputKey::MouseWheelUp)
            wheelUpMask |= 1ULL << i;
        else if (hkKeyMapping[i] == MelonPrime::InputKey::MouseWheelDown)
            wheelDownMask |= 1ULL << i;
#endif
    }
#ifdef MELONPRIME_DS
    wheelUpHotkeyMask.store(wheelUpMask, std::memory_order_release);
    wheelDownHotkeyMask.store(wheelDownMask, std::memory_order_release);
    const JoystickBindingProgram joystickProgram =
        compileJoystickBindingProgram();
    rebuildMouseButtonBindingMasks();
#endif

#ifdef MELONPRIME_DS
    SDL_LockMutex(joyMutex.get());
    publishJoystickBindingProgramLocked(joystickProgram);
#endif
    setJoystickLocked(localCfg.GetInt("JoystickID"));
    SDL_UnlockMutex(joyMutex.get());
}

#ifdef MELONPRIME_DS
EmuInstance::JoystickBindingProgram
EmuInstance::compileJoystickBindingProgram() const
{
    JoystickBindingProgram program{};

    const auto findOrAddSource = [&program](
        JoystickSourceKind kind, uint16_t index) -> uint8_t {
        for (uint8_t i = 0; i < program.sourceCount; ++i) {
            const auto& source = program.sources[i];
            if (source.kind == kind && source.index == index)
                return i;
        }
        assert(program.sourceCount < kMaxJoystickCompiledEntries);
        const uint8_t sourceIndex = program.sourceCount++;
        program.sources[sourceIndex] = { kind, index };
        return sourceIndex;
    };

    const auto addRule = [&program, &findOrAddSource](
        JoystickSourceKind kind, uint16_t index, uint8_t predicate,
        uint16_t inputBits, uint64_t hotkeyBits) {
        const uint8_t sourceIndex = findOrAddSource(kind, index);
        for (uint8_t i = 0; i < program.ruleCount; ++i) {
            auto& rule = program.rules[i];
            if (rule.sourceIndex == sourceIndex
                && rule.predicate == predicate) {
                rule.inputBits |= inputBits;
                rule.hotkeyBits |= hotkeyBits;
                return;
            }
        }
        assert(program.ruleCount < kMaxJoystickCompiledEntries);
        program.rules[program.ruleCount++] = {
            sourceIndex, predicate, inputBits, hotkeyBits
        };
    };

    const auto compileBinding = [&addRule](
        int binding, uint16_t inputBits, uint64_t hotkeyBits) {
        if (binding == -1)
            return;

        if ((binding & 0xFFFF) != 0xFFFF) {
            if (binding & 0x100) {
                addRule(
                    JoystickSourceKind::Hat,
                    static_cast<uint16_t>((binding >> 4) & 0xF),
                    static_cast<uint8_t>(binding & 0xF),
                    inputBits, hotkeyBits);
            }
            else {
                addRule(
                    JoystickSourceKind::Button,
                    static_cast<uint16_t>(binding & 0xFFFF), 0,
                    inputBits, hotkeyBits);
            }
        }
        if (binding & 0x10000) {
            addRule(
                JoystickSourceKind::Axis,
                static_cast<uint16_t>((binding >> 24) & 0xF),
                static_cast<uint8_t>((binding >> 20) & 0xF),
                inputBits, hotkeyBits);
        }
    };

    for (int i = 0; i < 12; ++i)
        compileBinding(joyMapping[i], static_cast<uint16_t>(1u << i), 0);
    for (int i = 0; i < HK_MAX; ++i)
        compileBinding(hkJoyMapping[i], 0, 1ULL << i);

    return program;
}

void EmuInstance::publishJoystickBindingProgramLocked(
    const JoystickBindingProgram& program)
{
    pendingJoystickBindingProgram = program;
    ++joystickBindingProgramGeneration;
}

void EmuInstance::activateJoystickBindingProgramLocked()
{
    const uint32_t generation = joystickBindingProgramGeneration;
    if (generation == activeJoystickBindingProgramGeneration)
        return;

    // The GUI/config writer and this cold copy share joyMutex. Once copied,
    // only EmuThread reads active, so numeric projection stays lock-free and
    // cannot mix physical samples with another binding generation.
    activeJoystickBindingProgram = pendingJoystickBindingProgram;
    activeJoystickBindingProgramGeneration = generation;
}

void EmuInstance::rebuildMouseButtonBindingMasks()
{
    m_mouseRecoveryEligibleMask = 0;
    for (int buttonIndex = 0; buttonIndex < static_cast<int>(
             MelonPrime::kSupportedMouseButtonCount); ++buttonIndex) {
        auto& masks = mouseButtonMasks[buttonIndex];
        masks = {};
        const int key = static_cast<int>(
                MelonPrime::kSupportedMouseButtons[buttonIndex])
            | MelonPrime::InputKey::MouseMark;
        for (int i = 0; i < 12; ++i) {
            if (key == keyMapping[i])
                masks.inputBits |= static_cast<uint16_t>(1u << i);
        }
        for (int i = 0; i < HK_MAX; ++i) {
            if (key != hkKeyMapping[i])
                continue;
            const uint64_t bit = 1ULL << i;
            masks.hotkeyBits |= bit;
            if (bit & kGlobalCommandHotkeyMask)
                masks.globalCommandBits |= bit;
            else if (bit & kGameplayHotkeyMask)
                masks.gameplayBits |= bit;
        }
        if (masks.inputBits || masks.hotkeyBits)
            m_mouseRecoveryEligibleMask |= static_cast<uint8_t>(1u << buttonIndex);
    }
}
#endif

void EmuInstance::inputRumbleStart(melonDS::u32 len_ms)
{
    SDL_LockMutex(joyMutex.get());

#ifdef MELONPRIME_DS
    joystickDevice.RumbleStartLocked(len_ms);
#else
    if (controller && hasRumble && !isRumbling)
    {
        SDL_GameControllerRumble(controller, 0xFFFF, 0xFFFF, len_ms);
        isRumbling = true;
    }
#endif

    SDL_UnlockMutex(joyMutex.get());
}

void EmuInstance::inputRumbleStop()
{
    SDL_LockMutex(joyMutex.get());

#ifdef MELONPRIME_DS
    joystickDevice.RumbleStopLocked();
#else
    if (controller && hasRumble && isRumbling)
    {
        SDL_GameControllerRumble(controller, 0, 0, 0);
        isRumbling = false;
    }
#endif

    SDL_UnlockMutex(joyMutex.get());
}

float EmuInstance::inputMotionQuery(melonDS::Platform::MotionQueryType type)
{
#ifdef MELONPRIME_DS
    float value = 0.0f;
    SDL_LockMutex(joyMutex.get());
    const bool available = joystickDevice.ReadMotionLocked(type, value);
    SDL_UnlockMutex(joyMutex.get());
    if (available)
        return value;
#else
    float values[3];
    SDL_LockMutex(joyMutex.get());
    if (type <= melonDS::Platform::MotionAccelerationZ)
    {
        if (controller && hasAccelerometer)
        {
            if (SDL_GameControllerGetSensorData(controller, SDL_SENSOR_ACCEL, values, 3) == 0)
            {
                // Map values from DS console orientation to SDL controller orientation.
                SDL_UnlockMutex(joyMutex.get());
                switch (type)
                {
                case melonDS::Platform::MotionAccelerationX:
                    return values[0];
                case melonDS::Platform::MotionAccelerationY:
                    return -values[2];
                case melonDS::Platform::MotionAccelerationZ:
                    return values[1];
                default:
                    break;
                }
            }
        }
    }
    else if (type <= melonDS::Platform::MotionRotationZ)
    {
        if (controller && hasGyroscope)
        {
            if (SDL_GameControllerGetSensorData(controller, SDL_SENSOR_GYRO, values, 3) == 0)
            {
                // Map values from DS console orientation to SDL controller orientation.
                SDL_UnlockMutex(joyMutex.get());
                switch (type)
                {
                case melonDS::Platform::MotionRotationX:
                    return values[0];
                case melonDS::Platform::MotionRotationY:
                    return -values[2];
                case melonDS::Platform::MotionRotationZ:
                    return values[1];
                default:
                    break;
                }
            }
        }
    }
    SDL_UnlockMutex(joyMutex.get());
#endif
    if (type == melonDS::Platform::MotionAccelerationZ)
        return SDL_STANDARD_GRAVITY;
    return 0.0f;
}


void EmuInstance::setJoystick(int id)
{
    SDL_LockMutex(joyMutex.get());
    setJoystickLocked(id);
    SDL_UnlockMutex(joyMutex.get());
}

void EmuInstance::setJoystickLocked(int id)
{
    joystickID = id;
    openJoystick();
}

#ifdef MELONPRIME_DS
bool EmuInstance::pollJoystickMapping(
    int oldMapping, const int* axesRest, int& outMapping)
{
    SDL_LockMutex(joyMutex.get());

    if (!joystickDevice.HasJoystickLocked()
        || !joystickDevice.IsAttachedLocked()) {
        SDL_UnlockMutex(joyMutex.get());
        return false;
    }

    bool found = false;

    int nbuttons = joystickDevice.ButtonCountLocked();
    for (int i = 0; i < nbuttons; ++i) {
        int32_t value = 0;
        if (joystickDevice.SampleSourceLocked(
                MelonPrime::JoystickSourceKind::Button,
                static_cast<uint16_t>(i), value) && value) {
            outMapping = (oldMapping & 0xFFFF0000) | i;
            found = true;
            break;
        }
    }

    if (!found) {
        int nhats = joystickDevice.HatCountLocked();
        if (nhats > 16) nhats = 16;
        for (int i = 0; i < nhats; ++i) {
            int32_t hat = 0;
            if (!joystickDevice.SampleSourceLocked(
                    MelonPrime::JoystickSourceKind::Hat,
                    static_cast<uint16_t>(i), hat))
                continue;
            if (!hat)
                continue;

            if (hat & 0x1)      hat = 0x1;
            else if (hat & 0x2) hat = 0x2;
            else if (hat & 0x4) hat = 0x4;
            else                hat = 0x8;

            outMapping = (oldMapping & 0xFFFF0000) | 0x100 | hat | (i << 4);
            found = true;
            break;
        }
    }

    if (!found && axesRest) {
        int naxes = joystickDevice.AxisCountLocked();
        if (naxes > 16) naxes = 16;
        for (int i = 0; i < naxes; ++i) {
            int32_t axisValue = 0;
            if (!joystickDevice.SampleSourceLocked(
                    MelonPrime::JoystickSourceKind::Axis,
                    static_cast<uint16_t>(i), axisValue))
                continue;

            const int diff = std::abs(axisValue - axesRest[i]);
            if (diff < 16384)
                continue;

            if (axesRest[i] < -16384) {
                outMapping = (oldMapping & 0xFFFF)
                    | 0x10000 | (2 << 20) | (i << 24);
            }
            else {
                const int axisType = axisValue > 0 ? 0 : 1;
                outMapping = (oldMapping & 0xFFFF)
                    | 0x10000 | (axisType << 20) | (i << 24);
            }
            found = true;
            break;
        }
    }

    SDL_UnlockMutex(joyMutex.get());
    return true;
}

void EmuInstance::captureJoystickAxisRest(int* axesRest, int count)
{
    if (!axesRest || count <= 0)
        return;

    for (int i = 0; i < count; ++i)
        axesRest[i] = 0;

    SDL_LockMutex(joyMutex.get());
    if (joystickDevice.HasJoystickLocked()
        && joystickDevice.IsAttachedLocked()) {
        int naxes = joystickDevice.AxisCountLocked();
        if (naxes > count) naxes = count;
        if (naxes > 16) naxes = 16;
        for (int i = 0; i < naxes; ++i) {
            int32_t value = 0;
            if (joystickDevice.SampleSourceLocked(
                    MelonPrime::JoystickSourceKind::Axis,
                    static_cast<uint16_t>(i), value))
                axesRest[i] = value;
        }
    }
    SDL_UnlockMutex(joyMutex.get());
}
#endif

void EmuInstance::openJoystick()
{
#ifdef MELONPRIME_DS
    const bool opened = joystickDevice.OpenLocked(joystickID);
    joystickPresent.store(opened, std::memory_order_release);
#else
    closeJoystick();

    int num = SDL_NumJoysticks();
    if (num < 1)
        return;

    if (joystickID >= num)
        joystickID = 0;

    joystick = SDL_JoystickOpen(joystickID);

    if (SDL_IsGameController(joystickID))
    {
        controller = SDL_GameControllerOpen(joystickID);
    }

    if (controller)
    {
        if (SDL_GameControllerHasRumble(controller))
        {
            hasRumble = true;
        }
        if (SDL_GameControllerHasSensor(controller, SDL_SENSOR_ACCEL))
        {
            hasAccelerometer = SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_ACCEL, SDL_TRUE) == 0;
        }
        if (SDL_GameControllerHasSensor(controller, SDL_SENSOR_GYRO))
        {
            hasGyroscope = SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_GYRO, SDL_TRUE) == 0;
        }
    }

#endif
}

void EmuInstance::closeJoystick()
{
#ifdef MELONPRIME_DS
    // Presence is a hint only; publishing false before destruction prevents
    // lock-free readers from scheduling avoidable work on a closing device.
    joystickPresent.store(false, std::memory_order_release);
#else
    if (controller)
    {
        SDL_GameControllerClose(controller);
        controller = nullptr;
    }
    if (joystick)
    {
        SDL_JoystickClose(joystick);
        joystick = nullptr;
    }
    hasRumble = false;
    hasAccelerometer = false;
    hasGyroscope = false;
    isRumbling = false;
#endif

#ifdef MELONPRIME_DS
    joystickDevice.CloseLocked();
    // Device lifetime belongs under joyMutex. Gameplay-derived state belongs
    // exclusively to EmuThread, so GUI/config writers publish only a reset.
    joystickGameplayResetPending.store(true, std::memory_order_release);
#endif
}

#ifdef MELONPRIME_DS
void EmuInstance::resetJoystickConsumerState()
{
    controllerCommandHotkeyMask = 0;
    controllerCommandSnapshotValid = false;
    controllerCommandNeedsBaseline = true;
    lateJoystick.inputMask = 0xFFF;
    lateJoystick.hotkeyHeld = 0;
    lateJoystick.hotkeyPressed = 0;
    previousLateJoystickHotkeyMask = 0;
    lateJoystickNeedsBaseline = true;
    inputMask = keyInputMask.load(std::memory_order_relaxed);
    hotkeyMask = keyHotkeyMask.load(std::memory_order_relaxed);
}

bool EmuInstance::consumeJoystickResetPending()
{
    if (UNLIKELY(joystickGameplayResetPending.load(
            std::memory_order_relaxed))
        && joystickGameplayResetPending.exchange(
            false, std::memory_order_acq_rel)) {
        resetJoystickConsumerState();
        return true;
    }
    return false;
}

void EmuInstance::probeJoystickConnection()
{
    SDL_LockMutex(joyMutex.get());
#ifdef MELONPRIME_DS
    joystickDevice.UpdateLocked();
    if (!joystickDevice.HasJoystickLocked())
        openJoystick();
#else
    SDL_JoystickUpdate();
    if (!joystick && SDL_NumJoysticks() > 0)
        openJoystick();
#endif
    SDL_UnlockMutex(joyMutex.get());
}

bool EmuInstance::sampleJoystickPhysicalLocked(
    JoystickPhysicalSnapshot& snapshot, Uint64* updateTicks)
{
    activateJoystickBindingProgramLocked();
#ifdef MELONPRIME_DS
    if (!joystickDevice.HasJoystickLocked())
        return false;

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    const Uint64 updateStartTick = MelonPrimePerf::ReadTicksIfEnabled();
#endif
    joystickDevice.UpdateLocked();
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    const Uint64 updateEndTick = MelonPrimePerf::ReadTicksIfEnabled();
    if (updateTicks && updateEndTick >= updateStartTick)
        *updateTicks = updateEndTick - updateStartTick;
#endif
    if (UNLIKELY(!joystickDevice.IsAttachedLocked())) {
        closeJoystick();
        return false;
    }

    snapshot.sourceCount = activeJoystickBindingProgram.sourceCount;
    for (uint8_t i = 0; i < snapshot.sourceCount; ++i) {
        const auto& source = activeJoystickBindingProgram.sources[i];
        const bool sampled = joystickDevice.SampleSourceLocked(
            source.kind, source.index, snapshot.sourceValue[i]);
        assert(sampled);
        if (UNLIKELY(!sampled))
            snapshot.sourceValue[i] = 0;
    }
    return true;
#else
    if (!joystick)
        return false;

    SDL_JoystickUpdate();
    if (UNLIKELY(!SDL_JoystickGetAttached(joystick))) {
        closeJoystick();
        return false;
    }

    snapshot.sourceCount = activeJoystickBindingProgram.sourceCount;
    for (uint8_t i = 0; i < snapshot.sourceCount; ++i) {
        const auto& source = activeJoystickBindingProgram.sources[i];
        switch (source.kind) {
        case JoystickSourceKind::Button:
            snapshot.sourceValue[i] =
                SDL_JoystickGetButton(joystick, source.index);
            break;
        case JoystickSourceKind::Hat:
            snapshot.sourceValue[i] =
                SDL_JoystickGetHat(joystick, source.index);
            break;
        case JoystickSourceKind::Axis:
            snapshot.sourceValue[i] =
                SDL_JoystickGetAxis(joystick, source.index);
            break;
        }
    }
    return true;
#endif
}

bool EmuInstance::sampleJoystickPhysical(JoystickPhysicalSnapshot& snapshot)
{
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    const Uint64 lockStartTick = MelonPrimePerf::ReadTicksIfEnabled();
#endif
    SDL_LockMutex(joyMutex.get());
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    const Uint64 lockAcquiredTick = MelonPrimePerf::ReadTicksIfEnabled();
    const Uint64 sampleStartTick = lockAcquiredTick;
#endif
    Uint64 updateTicks = 0;
    const bool sampled = sampleJoystickPhysicalLocked(snapshot, &updateTicks);
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    const Uint64 sampleEndTick = MelonPrimePerf::ReadTicksIfEnabled();
#endif
    SDL_UnlockMutex(joyMutex.get());

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    if (lockAcquiredTick >= lockStartTick)
        MelonPrimePerf::RecordInputMetricTicks(
            MelonPrimePerf::InputMetric::JoystickLockWait,
            lockAcquiredTick - lockStartTick);
    if (sampleEndTick >= sampleStartTick)
        MelonPrimePerf::RecordInputMetricTicks(
            MelonPrimePerf::InputMetric::JoystickSample,
            sampleEndTick - sampleStartTick);
    if (updateTicks)
        MelonPrimePerf::RecordInputMetricTicks(
            MelonPrimePerf::InputMetric::JoystickSDLUpdate,
            updateTicks);
#endif
    return sampled;
}

EmuInstance::JoystickProjectedState
EmuInstance::projectJoystickPhysicalSnapshot(
    const JoystickPhysicalSnapshot& snapshot) const
{
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    MelonPrimePerf::ScopedInputMetric projectMetric(
        MelonPrimePerf::InputMetric::JoystickProject);
#endif
    JoystickProjectedState projected{0xFFF, 0};
    for (uint8_t i = 0; i < activeJoystickBindingProgram.ruleCount; ++i) {
        const auto& rule = activeJoystickBindingProgram.rules[i];
        assert(rule.sourceIndex < snapshot.sourceCount);
        if (UNLIKELY(rule.sourceIndex >= snapshot.sourceCount))
            continue;

        const auto& source =
            activeJoystickBindingProgram.sources[rule.sourceIndex];
        const int32_t value = snapshot.sourceValue[rule.sourceIndex];
        bool down = false;
        switch (source.kind) {
        case JoystickSourceKind::Button:
            down = value != 0;
            break;
        case JoystickSourceKind::Hat:
            down = (value & rule.predicate) != 0;
            break;
        case JoystickSourceKind::Axis:
            if (rule.predicate == 0)
                down = value > 16384;
            else if (rule.predicate == 1)
                down = value < -16384;
            else if (rule.predicate == 2)
                down = value > 0;
            break;
        }
        if (!down)
            continue;
        projected.inputMask &= static_cast<uint16_t>(~rule.inputBits);
        projected.hotkeyMask |= rule.hotkeyBits;
    }
    return projected;
}

void EmuInstance::projectJoystickCommandState(
    const JoystickProjectedState& projected)
{
    controllerCommandHotkeyMask = projected.hotkeyMask;
    controllerCommandSnapshotValid = true;
}

void EmuInstance::projectJoystickGameplayState(
    const JoystickProjectedState& projected, bool commitGameplayEdges)
{
    lateJoystick.inputMask = projected.inputMask;
    lateJoystick.hotkeyHeld = projected.hotkeyMask;
    if (!commitGameplayEdges) {
        // A nested FrameAdvance may refresh physical held state, but it must
        // never consume the outer frame's gameplay edge baseline.
        lateJoystick.hotkeyPressed = 0;
    }
    else if (lateJoystickNeedsBaseline) {
        // Reconnect/config switch: held controls establish a baseline without
        // becoming phantom gameplay presses from the previous device.
        lateJoystick.hotkeyPressed = 0;
        lateJoystickNeedsBaseline = false;
    }
    else {
        lateJoystick.hotkeyPressed =
            projected.hotkeyMask & ~previousLateJoystickHotkeyMask;
    }
    if (commitGameplayEdges)
        previousLateJoystickHotkeyMask = projected.hotkeyMask;

    inputMask = keyInputMask.load(std::memory_order_relaxed)
        & projected.inputMask;
    hotkeyMask = keyHotkeyMask.load(std::memory_order_relaxed)
        | projected.hotkeyMask;
}

void EmuInstance::refreshJoystickCommandState()
{
    if (!joystickPresent.load(std::memory_order_acquire)) {
        controllerCommandHotkeyMask = 0;
        controllerCommandSnapshotValid = false;
        return;
    }

    JoystickPhysicalSnapshot physical;
    if (!sampleJoystickPhysical(physical)) {
        (void)consumeJoystickResetPending();
        controllerCommandHotkeyMask = 0;
        controllerCommandSnapshotValid = false;
        return;
    }
    projectJoystickCommandState(projectJoystickPhysicalSnapshot(physical));
}
#endif


void EmuInstance::onKeyPress(QKeyEvent* event)
{
#ifdef MELONPRIME_DS
    if (event->isAutoRepeat())
        return;
    const int key = NormalizeQtKeyBinding(*event);
    uint16_t pressedInputBits = 0;
    uint64_t pressedHotkeyBits = 0;
    for (int i = 0; i < 12; i++)
        if (key == keyMapping[i])
            pressedInputBits |= static_cast<uint16_t>(1u << i);
    for (int i = 0; i < HK_MAX; i++)
        if (key == hkKeyMapping[i])
            pressedHotkeyBits |= 1ULL << i;
    if (pressedInputBits)
        keyInputMask.fetch_and(
            static_cast<uint16_t>(~pressedInputBits),
            std::memory_order_relaxed);
    if (pressedHotkeyBits) {
        keyHotkeyMask.fetch_or(pressedHotkeyBits, std::memory_order_relaxed);
        const uint64_t globalCommandBits =
            pressedHotkeyBits & kGlobalCommandHotkeyMask;
        if (globalCommandBits)
            qtGlobalCommandPressPending.fetch_or(
                globalCommandBits, std::memory_order_release);
        const uint64_t gameplayBits =
            pressedHotkeyBits & kGameplayHotkeyMask;
        if (gameplayBits)
            qtGameplayPressPending.fetch_or(
                gameplayBits, std::memory_order_release);
    }
#else
    int keyHK = NormalizeQtKeyBinding(*event);
    int keyKP = keyHK;
    if (event->modifiers() != Qt::KeypadModifier)
        keyKP &= ~event->modifiers();

    for (int i = 0; i < 12; i++)
        if (keyKP == keyMapping[i])
            keyInputMask &= ~(1 << i);

    for (int i = 0; i < HK_MAX; i++)
        if (keyHK == hkKeyMapping[i])
            keyHotkeyMask |= (1 << i);
#endif
}

void EmuInstance::onKeyRelease(QKeyEvent* event)
{
#ifdef MELONPRIME_DS
    if (event->isAutoRepeat())
        return;
#endif
    const int normalized = NormalizeQtKeyBinding(*event);
#ifdef MELONPRIME_DS
    const int baseKey = event->key();
    const bool isModifier = IsQtModifierKey(baseKey);
    uint16_t releasedInputBits = 0;
    uint64_t releasedHotkeyBits = 0;
    for (int i = 0; i < 12; i++)
        if (QtKeyBindingMatchesRelease(
                keyMapping[i], normalized, baseKey, isModifier))
            releasedInputBits |= static_cast<uint16_t>(1u << i);

    for (int i = 0; i < HK_MAX; i++)
        if (QtKeyBindingMatchesRelease(
                hkKeyMapping[i], normalized, baseKey, isModifier))
            releasedHotkeyBits |= 1ULL << i;
    if (releasedInputBits)
        keyInputMask.fetch_or(releasedInputBits, std::memory_order_relaxed);
    if (releasedHotkeyBits)
        keyHotkeyMask.fetch_and(~releasedHotkeyBits, std::memory_order_relaxed);
#else
    int keyHK = normalized;
    int keyKP = keyHK;
    if (event->modifiers() != Qt::KeypadModifier)
        keyKP &= ~event->modifiers();

    for (int i = 0; i < 12; i++)
        if (keyKP == keyMapping[i])
            keyInputMask |= (1 << i);

    for (int i = 0; i < HK_MAX; i++)
        if (keyHK == hkKeyMapping[i])
            keyHotkeyMask &= ~(1 << i);
#endif
}

#ifdef MELONPRIME_DS
void EmuInstance::onMousePress(QMouseEvent* event)
{
    const int buttonIndex = TrackedMouseButtonIndex(event->button());
    if (buttonIndex < 0)
        return;
    const auto& masks = mouseButtonMasks[buttonIndex];
    if (masks.inputBits)
        keyInputMask.fetch_and(
            static_cast<uint16_t>(~masks.inputBits),
            std::memory_order_relaxed);
    if (masks.hotkeyBits) {
        keyHotkeyMask.fetch_or(masks.hotkeyBits, std::memory_order_relaxed);
        if (masks.globalCommandBits)
            qtGlobalCommandPressPending.fetch_or(
                masks.globalCommandBits, std::memory_order_release);
    }
    if (masks.gameplayBits) {
        qtGameplayPressPending.fetch_or(
            masks.gameplayBits, std::memory_order_release);
    }
}

void EmuInstance::onMouseRelease(QMouseEvent* event)
{
    const int buttonIndex = TrackedMouseButtonIndex(event->button());
    if (buttonIndex < 0)
        return;
    const auto& masks = mouseButtonMasks[buttonIndex];
    if (masks.inputBits)
        keyInputMask.fetch_or(masks.inputBits, std::memory_order_relaxed);
    if (masks.hotkeyBits)
        keyHotkeyMask.fetch_and(~masks.hotkeyBits, std::memory_order_relaxed);
}

bool EmuInstance::hotkeyUsesKeyboardKey(int hotkeyId, int qtKey) const
{
    return hotkeyId >= 0 && hotkeyId < HK_MAX
        && hkKeyMapping[hotkeyId] == qtKey;
}

bool EmuInstance::hotkeyUsesMouseButton(int hotkeyId, Qt::MouseButton button) const
{
    if (hotkeyId < 0 || hotkeyId >= HK_MAX)
        return false;
    const int key = static_cast<int>(button) | MelonPrime::InputKey::MouseMark;
    return hkKeyMapping[hotkeyId] == key;
}

void EmuInstance::syncMouseHotkeysFromQtButtons(Qt::MouseButtons physical)
{
    uint16_t releasedInputBits = 0;
    uint64_t releasedHotkeyBits = 0;
    for (int buttonIndex = 0; buttonIndex < static_cast<int>(
             MelonPrime::kSupportedMouseButtonCount); ++buttonIndex) {
        if (physical & MelonPrime::kSupportedMouseButtons[buttonIndex])
            continue;
        const auto& masks = mouseButtonMasks[buttonIndex];
        releasedInputBits |= masks.inputBits;
        releasedHotkeyBits |= masks.hotkeyBits;
    }
    const uint16_t currentInput =
        keyInputMask.load(std::memory_order_relaxed);
    const uint16_t staleInput = static_cast<uint16_t>(
        releasedInputBits & static_cast<uint16_t>(~currentInput));
    if (UNLIKELY(staleInput != 0))
        keyInputMask.fetch_or(staleInput, std::memory_order_relaxed);

    const uint64_t currentHotkeys =
        keyHotkeyMask.load(std::memory_order_relaxed);
    const uint64_t staleHotkeys = releasedHotkeyBits & currentHotkeys;
    if (UNLIKELY(staleHotkeys != 0))
        keyHotkeyMask.fetch_and(~staleHotkeys, std::memory_order_relaxed);
}

void EmuInstance::onMouseWheel(int delta)
{
    if (!delta) return;

    const uint64_t pulse = wheelHotkeyMaskForDelta(delta);
    if (!pulse) return;
    const uint64_t globalCommandBits = pulse & kGlobalCommandHotkeyMask;
    if (globalCommandBits)
        qtGlobalCommandPressPending.fetch_or(
            globalCommandBits, std::memory_order_release);
    const uint64_t levelPulseBits = pulse & ~kGameplayHotkeyMask;
    if (levelPulseBits)
        qtWheelLevelPulsePending.fetch_or(
            levelPulseBits, std::memory_order_release);
}
#endif // MELONPRIME_DS

void EmuInstance::keyReleaseAll()
{
#ifdef MELONPRIME_DS
    keyInputMask.store(0xFFF, std::memory_order_relaxed);
    keyHotkeyMask.store(0, std::memory_order_relaxed);
    qtGlobalCommandPressPending.store(0, std::memory_order_relaxed);
    qtGameplayPressPending.store(0, std::memory_order_relaxed);
    qtWheelLevelPulsePending.store(0, std::memory_order_relaxed);
#else
    keyInputMask = 0xFFF;
    keyHotkeyMask = 0;
#endif
}

bool EmuInstance::joystickButtonDown(int val)
{
#ifdef MELONPRIME_DS
    // The MelonPrime path projects the shared physical snapshot instead of
    // reading SDL directly here. This helper remains for the original
    // non-DS polling path below.
    (void)val;
    return false;
#else
    if (val == -1) return false;

    bool hasbtn = ((val & 0xFFFF) != 0xFFFF);

    if (hasbtn)
    {
        if (val & 0x100)
        {
            int hatnum = (val >> 4) & 0xF;
            int hatdir = val & 0xF;
            Uint8 hatval = SDL_JoystickGetHat(joystick, hatnum);

            bool pressed = false;
            if (hatdir == 0x1) pressed = (hatval & SDL_HAT_UP);
            else if (hatdir == 0x4) pressed = (hatval & SDL_HAT_DOWN);
            else if (hatdir == 0x2) pressed = (hatval & SDL_HAT_RIGHT);
            else if (hatdir == 0x8) pressed = (hatval & SDL_HAT_LEFT);

            if (pressed) return true;
        }
        else
        {
            int btnnum = val & 0xFFFF;
            Uint8 btnval = SDL_JoystickGetButton(joystick, btnnum);

            if (btnval) return true;
        }
    }

    if (val & 0x10000)
    {
        int axisnum = (val >> 24) & 0xF;
        int axisdir = (val >> 20) & 0xF;
        Sint16 axisval = SDL_JoystickGetAxis(joystick, axisnum);

        switch (axisdir)
        {
        case 0: // positive
            if (axisval > 16384) return true;
            break;

        case 1: // negative
            if (axisval < -16384) return true;
            break;

        case 2: // trigger
            if (axisval > 0) return true;
            break;
        }
    }

    return false;
#endif
}

void EmuInstance::inputProcess(bool guestFrameWillRun)
{
#ifdef MELONPRIME_DS
    if (!guestFrameWillRun)
        MelonPrimePerf::BeginInputTotal();
    // =========================================================================
    // Controller lifecycle owner and global emulator-edge sample.
    //
    // Running physical state is sampled only at the guest-frame late latch.
    // When no guest frame will run, refresh command state here so controller
    // release/re-press remains live while paused without touching gameplay
    // edge ownership.
    // =========================================================================
    if (guestFrameWillRun)
        lateJoystick.hotkeyPressed = 0;

    (void)consumeJoystickResetPending();

    bool lifecycleCheckDue = false;
    if (UNLIKELY(++joystickLifecycleCheckCounter >= 60)) {
        joystickLifecycleCheckCounter = 0;
        lifecycleCheckDue = true;
    }

    // Active controllers are attachment-checked by the one required physical
    // sample. Probe an absent device at the normal per-instance cadence in
    // either scheduling state; a successful paused probe is sampled below.
    if (UNLIKELY(lifecycleCheckDue)
        && !joystickPresent.load(std::memory_order_acquire)) {
        probeJoystickConnection();
        (void)consumeJoystickResetPending();
    }
    if (!guestFrameWillRun)
        refreshJoystickCommandState();

    // Combined edge detection (keyboard + joystick)
    const uint64_t currentKeyHotkeys =
        keyHotkeyMask.load(std::memory_order_relaxed);
    uint64_t qtWheelLevelPulse = 0;
    if (UNLIKELY(qtWheelLevelPulsePending.load(
            std::memory_order_relaxed) != 0)) {
        qtWheelLevelPulse = qtWheelLevelPulsePending.exchange(
            0, std::memory_order_acq_rel);
    }
    hotkeyMask = currentKeyHotkeys | controllerCommandHotkeyMask
        | qtWheelLevelPulse;
    if (UNLIKELY(controllerCommandNeedsBaseline
            && controllerCommandSnapshotValid)) {
        // A newly connected/configured controller may already be held. Fold
        // only its command bits into the baseline so reconnect cannot synthesize
        // Pause/Reset/fullscreen presses or suppress unrelated Qt edges.
        lastHotkeyMask |= controllerCommandHotkeyMask;
        controllerCommandNeedsBaseline = false;
    }
    uint64_t qtGlobalPressed = 0;
    if (UNLIKELY(qtGlobalCommandPressPending.load(
            std::memory_order_relaxed) != 0)) {
        qtGlobalPressed = qtGlobalCommandPressPending.exchange(
            0, std::memory_order_acq_rel);
    }
    hotkeyPress = (hotkeyMask & ~lastHotkeyMask) | qtGlobalPressed;
    hotkeyRelease = lastHotkeyMask & ~hotkeyMask;
    lastHotkeyMask = hotkeyMask;

    if (!guestFrameWillRun)
        MelonPrimePerf::EndInputTotal();

#else
    // Original melonDS path: full SDL polling + edge detection
    SDL_LockMutex(joyMutex.get());
    SDL_JoystickUpdate();

    if (joystick)
    {
        if (!SDL_JoystickGetAttached(joystick))
        {
            closeJoystick();
        }
    }
    if (!joystick && (SDL_NumJoysticks() > 0))
    {
        openJoystick();
    }

    joyInputMask = 0xFFF;
    if (joystick)
    {
        for (int i = 0; i < 12; i++)
            if (joystickButtonDown(joyMapping[i]))
                joyInputMask &= ~(1 << i);
    }

    inputMask = keyInputMask & joyInputMask;

    joyHotkeyMask = 0;
    if (joystick)
    {
        for (int i = 0; i < HK_MAX; i++)
            if (joystickButtonDown(hkJoyMapping[i]))
                joyHotkeyMask |= (1 << i);
    }

    hotkeyMask = keyHotkeyMask | joyHotkeyMask;
    hotkeyPress = hotkeyMask & ~lastHotkeyMask;
    hotkeyRelease = lastHotkeyMask & ~hotkeyMask;
    lastHotkeyMask = hotkeyMask;
    SDL_UnlockMutex(joyMutex.get());
#endif
}

#ifdef MELONPRIME_DS
// =========================================================================
// inputRefreshJoystickState — guest-frame late controller snapshot
//
// Called after Sleep inside frameAdvanceOnce to give RunFrameHook the
// freshest possible joystick axis/button state.
//
// Global emulator command edges are finalized by inputProcess(). This function
// owns a separate MelonPrime gameplay edge baseline and never rewrites those
// global edges, preventing save/pause/fullscreen commands from re-firing.
// =========================================================================
void EmuInstance::inputRefreshJoystickState(bool commitGameplayEdges)
{
    (void)consumeJoystickResetPending();

    if (!joystickPresent.load(std::memory_order_acquire)) {
        inputMask = keyInputMask.load(std::memory_order_relaxed);
        hotkeyMask = keyHotkeyMask.load(std::memory_order_relaxed);
        return;
    }

    JoystickPhysicalSnapshot physical;
    if (!sampleJoystickPhysical(physical)) {
        (void)consumeJoystickResetPending();
        return;
    }

    // Physical acquisition happens once. Both consumers receive the same
    // projection, and numeric mask assembly remains outside the SDL mutex.
    const JoystickProjectedState projected =
        projectJoystickPhysicalSnapshot(physical);
    projectJoystickCommandState(projected);
    projectJoystickGameplayState(projected, commitGameplayEdges);
}
#endif // MELONPRIME_DS

void EmuInstance::touchScreen(int x, int y)
{
    touchX = x;
    touchY = y;
    isTouching = true;
}

void EmuInstance::releaseScreen()
{
    isTouching = false;
}

#ifdef MELONPRIME_DS
melonDS::u32 EmuInstance::getInputMask() {
    return static_cast<melonDS::u32>(inputMask) & 0xFFF;
}
#endif // MELONPRIME_DS

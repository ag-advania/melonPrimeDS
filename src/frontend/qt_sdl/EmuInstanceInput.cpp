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

#include "Platform.h"
#include "SDL_gamecontroller.h"
#include "SDL_sensor.h"
#include "main.h"
#include "Config.h"

#ifdef MELONPRIME_DS
#include "MelonPrimeCompilerHints.h"
#include "MelonPrimeDef.h"
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

std::shared_ptr<SDL_mutex> EmuInstance::joyMutexGlobal = nullptr;

#ifdef MELONPRIME_DS
namespace {
constexpr Qt::MouseButton kTrackedMouseButtons[5] = {
    Qt::LeftButton,
    Qt::RightButton,
    Qt::MiddleButton,
    Qt::XButton1,
    Qt::XButton2,
};

int TrackedMouseButtonIndex(Qt::MouseButton button) noexcept
{
    switch (button) {
    case Qt::LeftButton:   return 0;
    case Qt::RightButton:  return 1;
    case Qt::MiddleButton: return 2;
    case Qt::XButton1:     return 3;
    case Qt::XButton2:     return 4;
    default:               return -1;
    }
}
}
#endif


void EmuInstance::inputInit()
{
    if (!joyMutexGlobal)
    {
        SDL_mutex* mutex = SDL_CreateMutex();
        joyMutexGlobal = std::shared_ptr<SDL_mutex>(mutex, SDL_DestroyMutex);
    }
    joyMutex = joyMutexGlobal;

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
    joystickPhysicalSourceCount = 0;
    joystickFanoutRuleCount = 0;
    qtGameplayPressPending.store(0, std::memory_order_relaxed);
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

    joystick = nullptr;
    controller = nullptr;
    hasRumble = false;
    hasAccelerometer = false;
    hasGyroscope = false;
    isRumbling = false;

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
    SDL_LockMutex(joyMutex.get());

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
    wheelUpHotkeyMask = wheelUpMask;
    wheelDownHotkeyMask = wheelDownMask;
    rebuildActiveJoystickBindings();
    rebuildMouseButtonBindingMasks();
#endif

    setJoystickLocked(localCfg.GetInt("JoystickID"));
    SDL_UnlockMutex(joyMutex.get());
}

#ifdef MELONPRIME_DS
void EmuInstance::rebuildActiveJoystickBindings()
{
    joystickPhysicalSourceCount = 0;
    joystickFanoutRuleCount = 0;

    const auto findOrAddSource = [this](
        JoystickSourceKind kind, uint16_t index) -> uint8_t {
        for (uint8_t i = 0; i < joystickPhysicalSourceCount; ++i) {
            const auto& source = joystickPhysicalSources[i];
            if (source.kind == kind && source.index == index)
                return i;
        }
        const uint8_t sourceIndex = joystickPhysicalSourceCount++;
        joystickPhysicalSources[sourceIndex] = { kind, index };
        return sourceIndex;
    };

    const auto addRule = [this, &findOrAddSource](
        JoystickSourceKind kind, uint16_t index, uint8_t predicate,
        uint16_t inputBits, uint64_t hotkeyBits) {
        const uint8_t sourceIndex = findOrAddSource(kind, index);
        for (uint8_t i = 0; i < joystickFanoutRuleCount; ++i) {
            auto& rule = joystickFanoutRules[i];
            if (rule.sourceIndex == sourceIndex
                && rule.predicate == predicate) {
                rule.inputBits |= inputBits;
                rule.hotkeyBits |= hotkeyBits;
                return;
            }
        }
        joystickFanoutRules[joystickFanoutRuleCount++] = {
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
}

void EmuInstance::rebuildMouseButtonBindingMasks()
{
    for (int buttonIndex = 0; buttonIndex < 5; ++buttonIndex) {
        auto& masks = mouseButtonMasks[buttonIndex];
        masks = {};
        const int key = static_cast<int>(kTrackedMouseButtons[buttonIndex])
            | MelonPrime::InputKey::MouseMark;
        for (int i = 0; i < 12; ++i) {
            // Preserve the established MelonPrime mouse-to-DS projection.
            if (key == hkKeyMapping[i])
                masks.inputBits |= static_cast<uint16_t>(1u << i);
        }
        for (int i = 0; i < HK_MAX; ++i) {
            if (key == hkKeyMapping[i])
                masks.hotkeyBits |= 1ULL << i;
        }
    }
}
#endif

void EmuInstance::inputRumbleStart(melonDS::u32 len_ms)
{
    SDL_LockMutex(joyMutex.get());

    if (controller && hasRumble && !isRumbling)
    {
        SDL_GameControllerRumble(controller, 0xFFFF, 0xFFFF, len_ms);
        isRumbling = true;
    }

    SDL_UnlockMutex(joyMutex.get());
}

void EmuInstance::inputRumbleStop()
{
    SDL_LockMutex(joyMutex.get());

    if (controller && hasRumble && isRumbling)
    {
        SDL_GameControllerRumble(controller, 0, 0, 0);
        isRumbling = false;
    }

    SDL_UnlockMutex(joyMutex.get());
}

float EmuInstance::inputMotionQuery(melonDS::Platform::MotionQueryType type)
{
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

void EmuInstance::openJoystick()
{
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

}

void EmuInstance::closeJoystick()
{
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

#ifdef MELONPRIME_DS
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
    SDL_JoystickUpdate();
    if (!joystick && SDL_NumJoysticks() > 0)
        openJoystick();
    SDL_UnlockMutex(joyMutex.get());
}

bool EmuInstance::sampleJoystickPhysicalLocked(
    JoystickPhysicalSnapshot& snapshot)
{
    if (!joystick)
        return false;

    SDL_JoystickUpdate();
    if (UNLIKELY(!SDL_JoystickGetAttached(joystick))) {
        closeJoystick();
        return false;
    }

    snapshot.sourceCount = joystickPhysicalSourceCount;
    for (uint8_t i = 0; i < snapshot.sourceCount; ++i) {
        const auto& source = joystickPhysicalSources[i];
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
}

bool EmuInstance::sampleJoystickPhysical(JoystickPhysicalSnapshot& snapshot)
{
    SDL_LockMutex(joyMutex.get());
    const bool sampled = sampleJoystickPhysicalLocked(snapshot);
    SDL_UnlockMutex(joyMutex.get());
    return sampled;
}

EmuInstance::JoystickProjectedState
EmuInstance::projectJoystickPhysicalSnapshot(
    const JoystickPhysicalSnapshot& snapshot) const
{
    JoystickProjectedState projected{0xFFF, 0};
    for (uint8_t i = 0; i < joystickFanoutRuleCount; ++i) {
        const auto& rule = joystickFanoutRules[i];
        assert(rule.sourceIndex < snapshot.sourceCount);
        if (UNLIKELY(rule.sourceIndex >= snapshot.sourceCount))
            continue;

        const auto& source = joystickPhysicalSources[rule.sourceIndex];
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
    if (!joystick) {
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


// distinguish between left and right modifier keys (Ctrl, Alt, Shift)
// Qt provides no real cross-platform way to do this, so here we go
// for Windows and Linux we can distinguish via scancodes (but both
// provide different scancodes)
bool isRightModKey(QKeyEvent* event)
{
#ifdef __WIN32__
    quint32 scan = event->nativeScanCode();
    return (scan == 0x11D || scan == 0x138 || scan == 0x36);
#elif __APPLE__
    quint32 scan = event->nativeVirtualKey();
    return (scan == 0x36 || scan == 0x3C || scan == 0x3D || scan == 0x3E);
#else
    quint32 scan = event->nativeScanCode();
    return (scan == 0x69 || scan == 0x6C || scan == 0x3E);
#endif
}

int getEventKeyVal(QKeyEvent* event)
{
    int key = event->key();
    int mod = event->modifiers();
    bool ismod = (key == Qt::Key_Control ||
        key == Qt::Key_Alt ||
        key == Qt::Key_AltGr ||
        key == Qt::Key_Shift ||
        key == Qt::Key_Meta);

    if (!ismod)
        key |= mod;
    else if (isRightModKey(event))
        key |= (1 << 31);

    return key;
}


void EmuInstance::onKeyPress(QKeyEvent* event)
{
#ifdef MELONPRIME_DS
    if (event->isAutoRepeat())
        return;
    int key = event->key();
    uint64_t pressedHotkeyBits = 0;
    for (int i = 0; i < 12; i++)
        if (key == hkKeyMapping[i])
            keyInputMask.fetch_and(static_cast<uint16_t>(~(1u << i)), std::memory_order_relaxed);
    for (int i = 0; i < HK_MAX; i++)
        if (key == hkKeyMapping[i])
            pressedHotkeyBits |= 1ULL << i;
    if (pressedHotkeyBits) {
        keyHotkeyMask.fetch_or(pressedHotkeyBits, std::memory_order_relaxed);
        qtGameplayPressPending.fetch_or(
            pressedHotkeyBits, std::memory_order_release);
    }
#else
    int keyHK = getEventKeyVal(event);
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
    int key = event->key();

    for (int i = 0; i < 12; i++)
        if (key == hkKeyMapping[i])
            keyInputMask.fetch_or(static_cast<uint16_t>(1u << i), std::memory_order_relaxed);

    for (int i = 0; i < HK_MAX; i++)
        if (key == hkKeyMapping[i])
            keyHotkeyMask.fetch_and(~(1ULL << i), std::memory_order_relaxed);
#else
    int keyHK = getEventKeyVal(event);
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
        qtGameplayPressPending.fetch_or(
            masks.hotkeyBits, std::memory_order_release);
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
    for (int buttonIndex = 0; buttonIndex < 5; ++buttonIndex) {
        if (physical & kTrackedMouseButtons[buttonIndex])
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
    keyHotkeyMask.fetch_or(pulse, std::memory_order_relaxed);
    wheelHotkeyPulseMask.fetch_or(pulse, std::memory_order_relaxed);
}
#endif // MELONPRIME_DS

void EmuInstance::keyReleaseAll()
{
#ifdef MELONPRIME_DS
    keyInputMask.store(0xFFF, std::memory_order_relaxed);
    keyHotkeyMask.store(0, std::memory_order_relaxed);
    qtGameplayPressPending.store(0, std::memory_order_relaxed);
    wheelHotkeyPulseMask.store(0, std::memory_order_relaxed);
#else
    keyInputMask = 0xFFF;
    keyHotkeyMask = 0;
#endif
}

bool EmuInstance::joystickButtonDown(int val)
{
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
}

void EmuInstance::inputProcess(bool guestFrameWillRun)
{
#ifdef MELONPRIME_DS
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
    if (!joystick && lifecycleCheckDue) {
        probeJoystickConnection();
        (void)consumeJoystickResetPending();
    }
    if (!guestFrameWillRun)
        refreshJoystickCommandState();

    // Combined edge detection (keyboard + joystick)
    const uint64_t currentKeyHotkeys =
        keyHotkeyMask.load(std::memory_order_relaxed);
    hotkeyMask = currentKeyHotkeys | controllerCommandHotkeyMask;
    if (UNLIKELY(controllerCommandNeedsBaseline
            && controllerCommandSnapshotValid)) {
        // A newly connected/configured controller may already be held. Fold
        // only its command bits into the baseline so reconnect cannot synthesize
        // Pause/Reset/fullscreen presses or suppress unrelated Qt edges.
        lastHotkeyMask |= controllerCommandHotkeyMask;
        controllerCommandNeedsBaseline = false;
    }
    hotkeyPress = hotkeyMask & ~lastHotkeyMask;
    hotkeyRelease = lastHotkeyMask & ~hotkeyMask;
    lastHotkeyMask = hotkeyMask;

    // Mouse-wheel bindings are impulses: release the virtual key after the
    // edge latch so the next frame sees a clean release.
    uint64_t wheelPulseMask = 0;
    if (UNLIKELY(wheelHotkeyPulseMask.load(std::memory_order_relaxed) != 0))
        wheelPulseMask =
            wheelHotkeyPulseMask.exchange(0, std::memory_order_relaxed);
    if (wheelPulseMask) {
        keyHotkeyMask.fetch_and(~wheelPulseMask, std::memory_order_relaxed);
    }

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

    if (!joystick) {
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
float EmuInstance::hotkeyAnalogueValue(int id) {
    int val = hkJoyMapping[id];
    if (val == -1) return 0;

    if (val & 0x10000)
    {
        int axisnum = (val >> 24) & 0xF;
        // int axisdir = (val >> 20) & 0xF;
        Sint16 axisval = SDL_JoystickGetAxis(joystick, axisnum);
        return (float)axisval / INT16_MAX;
    }

    return 0;
}

melonDS::u32 EmuInstance::getInputMask() {
    return static_cast<melonDS::u32>(inputMask) & 0xFFF;
}
#endif // MELONPRIME_DS

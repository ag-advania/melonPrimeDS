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
    static_assert(HK_MAX + 12 <= 255,
        "active joystick binding count exceeds uint8_t capacity");

    keyInputMask.store(0xFFF, std::memory_order_relaxed);
    inputMask = 0xFFF;

    keyHotkeyMask.store(0, std::memory_order_relaxed);
    hotkeyMask = 0;
    lastHotkeyMask = 0;
    lateJoystick = {};
    previousLateJoystickHotkeyMask = 0;
    lateJoystickNeedsBaseline = true;
    joystickGameplayResetPending.store(false, std::memory_order_relaxed);
    joystickLifecycleCheckCounter = 0;
    activeJoystickBindingCount = 0;
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

    setJoystick(localCfg.GetInt("JoystickID"));
    SDL_UnlockMutex(joyMutex.get());
}

#ifdef MELONPRIME_DS
void EmuInstance::rebuildActiveJoystickBindings()
{
    activeJoystickBindingCount = 0;

    const auto addBinding = [this](
        int binding, uint16_t inputBits, uint64_t hotkeyBits) {
        if (binding == -1)
            return;

        // Cold config/rebind path: merge duplicate physical bindings once so
        // the late poll samples each active SDL control at most once.
        for (uint8_t i = 0; i < activeJoystickBindingCount; ++i) {
            auto& entry = activeJoystickBindings[i];
            if (entry.binding == binding) {
                entry.inputBits |= inputBits;
                entry.hotkeyBits |= hotkeyBits;
                return;
            }
        }

        auto& entry = activeJoystickBindings[activeJoystickBindingCount++];
        entry.binding = binding;
        entry.inputBits = inputBits;
        entry.hotkeyBits = hotkeyBits;
    };

    for (int i = 0; i < 12; ++i)
        addBinding(joyMapping[i], static_cast<uint16_t>(1u << i), 0);
    for (int i = 0; i < HK_MAX; ++i)
        addBinding(hkJoyMapping[i], 0, 1ULL << i);
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
    joystickID = id;
    openJoystick();
    SDL_UnlockMutex(joyMutex.get());
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
void EmuInstance::resetLateJoystickGameplayState()
{
    lateJoystick.inputMask = 0xFFF;
    lateJoystick.hotkeyHeld = 0;
    lateJoystick.hotkeyPressed = 0;
    previousLateJoystickHotkeyMask = 0;
    lateJoystickNeedsBaseline = true;
    inputMask = keyInputMask.load(std::memory_order_relaxed);
    hotkeyMask = keyHotkeyMask.load(std::memory_order_relaxed);
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
    int key = event->key();
    for (int i = 0; i < 12; i++)
        if (key == hkKeyMapping[i])
            keyInputMask.fetch_and(static_cast<uint16_t>(~(1u << i)), std::memory_order_relaxed);
    for (int i = 0; i < HK_MAX; i++)
        if (key == hkKeyMapping[i])
            keyHotkeyMask.fetch_or(1ULL << i, std::memory_order_relaxed);
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
    int key = static_cast<int>(event->button()) | MelonPrime::InputKey::MouseMark;

    for (int i = 0; i < 12; i++)
        if (key == hkKeyMapping[i])
            keyInputMask.fetch_and(static_cast<uint16_t>(~(1u << i)), std::memory_order_relaxed);

    for (int i = 0; i < HK_MAX; i++)
        if (key == hkKeyMapping[i])
            keyHotkeyMask.fetch_or(1ULL << i, std::memory_order_relaxed);
}

void EmuInstance::onMouseRelease(QMouseEvent* event)
{
    int key = static_cast<int>(event->button()) | MelonPrime::InputKey::MouseMark;

    for (int i = 0; i < 12; i++)
        if (key == hkKeyMapping[i])
            keyInputMask.fetch_or(static_cast<uint16_t>(1u << i), std::memory_order_relaxed);

    for (int i = 0; i < HK_MAX; i++)
        if (key == hkKeyMapping[i])
            keyHotkeyMask.fetch_and(~(1ULL << i), std::memory_order_relaxed);
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
    if (releasedInputBits)
        keyInputMask.fetch_or(releasedInputBits, std::memory_order_relaxed);
    if (releasedHotkeyBits)
        keyHotkeyMask.fetch_and(~releasedHotkeyBits, std::memory_order_relaxed);
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

void EmuInstance::inputProcess()
{
#ifdef MELONPRIME_DS
    // =========================================================================
    // Controller lifecycle owner and global emulator-edge sample.
    //
    // Physical state is sampled at the guest-frame late latch. This early path
    // only performs a per-instance, throttled attach/detach check so paused or
    // otherwise non-advancing instances still converge without a second SDL
    // update on every running frame.
    // =========================================================================
    lateJoystick.hotkeyPressed = 0;
    if (UNLIKELY(++joystickLifecycleCheckCounter >= 60)) {
        joystickLifecycleCheckCounter = 0;
        SDL_LockMutex(joyMutex.get());
        SDL_JoystickUpdate();
        if (joystick && !SDL_JoystickGetAttached(joystick))
            closeJoystick();
        else if (!joystick && SDL_NumJoysticks() > 0)
            openJoystick();
        SDL_UnlockMutex(joyMutex.get());
    }

    if (UNLIKELY(joystickGameplayResetPending.load(
            std::memory_order_relaxed))
        && joystickGameplayResetPending.exchange(
            false, std::memory_order_acq_rel)) {
        resetLateJoystickGameplayState();
    }

    // Combined edge detection (keyboard + joystick)
    const uint64_t currentKeyHotkeys =
        keyHotkeyMask.load(std::memory_order_relaxed);
    hotkeyMask = currentKeyHotkeys | lateJoystick.hotkeyHeld;
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
    if (UNLIKELY(joystickGameplayResetPending.load(
            std::memory_order_relaxed))
        && joystickGameplayResetPending.exchange(
            false, std::memory_order_acq_rel)) {
        resetLateJoystickGameplayState();
    }

    if (!joystick) {
        inputMask = keyInputMask.load(std::memory_order_relaxed);
        hotkeyMask = keyHotkeyMask.load(std::memory_order_relaxed);
        return;
    }

    SDL_LockMutex(joyMutex.get());
    SDL_JoystickUpdate();

    // A device can disappear between the early lifecycle check and this late
    // sample. Route that edge through the same close/reset owner.
    if (UNLIKELY(!SDL_JoystickGetAttached(joystick)))
    {
        closeJoystick();
        SDL_UnlockMutex(joyMutex.get());
        (void)joystickGameplayResetPending.exchange(
            false, std::memory_order_acq_rel);
        resetLateJoystickGameplayState();
        return;
    }

    bool bindingDown[HK_MAX + 12];
    for (uint8_t i = 0; i < activeJoystickBindingCount; ++i) {
        bindingDown[i] = joystickButtonDown(
            activeJoystickBindings[i].binding);
    }

    SDL_UnlockMutex(joyMutex.get());

    // SDL handle lifetime is protected above. Numeric mask assembly is local
    // work and stays outside the process-global joystick lock.
    uint16_t nextInputMask = 0xFFF;
    uint64_t nextHotkeyMask = 0;
    for (uint8_t i = 0; i < activeJoystickBindingCount; ++i) {
        if (bindingDown[i]) {
            const auto& entry = activeJoystickBindings[i];
            nextInputMask &= static_cast<uint16_t>(~entry.inputBits);
            nextHotkeyMask |= entry.hotkeyBits;
        }
    }

    lateJoystick.inputMask = nextInputMask;
    lateJoystick.hotkeyHeld = nextHotkeyMask;
    if (!commitGameplayEdges) {
        // A nested FrameAdvance may refresh physical held state, but it must
        // never consume the outer frame's gameplay edge baseline.
        lateJoystick.hotkeyPressed = 0;
    }
    else if (lateJoystickNeedsBaseline) {
        // Reconnect/config switch: held controls establish a baseline without
        // becoming phantom presses from the previous physical device.
        lateJoystick.hotkeyPressed = 0;
        lateJoystickNeedsBaseline = false;
    }
    else {
        lateJoystick.hotkeyPressed =
            nextHotkeyMask & ~previousLateJoystickHotkeyMask;
    }
    if (commitGameplayEdges)
        previousLateJoystickHotkeyMask = nextHotkeyMask;

    inputMask = keyInputMask.load(std::memory_order_relaxed) & nextInputMask;
    hotkeyMask =
        keyHotkeyMask.load(std::memory_order_relaxed) | nextHotkeyMask;
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

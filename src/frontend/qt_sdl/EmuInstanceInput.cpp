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
#endif
};

std::shared_ptr<SDL_mutex> EmuInstance::joyMutexGlobal = nullptr;


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

    keyInputMask = 0xFFF;
    joyInputMask = 0xFFF;
    inputMask = 0xFFF;

    keyHotkeyMask = 0;
    joyHotkeyMask = 0;
    hotkeyMask = 0;
    lastHotkeyMask = 0;

    joyHotkeyPress = 0;
    joyHotkeyRelease = 0;
    lastJoyHotkeyMask = 0;
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

    for (int i = 0; i < HK_MAX; i++)
    {
        hkKeyMapping[i] = keycfg.GetInt(hotkeyNames[i]);
        hkJoyMapping[i] = joycfg.GetInt(hotkeyNames[i]);
    }

    setJoystick(localCfg.GetInt("JoystickID"));
    SDL_UnlockMutex(joyMutex.get());
}

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
    if (controller) SDL_GameControllerClose(controller);

    if (joystick) SDL_JoystickClose(joystick);

    int num = SDL_NumJoysticks();
    if (num < 1)
    {
        controller = nullptr;
        joystick = nullptr;
        hasRumble = false;
        hasAccelerometer = false;
        hasGyroscope = false;
        return;
    }

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
        hasRumble = false;
        hasAccelerometer = false;
        hasGyroscope = false;
    }
    if (joystick)
    {
        SDL_JoystickClose(joystick);
        joystick = nullptr;
    }
}


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
            keyInputMask &= ~(1u << i);
    for (int i = 0; i < HK_MAX; i++)
        if (key == hkKeyMapping[i])
            keyHotkeyMask |= (1ULL << i);
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
            keyInputMask |= (1u << i);

    for (int i = 0; i < HK_MAX; i++)
        if (key == hkKeyMapping[i])
            keyHotkeyMask &= ~(1ULL << i);
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
    int key = static_cast<int>(event->button()) | 0xF0000000;

    for (int i = 0; i < 12; i++)
        if (key == hkKeyMapping[i])
            keyInputMask &= ~(1u << i);

    for (int i = 0; i < HK_MAX; i++)
        if (key == hkKeyMapping[i])
            keyHotkeyMask |= (1ULL << i);
}

void EmuInstance::onMouseRelease(QMouseEvent* event)
{
    int key = static_cast<int>(event->button()) | 0xF0000000;

    for (int i = 0; i < 12; i++)
        if (key == hkKeyMapping[i])
            keyInputMask |= (1u << i);

    for (int i = 0; i < HK_MAX; i++)
        if (key == hkKeyMapping[i])
            keyHotkeyMask &= ~(1ULL << i);
}
#endif // MELONPRIME_DS

void EmuInstance::keyReleaseAll()
{
#ifdef MELONPRIME_DS
    keyInputMask = 0xFFF;
    keyHotkeyMask = 0;
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
    // P-21: Edge-detection-only path.
    //
    // SDL_JoystickUpdate is handled solely by inputRefreshJoystickState()
    // (called after Sleep in the frame lambda). Joystick masks (joyInputMask,
    // joyHotkeyMask) are already set from that call.
    //
    // This removes per-frame:
    //   - SDL_LockMutex + SDL_UnlockMutex      (2 syscalls)
    //   - SDL_JoystickUpdate → PeekMessage      (1 syscall + message dispatch)
    //   - SDL_JoystickGetAttached               (SDL call)
    //   - SDL_NumJoysticks                      (SDL call)
    //   - 12× joystickButtonDown (joyInputMask) (loop)
    //   - HK_MAX× joystickButtonDown (hotkey)   (loop)
    //
    // UI hotkeys (Pause, Reset, etc.) see joystick button presses from the
    // previous frame's inputRefreshJoystickState. 1-frame delay (~16ms) is
    // imperceptible for non-gameplay actions.
    //
    // keyHotkeyMask (keyboard) is updated asynchronously by Qt events, so
    // hotkeyMask must be recomputed here to catch keyboard-driven edges.
    // =========================================================================

    // Joystick edge detection (masks set by inputRefreshJoystickState)
    joyHotkeyPress = joyHotkeyMask & ~lastJoyHotkeyMask;
    joyHotkeyRelease = lastJoyHotkeyMask & ~joyHotkeyMask;
    lastJoyHotkeyMask = joyHotkeyMask;

    // Combined edge detection (keyboard + joystick)
    hotkeyMask = keyHotkeyMask | joyHotkeyMask;
    hotkeyPress = hotkeyMask & ~lastHotkeyMask;
    hotkeyRelease = lastHotkeyMask & ~hotkeyMask;
    lastHotkeyMask = hotkeyMask;

#else
    // Original melonDS path: full SDL polling + edge detection
    SDL_LockMutex(joyMutex.get());
    SDL_JoystickUpdate();

    if (joystick)
    {
        if (!SDL_JoystickGetAttached(joystick))
        {
            SDL_JoystickClose(joystick);
            joystick = nullptr;
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
// P-15: inputRefreshJoystickState — lightweight joystick re-poll
//
// Called after Sleep inside frameAdvanceOnce to give RunFrameHook the
// freshest possible joystick axis/button state.
//
// P-23: No-joystick fast path.
// When joystick == nullptr (common for KB+M MelonPrime players),
// skip SDL_LockMutex + SDL_JoystickUpdate + SDL_UnlockMutex entirely.
// Saves 2 mutex syscalls + SDL internal overhead per frame.
//
// The 60-frame attachment check for newly connected controllers still
// runs via inputProcess() in the outer loop, but the hot path inside
// frameAdvanceOnce avoids all SDL overhead when no controller exists.
// =========================================================================
void EmuInstance::inputRefreshJoystickState()
{
    // P-23: Skip SDL overhead when no joystick connected.
    // joyInputMask stays 0xFFF (all released), joyHotkeyMask stays 0.
    // hotkeyMask still includes keyHotkeyMask from Qt events.
    if (!joystick) {
        // P-23: Throttled attachment check — detect newly connected controllers.
        // SDL_NumJoysticks requires the SDL event pump, so we must at least
        // call SDL_JoystickUpdate periodically to process connect/disconnect.
        static uint8_t s_joyCheckCounter = 0;
        if (UNLIKELY(++s_joyCheckCounter >= 60)) {
            s_joyCheckCounter = 0;
            SDL_LockMutex(joyMutex.get());
            SDL_JoystickUpdate();
            if (SDL_NumJoysticks() > 0) {
                openJoystick();
                // If a joystick was found, immediately refresh masks
                if (joystick) {
                    joyInputMask = 0xFFF;
                    for (int i = 0; i < 12; i++)
                        if (joystickButtonDown(joyMapping[i]))
                            joyInputMask &= ~(1u << i);
                    inputMask = keyInputMask & joyInputMask;

                    joyHotkeyMask = 0;
                    for (int i = 0; i < HK_MAX; i++)
                        if (joystickButtonDown(hkJoyMapping[i]))
                            joyHotkeyMask |= (1ULL << i);
                    hotkeyMask = keyHotkeyMask | joyHotkeyMask;
                }
            }
            SDL_UnlockMutex(joyMutex.get());
        }
        return;
    }

    SDL_LockMutex(joyMutex.get());
    SDL_JoystickUpdate();

    // P-23: Throttle joystick detachment check to ~once per second.
    static uint8_t s_detachCheckCounter = 0;
    if (UNLIKELY(++s_detachCheckCounter >= 60))
    {
        s_detachCheckCounter = 0;
        if (!SDL_JoystickGetAttached(joystick))
        {
            SDL_JoystickClose(joystick);
            joystick = nullptr;
            joyInputMask = 0xFFF;
            joyHotkeyMask = 0;
            inputMask = keyInputMask & joyInputMask;
            hotkeyMask = keyHotkeyMask | joyHotkeyMask;
            SDL_UnlockMutex(joyMutex.get());
            return;
        }
    }

    joyInputMask = 0xFFF;
    for (int i = 0; i < 12; i++)
        if (joystickButtonDown(joyMapping[i]))
            joyInputMask &= ~(1u << i);

    inputMask = keyInputMask & joyInputMask;

    joyHotkeyMask = 0;
    for (int i = 0; i < HK_MAX; i++)
        if (joystickButtonDown(hkJoyMapping[i]))
            joyHotkeyMask |= (1ULL << i);

    // Refresh combined mask but do NOT recompute edges.
    hotkeyMask = keyHotkeyMask | joyHotkeyMask;

    SDL_UnlockMutex(joyMutex.get());
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
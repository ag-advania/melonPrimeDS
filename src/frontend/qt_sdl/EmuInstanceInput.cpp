/*
    Copyright 2016-2024 melonDS team

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

#include "main.h"
#include "Config.h"

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
    "HK_SlowMo",
    "HK_FastForwardToggle",
    "HK_SlowMoToggle",

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
    // "HK_MetroidWeaponCheck",
    "HK_MetroidMenu",
    "HK_MetroidIngameSensiUp",
    "HK_MetroidIngameSensiDown",

};


void EmuInstance::inputInit()
{
    /* MelonPrimeDS comment-out
    keyInputMask = 0xFFF;
    joyInputMask = 0xFFF;
    inputMask = 0xFFF;

    keyHotkeyMask = 0;
    joyHotkeyMask = 0;
    hotkeyMask = 0;
    lastHotkeyMask = 0;
    */

    /* MelonPrimeDS { */
    keyInputMask.fill(true, 12);
    joyInputMask.fill(true, 12);
    inputMask.fill(true, 12);

    keyHotkeyMask.fill(false, HK_MAX);
    joyHotkeyMask.fill(false, HK_MAX);
    hotkeyMask.fill(false, HK_MAX);
    lastHotkeyMask.fill(false, HK_MAX);
    /* MelonPrimeDS {{} */


    isTouching = false;
    touchX = 0;
    touchY = 0;

    joystick = nullptr;
    controller = nullptr;
    hasRumble = false;
    isRumbling = false;
    inputLoadConfig();
}

void EmuInstance::inputDeInit()
{
    closeJoystick();
}

void EmuInstance::inputLoadConfig()
{
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
}

void EmuInstance::inputRumbleStart(melonDS::u32 len_ms)
{
    if (controller && hasRumble && !isRumbling)
    {
	SDL_GameControllerRumble(controller, 0xFFFF, 0xFFFF, len_ms);
	isRumbling = true;
    }
}

void EmuInstance::inputRumbleStop()
{
    if (controller && hasRumble && isRumbling)
    {
	SDL_GameControllerRumble(controller, 0, 0, 0);
	isRumbling = false;
    }
}


void EmuInstance::setJoystick(int id)
{
    joystickID = id;
    openJoystick();
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
    }
}

void EmuInstance::closeJoystick()
{
    if (controller)
    {
	SDL_GameControllerClose(controller);
	controller = nullptr;
	hasRumble = false;
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
        key |= (1<<31);

    return key;
}


void EmuInstance::onKeyPress(QKeyEvent* event)
{
    /* MelonPrimeDS comment-out 
    int keyHK = getEventKeyVal(event);
    int keyKP = keyHK;
    if (event->modifiers() != Qt::KeypadModifier)
        keyKP &= ~event->modifiers();

    for (int i = 0; i < 12; i++)
        if (keyKP == keyMapping[i])
            keyInputMask &= ~(1<<i);

    for (int i = 0; i < HK_MAX; i++)
        if (keyHK == hkKeyMapping[i])
            keyHotkeyMask |= (1<<i);
    */

    int key = event->key();
    for (int i = 0; i < 12; i++)
        if (key == hkKeyMapping[i])
            keyInputMask.setBit(i, false);
    for (int i = 0; i < HK_MAX; i++)
        if (key == hkKeyMapping[i])
            keyHotkeyMask.setBit(i, true);
}

void EmuInstance::onKeyRelease(QKeyEvent* event)
{
    /* MelonPrimeDS comment-out
    int keyHK = getEventKeyVal(event);
    int keyKP = keyHK;
    if (event->modifiers() != Qt::KeypadModifier)
        keyKP &= ~event->modifiers();

    for (int i = 0; i < 12; i++)
        if (keyKP == keyMapping[i])
            keyInputMask |= (1<<i);

    for (int i = 0; i < HK_MAX; i++)
        if (keyHK == hkKeyMapping[i])
            keyHotkeyMask &= ~(1<<i);
    */

    int key = event->key();

    for (int i = 0; i < 12; i++)
        if (key == hkKeyMapping[i])
            keyInputMask.setBit(i, true);

    for (int i = 0; i < HK_MAX; i++)
        if (key == hkKeyMapping[i])
            keyHotkeyMask.setBit(i, false);
}

// MelonPrimeDS
void EmuInstance::onMousePress(QMouseEvent* event)
{
    int key = static_cast<int>(event->button()) | 0xF0000000;

    for (int i = 0; i < 12; i++)
        if (key == hkKeyMapping[i])
            keyInputMask.setBit(i, false);

    for (int i = 0; i < HK_MAX; i++)
        if (key == hkKeyMapping[i])
            keyHotkeyMask.setBit(i, true);
}

// MelonPrimeDS
void EmuInstance::onMouseRelease(QMouseEvent* event)
{
    int key = static_cast<int>(event->button()) | 0xF0000000;

    for (int i = 0; i < 12; i++)
        if (key == hkKeyMapping[i])
            keyInputMask.setBit(i, true);

    for (int i = 0; i < HK_MAX; i++)
        if (key == hkKeyMapping[i])
            keyHotkeyMask.setBit(i, false);
}

void EmuInstance::keyReleaseAll()
{
    /* MelonPrimeDS comment-out
    keyInputMask = 0xFFF;
    keyHotkeyMask = 0;
    */

    // MelonPrimeDS
    keyInputMask.fill(true, 12);
    keyHotkeyMask.fill(false, HK_MAX);
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
            if      (hatdir == 0x1) pressed = (hatval & SDL_HAT_UP);
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

    // joyInputMask = 0xFFF; // MelonPrimeDS comment-out
    joyInputMask.fill(true, 12); // MelonPrimeDS
    if (joystick)
    {
        for (int i = 0; i < 12; i++)
            if (joystickButtonDown(joyMapping[i]))
                // joyInputMask &= ~(1 << i); // MelonPrimeDS comment-out
                joyInputMask.setBit(i, false); // MelonPrimeDS
    }

    inputMask = keyInputMask & joyInputMask;

    // joyHotkeyMask = 0; // MelonPrimeDS comment-out
    joyHotkeyMask.fill(false, HK_MAX); // MelonPrimeDS
    if (joystick)
    {
        for (int i = 0; i < HK_MAX; i++)
            if (joystickButtonDown(hkJoyMapping[i]))
                // joyHotkeyMask |= (1 << i);// MelonPrimeDS comment-out
                joyHotkeyMask.setBit(i, true); // MelonPrimeDS
    }

    hotkeyMask = keyHotkeyMask | joyHotkeyMask;
    hotkeyPress = hotkeyMask & ~lastHotkeyMask;
    hotkeyRelease = lastHotkeyMask & ~hotkeyMask;
    lastHotkeyMask = hotkeyMask;
}

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


// MelonPrimeDS
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

// MelonPrimeDS
melonDS::u32 EmuInstance::getInputMask() {
/*
    melonDS::u32 mask = 0;
    for (int i = 0; i < 12; i++) {
        if (inputMask.at(i)) mask |= (1 << i);
    }

    return mask;
    */

#ifdef COMMENTOUTTTTTTTT
    // 結果マスク初期化(全ビットOFFの状態で開始)
    melonDS::u32 mask = 0;

    // 各ビットに対して状態を確認して合成(ループ無しで最小分岐)
    mask |= static_cast<melonDS::u32>(inputMask.testBit(0)) << 0;  // Bit 0
    mask |= static_cast<melonDS::u32>(inputMask.testBit(1)) << 1;  // Bit 1
    mask |= static_cast<melonDS::u32>(inputMask.testBit(2)) << 2;  // Bit 2
    mask |= static_cast<melonDS::u32>(inputMask.testBit(3)) << 3;  // Bit 3
    mask |= static_cast<melonDS::u32>(inputMask.testBit(4)) << 4;  // Bit 4
    mask |= static_cast<melonDS::u32>(inputMask.testBit(5)) << 5;  // Bit 5
    mask |= static_cast<melonDS::u32>(inputMask.testBit(6)) << 6;  // Bit 6
    mask |= static_cast<melonDS::u32>(inputMask.testBit(7)) << 7;  // Bit 7
    mask |= static_cast<melonDS::u32>(inputMask.testBit(8)) << 8;  // Bit 8
    mask |= static_cast<melonDS::u32>(inputMask.testBit(9)) << 9;  // Bit 9
    mask |= static_cast<melonDS::u32>(inputMask.testBit(10)) << 10; // Bit 10
    mask |= static_cast<melonDS::u32>(inputMask.testBit(11)) << 11; // Bit 11

    // 完成したマスクを返却
    return mask;
#endif
    return
        (static_cast<melonDS::u32>(inputMask.testBit(0)) << 0) |
        (static_cast<melonDS::u32>(inputMask.testBit(1)) << 1) |
        (static_cast<melonDS::u32>(inputMask.testBit(2)) << 2) |
        (static_cast<melonDS::u32>(inputMask.testBit(3)) << 3) |
        (static_cast<melonDS::u32>(inputMask.testBit(4)) << 4) |
        (static_cast<melonDS::u32>(inputMask.testBit(5)) << 5) |
        (static_cast<melonDS::u32>(inputMask.testBit(6)) << 6) |
        (static_cast<melonDS::u32>(inputMask.testBit(7)) << 7) |
        (static_cast<melonDS::u32>(inputMask.testBit(8)) << 8) |
        (static_cast<melonDS::u32>(inputMask.testBit(9)) << 9) |
        (static_cast<melonDS::u32>(inputMask.testBit(10)) << 10) |
        (static_cast<melonDS::u32>(inputMask.testBit(11)) << 11);
}
#include "MelonPrimeRawInputState.h"
#include "MelonPrimeRawWinInternal.h"

#include <cstring>
#include <iostream>

namespace {

RAWINPUT g_rawInput{};
bool g_readFailure = false;

UINT WINAPI FakeGetRawInputData(
    HRAWINPUT, UINT command, LPVOID data, PUINT size, UINT headerSize)
{
    (void)headerSize;
    if (g_readFailure)
        return UINT(-1);
    if (command != RID_INPUT || !data || !size || *size < sizeof(RAWINPUT))
        return 0;
    std::memcpy(data, &g_rawInput, sizeof(g_rawInput));
    *size = sizeof(g_rawInput);
    return sizeof(g_rawInput);
}

void ResetMouse(LONG x, LONG y, USHORT buttonFlags)
{
    g_rawInput = {};
    g_rawInput.header.dwType = RIM_TYPEMOUSE;
    g_rawInput.header.dwSize = sizeof(RAWINPUT);
    g_rawInput.data.mouse.usFlags = MOUSE_MOVE_RELATIVE;
    g_rawInput.data.mouse.lLastX = x;
    g_rawInput.data.mouse.lLastY = y;
    g_rawInput.data.mouse.usButtonFlags = buttonFlags;
}

void ResetKeyboard(USHORT flags)
{
    g_rawInput = {};
    g_rawInput.header.dwType = RIM_TYPEKEYBOARD;
    g_rawInput.header.dwSize = sizeof(RAWINPUT);
    g_rawInput.data.keyboard.VKey = 'A';
    g_rawInput.data.keyboard.MakeCode = 0x1E;
    g_rawInput.data.keyboard.Flags = flags;
}

void ResetHid()
{
    g_rawInput = {};
    g_rawInput.header.dwType = RIM_TYPEHID;
    g_rawInput.header.dwSize = sizeof(RAWINPUT);
}

bool ExpectHint(
    MelonPrime::InputState& state, const char* label, bool expected)
{
    const bool actual = state.processRawInput(
        reinterpret_cast<HRAWINPUT>(&g_rawInput));
    if (actual == expected)
        return true;
    std::cerr << label << ": expected recovery hint " << expected
              << ", got " << actual << '\n';
    return false;
}

} // namespace

int main()
{
    MelonPrime::InputState::InitializeTables();
    MelonPrime::WinInternal::fnNtUserGetRawInputData = &FakeGetRawInputData;
    MelonPrime::InputState state;

    int xRecoveryRequests = 0;
    ResetMouse(1, 0, 0);
    for (int i = 0; i < 10000; ++i) {
        if (state.processRawInput(reinterpret_cast<HRAWINPUT>(&g_rawInput)))
            ++xRecoveryRequests;
    }
    int x = 0;
    int y = 0;
    state.fetchMouseDelta(x, y);
    if (x != 10000 || y != 0 || xRecoveryRequests != 0) {
        std::cerr << "pure X motion must accumulate 10000 with zero recovery\n";
        return 1;
    }

    int yRecoveryRequests = 0;
    ResetMouse(0, 1, 0);
    for (int i = 0; i < 10000; ++i) {
        if (state.processRawInput(reinterpret_cast<HRAWINPUT>(&g_rawInput)))
            ++yRecoveryRequests;
    }
    state.fetchMouseDelta(x, y);
    if (x != 0 || y != 10000 || yRecoveryRequests != 0) {
        std::cerr << "pure Y motion must accumulate 10000 with zero recovery\n";
        return 1;
    }

    ResetMouse(0, 0, RI_MOUSE_WHEEL);
    g_rawInput.data.mouse.usButtonData = WHEEL_DELTA;
    if (!ExpectHint(state, "wheel-only", false))
        return 1;

    ResetMouse(0, 0, RI_MOUSE_LEFT_BUTTON_DOWN);
    if (!ExpectHint(state, "mouse down", true))
        return 1;
    ResetMouse(0, 0, RI_MOUSE_LEFT_BUTTON_UP);
    if (!ExpectHint(state, "mouse up", true))
        return 1;

    ResetKeyboard(0);
    if (!ExpectHint(state, "keyboard down", true))
        return 1;
    ResetKeyboard(RI_KEY_BREAK);
    if (!ExpectHint(state, "keyboard up", true))
        return 1;

    ResetHid();
    if (!ExpectHint(state, "ignored HID", false))
        return 1;

    g_readFailure = true;
    if (!ExpectHint(state, "GetRawInputData failure", true))
        return 1;

    MelonPrime::WinInternal::fnNtUserGetRawInputData = nullptr;
    std::cout << "raw-recovery-hint-tests: PASS\n";
    return 0;
}

#ifndef MELON_PRIME_WIN_INTERNAL_H
#define MELON_PRIME_WIN_INTERNAL_H

#ifdef _WIN32
#include <windows.h>
#include <atomic>

namespace MelonPrime {

    // =========================================================================
    // NT API function pointer types — for bypassing user32.dll overhead
    // =========================================================================

    using NtUserGetRawInputData_t = UINT(WINAPI*)(
        HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader);

    using NtUserGetRawInputBuffer_t = UINT(WINAPI*)(
        PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader);

    using NtUserPeekMessage_t = BOOL(WINAPI*)(
        LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg, BOOL bProcessSideEffects);

    using NtUserMsgWaitForMultipleObjectsEx_t = DWORD(WINAPI*)(
        DWORD nCount, const HANDLE* pHandles, DWORD dwMilliseconds, DWORD dwWakeMask, DWORD dwFlags);

    // P-11: NT timer resolution — 0.5ms floor (below timeBeginPeriod's 1.0ms limit)
    using NtSetTimerResolution_t = LONG(__stdcall*)(
        ULONG DesiredResolution, BOOLEAN SetResolution, ULONG* CurrentResolution);

    // =========================================================================
    // WinInternal — low-level Windows API access with lazy resolution
    // =========================================================================
    class WinInternal {
    public:
        static void ResolveNtApis() noexcept;

        [[nodiscard]] static bool IsResolved() noexcept {
            return s_resolved.load(std::memory_order_acquire);
        }

        static NtUserGetRawInputData_t              fnNtUserGetRawInputData;
        static NtUserGetRawInputBuffer_t             fnNtUserGetRawInputBuffer;
        static NtUserPeekMessage_t                   fnNtUserPeekMessage;
        static NtUserMsgWaitForMultipleObjectsEx_t   fnNtUserMsgWaitForMultipleObjectsEx;
        static NtSetTimerResolution_t                fnNtSetTimerResolution;

        // P-11: Set 0.5ms timer resolution for precision frame pacing.
        // Falls back to timeBeginPeriod(1) if NtSetTimerResolution unavailable.
        static void SetHighTimerResolution() noexcept;

    private:
        static std::atomic<bool> s_resolved;
    };

} // namespace MelonPrime
#endif // _WIN32
#endif // MELON_PRIME_WIN_INTERNAL_H

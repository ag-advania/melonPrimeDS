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

    private:
        static std::atomic<bool> s_resolved;
    };

} // namespace MelonPrime
#endif // _WIN32
#endif // MELON_PRIME_WIN_INTERNAL_H

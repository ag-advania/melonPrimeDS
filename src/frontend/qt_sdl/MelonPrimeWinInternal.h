#ifndef MELON_PRIME_WIN_INTERNAL_H
#define MELON_PRIME_WIN_INTERNAL_H

#ifdef _WIN32
#include <windows.h>
#include <atomic>

// ============================================================================
// NT API function pointer types
// ============================================================================

using NtUserGetRawInputData_t = UINT(WINAPI*)(
    HRAWINPUT hRawInput,
    UINT uiCommand,
    LPVOID pData,
    PUINT pcbSize,
    UINT cbSizeHeader
    );

using NtUserGetRawInputBuffer_t = UINT(WINAPI*)(
    PRAWINPUT pData,
    PUINT pcbSize,
    UINT cbSizeHeader
    );

using NtUserPeekMessage_t = BOOL(WINAPI*)(
    LPMSG lpMsg,
    HWND hWnd,
    UINT wMsgFilterMin,
    UINT wMsgFilterMax,
    UINT wRemoveMsg,
    BOOL bProcessSideEffects
    );

// 追加: NtUserMsgWaitForMultipleObjectsEx の型定義
using NtUserMsgWaitForMultipleObjectsEx_t = DWORD(WINAPI*)(
    DWORD nCount,
    const HANDLE* pHandles,
    DWORD dwMilliseconds,
    DWORD dwWakeMask,
    DWORD dwFlags
    );

// ============================================================================
// WinInternal - Low-level Windows API access
// ============================================================================

class WinInternal {
public:
    /**
     * @brief Resolve undocumented NT APIs for better performance.
     * Thread-safe, idempotent. Call early in application startup.
     */
    static void ResolveNtApis() noexcept;

    /**
     * @brief Check if NT APIs were successfully resolved.
     */
    [[nodiscard]] static bool IsResolved() noexcept {
        return s_resolved.load(std::memory_order_acquire);
    }

    // Function pointers - nullptr if not available
    static NtUserGetRawInputData_t   fnNtUserGetRawInputData;
    static NtUserGetRawInputBuffer_t fnNtUserGetRawInputBuffer;
    static NtUserPeekMessage_t       fnNtUserPeekMessage;
    // 追加: 待機関数のポインタ
    static NtUserMsgWaitForMultipleObjectsEx_t fnNtUserMsgWaitForMultipleObjectsEx;

private:
    static std::atomic<bool> s_resolved;
};

#endif // _WIN32
#endif // MELON_PRIME_WIN_INTERNAL_H
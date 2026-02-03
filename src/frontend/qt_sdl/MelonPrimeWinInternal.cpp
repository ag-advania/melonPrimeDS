#ifdef _WIN32
#include "MelonPrimeWinInternal.h"

// ============================================================================
// Static member definitions
// ============================================================================

NtUserGetRawInputData_t   WinInternal::fnNtUserGetRawInputData = nullptr;
NtUserGetRawInputBuffer_t WinInternal::fnNtUserGetRawInputBuffer = nullptr;
NtUserPeekMessage_t       WinInternal::fnNtUserPeekMessage = nullptr;
std::atomic<bool>         WinInternal::s_resolved{ false };

// ============================================================================
// WinInternal implementation
// ============================================================================

void WinInternal::ResolveNtApis() noexcept {
    // Fast path: already resolved
    if (s_resolved.load(std::memory_order_acquire)) {
        return;
    }

    // Try win32u.dll first (Windows 10+), then fall back to user32.dll
    HMODULE hModule = LoadLibraryW(L"win32u.dll");
    if (!hModule) {
        hModule = LoadLibraryW(L"user32.dll");
    }

    if (hModule) {
        fnNtUserGetRawInputData = reinterpret_cast<NtUserGetRawInputData_t>(
            GetProcAddress(hModule, "NtUserGetRawInputData"));

        fnNtUserGetRawInputBuffer = reinterpret_cast<NtUserGetRawInputBuffer_t>(
            GetProcAddress(hModule, "NtUserGetRawInputBuffer"));

        fnNtUserPeekMessage = reinterpret_cast<NtUserPeekMessage_t>(
            GetProcAddress(hModule, "NtUserPeekMessage"));

        // Note: We intentionally don't FreeLibrary here because we need
        // the function pointers to remain valid for the application lifetime
    }

    s_resolved.store(true, std::memory_order_release);
}

#endif // _WIN32
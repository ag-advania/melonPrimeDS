#ifdef _WIN32
#include "MelonPrimeRawWinInternal.h"

namespace MelonPrime {

    NtUserGetRawInputData_t              WinInternal::fnNtUserGetRawInputData = nullptr;
    NtUserGetRawInputBuffer_t            WinInternal::fnNtUserGetRawInputBuffer = nullptr;
    NtUserPeekMessage_t                  WinInternal::fnNtUserPeekMessage = nullptr;
    NtUserMsgWaitForMultipleObjectsEx_t  WinInternal::fnNtUserMsgWaitForMultipleObjectsEx = nullptr;
    NtSetTimerResolution_t               WinInternal::fnNtSetTimerResolution = nullptr;
    std::atomic<bool>                    WinInternal::s_resolved{ false };

    void WinInternal::ResolveNtApis() noexcept {
        if (s_resolved.load(std::memory_order_acquire)) return;

        HMODULE hModule = LoadLibraryW(L"win32u.dll");
        if (!hModule) hModule = LoadLibraryW(L"user32.dll");

        if (hModule) {
            const auto get = [&](const char* name) { return GetProcAddress(hModule, name); };
            fnNtUserGetRawInputData             = reinterpret_cast<NtUserGetRawInputData_t>(get("NtUserGetRawInputData"));
            fnNtUserGetRawInputBuffer           = reinterpret_cast<NtUserGetRawInputBuffer_t>(get("NtUserGetRawInputBuffer"));
            fnNtUserPeekMessage                 = reinterpret_cast<NtUserPeekMessage_t>(get("NtUserPeekMessage"));
            fnNtUserMsgWaitForMultipleObjectsEx = reinterpret_cast<NtUserMsgWaitForMultipleObjectsEx_t>(get("NtUserMsgWaitForMultipleObjectsEx"));
        }

        // P-11: NtSetTimerResolution lives in ntdll.dll (always loaded)
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (hNtdll) {
            fnNtSetTimerResolution = reinterpret_cast<NtSetTimerResolution_t>(
                GetProcAddress(hNtdll, "NtSetTimerResolution"));
        }

        s_resolved.store(true, std::memory_order_release);
    }

    // =========================================================================
    // P-11: SetHighTimerResolution
    //
    // NtSetTimerResolution(5000) → 0.5ms resolution (below timeBeginPeriod's
    // 1.0ms floor). Falls back to timeBeginPeriod(1) on failure.
    //
    // Called once from EmuThread::run() before the frame loop.
    // =========================================================================
    void WinInternal::SetHighTimerResolution() noexcept {
        // Ensure APIs are resolved
        ResolveNtApis();

        if (fnNtSetTimerResolution) {
            ULONG currentRes = 0;
            LONG status = fnNtSetTimerResolution(5000, TRUE, &currentRes); // 0.5ms
            if (status == 0) return; // STATUS_SUCCESS
        }

        // Fallback: timeBeginPeriod(1) → 1.0ms resolution
        HMODULE hWinmm = LoadLibraryW(L"winmm.dll");
        if (hWinmm) {
            using TimeBeginPeriod_t = UINT(WINAPI*)(UINT);
            auto fn = reinterpret_cast<TimeBeginPeriod_t>(
                GetProcAddress(hWinmm, "timeBeginPeriod"));
            if (fn) fn(1);
        }
    }

} // namespace MelonPrime
#endif // _WIN32

#ifdef _WIN32
#include "MelonPrimeRawWinInternal.h"

namespace MelonPrime {

    NtUserGetRawInputData_t              WinInternal::fnNtUserGetRawInputData = nullptr;
    NtUserGetRawInputBuffer_t            WinInternal::fnNtUserGetRawInputBuffer = nullptr;
    NtUserPeekMessage_t                  WinInternal::fnNtUserPeekMessage = nullptr;
    NtUserMsgWaitForMultipleObjectsEx_t  WinInternal::fnNtUserMsgWaitForMultipleObjectsEx = nullptr;
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

        s_resolved.store(true, std::memory_order_release);
    }

} // namespace MelonPrime
#endif // _WIN32

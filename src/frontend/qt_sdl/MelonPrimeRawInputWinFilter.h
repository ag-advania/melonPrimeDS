#pragma once
#ifdef _WIN32

#include <QAbstractNativeEventFilter>
#include <memory>
#include <atomic>
#include <vector>
#include <mutex>
#include <windows.h>

namespace MelonPrime {

    class InputState;
    class RawWorker;

    class RawInputWinFilter : public QAbstractNativeEventFilter
    {
    public:
        // シングルトンパターン（Qtのイベントフィルタはグローバルな性質を持つため）
        static RawInputWinFilter* Acquire(bool joy2KeySupport, HWND mainHwnd);
        static void Release() noexcept;
        static int RefCount() noexcept;

        // 設定変更
        void setJoy2KeySupport(bool enable);
        bool getJoy2KeySupport() const noexcept;
        void setRawInputTarget(HWND hwnd);

        // Qt イベントフィルタ
        bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

        // InputState へのアクセサ（エミュレータコアが使用）
        InputState* getState() const noexcept { return m_state.get(); }

        void fetchMouseDelta(int& outDx, int& outDy) noexcept;
        void discardDeltas() noexcept;
        void resetAllKeys() noexcept;
        void resetMouseButtons() noexcept;
        void resetHotkeyEdges() noexcept;
        void clearAllBindings();
        void setHotkeyVks(int id, const std::vector<UINT>& vks);

        bool hotkeyDown(int id) const noexcept;
        bool hotkeyPressed(int id) noexcept;
        bool hotkeyReleased(int id) noexcept;

    private:
        RawInputWinFilter(bool joy2KeySupport, HWND mainHwnd);
        ~RawInputWinFilter() override;

        void switchMode(bool toJoy2Key);
        void RegisterRawDevices(HWND hwnd) noexcept;
        void UnregisterRawDevices() noexcept;

        static RawInputWinFilter* s_instance;
        static std::atomic<int> s_refCount;
        static std::once_flag s_initFlag;

        std::unique_ptr<InputState> m_state;
        std::unique_ptr<RawWorker> m_worker;

        HWND m_mainHwnd = nullptr;
        std::atomic<bool> m_joy2KeySupport{ false };
    };

} // namespace MelonPrime

#endif // _WIN32
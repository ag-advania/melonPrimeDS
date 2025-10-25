#pragma once
#ifdef _WIN32

#include <QtCore/QAbstractNativeEventFilter>
#include <QtCore/QByteArray>
#include <QtCore/qglobal.h>

#include <windows.h>
#include <hidsdi.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#ifndef FORCE_INLINE
#  if defined(_MSC_VER)
#    define FORCE_INLINE __forceinline
#  else
#    define FORCE_INLINE __attribute__((always_inline)) inline
#  endif
#endif

// Low-latency Raw Input filter (Qt + Win32).
// �ENOLEGACY/NOHOTKEYS�͎g��Ȃ�
// �ECPU�ˑ��̍œK���͎g��Ȃ��i�ėpx86-64�j
// �ELevel3�͈ێ��i���͐�p�X���b�h�{���b�Z�[�W��p�E�B���h�E�{MsgWaitForMultipleObjectsEx�j
// �ELevelA�̂ݓK�p�FWM_INPUT����|���v�^���I�u�[�X�g�������^GetRawInputData����i�Œ�o�b�t�@�j
// �EMouse ��: ��d�o�b�t�@�{fetch_add�i�C�x���g�������f�j
class RawInputWinFilter final : public QAbstractNativeEventFilter
{
public:
    enum : uint32_t {
        kTypeNone = 0,
        kTypeMouse = 1,
        kTypeKeyboard = 2
    };

    struct HotkeyMask {
        uint64_t vkMask[4]{ 0,0,0,0 }; // 256 VK bits
        uint8_t  mouseMask{ 0 };       // 5 mouse bits (L,R,M,X1,X2)
        uint8_t  hasMask{ 0 };
        uint16_t _pad{ 0 };
    };

    // 64B�A���C����false sharing���
    struct alignas(64) InputState {
        std::array<std::atomic<uint64_t>, 4> vkDown{ {0,0,0,0} };
        std::atomic<uint8_t>                mouseButtons{ 0 }; // 5 bits used
    };

    // �}�E�X���΃��̓�d�o�b�t�@
    struct alignas(64) DeltaBuf {
        std::atomic<int32_t> dx{ 0 };
        std::atomic<int32_t> dy{ 0 };
    };

    static constexpr int kMaxHotkeyId = 256;

public:
    RawInputWinFilter();
    ~RawInputWinFilter() override;

    // Qt���t�H�[���o�b�N�o�H�i�X���b�h�^�p���͒ʏ킱���ɗ��Ȃ��j
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    // �����o�^�F�w��hwnd��RawInput��R�Â��i�X���b�h�^�p�Ɣr���j�B�����őS�o�^�̏������ēo�^�B
    bool registerRawInput(HWND hwnd);

    // Mouse ���i�]��I/F�j
    void fetchMouseDelta(int& outDx, int& outDy);
    bool getMouseDelta(int& outDx, int& outDy) { fetchMouseDelta(outDx, outDy); return (outDx | outDy) != 0; }
    void discardDeltas();

    // Resets
    void resetAll();
    void resetAllKeys();
    void resetMouseButtons();
    void resetHotkeyEdges();

    // Hotkey API
    bool hotkeyDown(int hk) const noexcept;
    bool hotkeyPressed(int hk) noexcept;   // edge: 0->1
    bool hotkeyReleased(int hk) noexcept;  // edge: 1->0
    void setHotkeyVks(int hk, const std::vector<UINT>& vks) noexcept;

    // Direct queries
    bool keyDown(UINT vk) const noexcept;
    bool mouseButtonDown(int b) const noexcept; // 0..4 = L,R,M,X1,X2

    // ==== Level3: input thread control ====
    bool startInputThread(bool inputSink = false) noexcept; // true��RIDEV_INPUTSINK
    void stopInputThread() noexcept;
    bool isInputThreadRunning() const noexcept { return m_threadRunning.load(std::memory_order_acquire); }

private:
    // Registered descriptors�i���o�^��registerRawInput()���X���b�h���ōs���j
    RAWINPUTDEVICE m_rid[2]{};

    // State
    InputState m_state{};

    // Mouse ����d�o�b�t�@
    alignas(64) DeltaBuf  m_delta[2]{};
    std::atomic<uint8_t>  m_writeIdx{ 0 };

    // �݊��̂��߂̋��p�b�N�̈�i���g�p�����c�u�j
    alignas(8) std::atomic<uint64_t> m_dxyPack{ 0 };

    // Byte-per-key/button mirrors
    std::array<std::atomic<uint8_t>, 256> m_vkDownCompat{};
    std::array<std::atomic<uint8_t>, 5>   m_mbCompat{};

    // Hotkey edge memory
    std::array<std::atomic<uint64_t>, (kMaxHotkeyId + 63) / 64> m_hkPrev{};

    // Hotkey masks
    std::array<HotkeyMask, kMaxHotkeyId> m_hkMask{};

    // �P��GetRawInputData�p�̌Œ�o�b�t�@�iQt���t�H�[���o�b�N�^�X���b�h�����ʁj
    alignas(8) BYTE m_rawBuf[sizeof(RAWINPUT) + 64]{};

    // ==== Level3 thread fields ====
    std::thread        m_inputThread{};
    std::atomic<bool>  m_threadRunning{ false };
    std::atomic<bool>  m_stopRequested{ false };
    std::atomic<HWND>  m_threadHwnd{ nullptr };

private:
    // Helpers
    FORCE_INLINE void accumMouseDelta(LONG dx, LONG dy) noexcept;
    FORCE_INLINE void setVkBit(UINT vk, bool down) noexcept;
    FORCE_INLINE bool getVkState(UINT vk) const noexcept;
    FORCE_INLINE bool getMouseButton(int b) const noexcept;
    FORCE_INLINE void addVkToMask(HotkeyMask& m, UINT vk) noexcept;

    // Level3 internals
    static LRESULT CALLBACK ThreadWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void inputThreadMain(bool inputSink) noexcept;
    void handleRawInputMessage(LPARAM lParam) noexcept; // Qt/Thread���ʃn���h��
    void clearAllRawInputRegistration() noexcept;
};

#endif // _WIN32

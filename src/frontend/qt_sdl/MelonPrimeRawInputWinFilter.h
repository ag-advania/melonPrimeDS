#pragma once
#ifdef _WIN32

// Qt
#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QtGlobal>

// Win32
#include <windows.h>

// STL
#include <array>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <cstdint>
#include <cstring>

#ifndef FORCE_INLINE
#  if defined(_MSC_VER)
#    define FORCE_INLINE __forceinline
#  else
#    define FORCE_INLINE __attribute__((always_inline)) inline
#  endif
#endif

// �Ɍ���x���F
// �E��p���̓X���b�h�imessage-only HWND�j�� WM_INPUT ��������
// �E�}�E�X���΂͒P��o�b�t�@�Ffetch_add(int32)�~2�A�擾�� exchange(0)�~2�i�ǉ��x���[���j
// �EVK/Mouse �� 256bit�{5bit �̃r�b�g�x�N�^�irelaxed�j
// �EUI���h���C����_�u���o�b�t�@�͓P��
// �EMMCSS �gGames�h �𓮓I�ɓK�p�iavrt.dll �𓮓I���[�h�j

class RawInputWinFilter final : public QAbstractNativeEventFilter
{
public:
    RawInputWinFilter();
    ~RawInputWinFilter() override;

    // Qt���ɂ� WM_INPUT �𗬂��Ȃ��݌v�Ȃ̂ŏ�� false�i�����ł͉������Ȃ��j
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    // ���̓X���b�h����iuseInputSink=true �Ŕ�t�H�[�J�X�����擾�j
    bool startInputThread(bool useInputSink = false);
    void stopInputThread();

    // �}�E�X���΃f���^�擾�i���o�����Ƀ[���N���A�j
    void fetchMouseDelta(int& outDx, int& outDy);
    void discardDeltas();

    // ���݂̃}�E�X�{�^����ԁibit0..4 = L,R,M,X1,X2�j
    uint8_t mouseButtons() const noexcept;

    // VK�����i0..255�j
    bool vkDown(uint32_t vk) const noexcept;

    // �z�b�g�L�[�ݒ�^���
    void setHotkeyVks(int hk, const std::vector<UINT>& vks);
    bool hotkeyDown(int hk) const noexcept;
    bool hotkeyPressed(int hk) noexcept;
    bool hotkeyReleased(int hk) noexcept;

    // ��ԃ��Z�b�g
    void resetMouseButtons();
    void resetAllKeys();
    void resetHotkeyEdges();

private:
    enum MouseIndex : uint8_t { kMB_Left = 0, kMB_Right, kMB_Middle, kMB_X1, kMB_X2, kMB_Max };

    struct alignas(16) HotkeyMask {
        uint64_t vkMask[4];
        uint8_t  mouseMask;
        uint8_t  hasMask;
        uint8_t  _pad[6];
    };

    static constexpr size_t kMaxHotkeyId = 256;

    static constexpr uint8_t kMouseButtonLUT[8] = {
        0xFF,       // 0 (unused)
        kMB_Left,   // 1 VK_LBUTTON
        kMB_Right,  // 2 VK_RBUTTON
        0xFF,       // 3
        kMB_Middle, // 4 VK_MBUTTON
        kMB_X1,     // 5 VK_XBUTTON1
        kMB_X2,     // 6 VK_XBUTTON2
        0xFF        // 7
    };

    // ---- ���L��ԁi�ǂ݁F�G�~���^�����F���̓X���b�h�j ----
    struct alignas(64) StateBits {
        std::array<std::atomic<uint64_t>, 4> vk; // 256bit VK����
        std::atomic<uint8_t> mouse;              // 5bit L,R,M,X1,X2
        StateBits() : vk{ 0,0,0,0 }, mouse(0) {}
    } m_state;

    // ���Έړ��i�P��o�b�t�@�j
    alignas(64) std::atomic<int32_t> m_dx{ 0 };
    alignas(64) std::atomic<int32_t> m_dy{ 0 };

    // �z�b�g�L�[
    alignas(64) std::array<HotkeyMask, kMaxHotkeyId> m_hkMask{};
    std::array<std::atomic<uint64_t>, (kMaxHotkeyId + 63) / 64> m_hkPrev{};

    // ---- ���̓X���b�h���� ----
    std::thread              m_thr;
    std::atomic<bool>        m_thrRunning{ false };
    std::atomic<bool>        m_thrQuit{ false };
    HWND                     m_hwndMsg = nullptr;
    HINSTANCE                m_hinst = nullptr;
    bool                     m_useInputSink = false;

    // RawInput �ꎞ�o�b�t�@�i�\���傫�߁j
    alignas(64) BYTE         m_rawBuf[256] = {};

    // ---- MMCSS�i���I���[�h�j----
    // avrt.dll API �̍ŏ���`�i�w�b�_��ˑ��j
    enum AVRT_PRIORITY { AVRT_PRIORITY_LOW = -1, AVRT_PRIORITY_NORMAL = 0, AVRT_PRIORITY_HIGH = 1, AVRT_PRIORITY_CRITICAL = 2 };
    using PFN_AvSetMmThreadCharacteristicsW = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
    using PFN_AvRevertMmThreadCharacteristics = BOOL(WINAPI*)(HANDLE);
    using PFN_AvSetMmThreadPriority = BOOL(WINAPI*)(HANDLE, AVRT_PRIORITY);

    HMODULE                               m_hAvrt = nullptr;
    PFN_AvSetMmThreadCharacteristicsW     m_pAvSetMmThreadCharacteristicsW = nullptr;
    PFN_AvRevertMmThreadCharacteristics   m_pAvRevertMmThreadCharacteristics = nullptr;
    PFN_AvSetMmThreadPriority             m_pAvSetMmThreadPriority = nullptr;
    HANDLE                                m_mmcssHandle = nullptr;
    DWORD                                 m_mmcssTaskIndex = 0;

    // ---- �������� ----
    static LRESULT CALLBACK  WndProcThunk(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT                  WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);

    void                     inputThreadMain();
    bool                     registerRawInput(HWND target);
    void                     applyThreadQoS();
    void                     handleRawInput(const RAWINPUT& raw) noexcept;
    void                     handleRawMouse(const RAWMOUSE& m) noexcept;
    void                     handleRawKeyboard(const RAWKEYBOARD& k) noexcept;
    static void              addVkToMask(HotkeyMask& m, UINT vk) noexcept;

    // ���[�e�B���e�B
    FORCE_INLINE bool        getVkState(uint32_t vk) const noexcept;

    // ���S�ǂ݃��b�p
    FORCE_INLINE bool        readRawInputToBuf(LPARAM lp, const RAWINPUT*& out) noexcept;

    // avrt.dll ���[�h
    void                     loadAvrt();
};

#endif // _WIN32

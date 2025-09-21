// MelonPrimeDirectInputFilter.h
#pragma once
#ifdef _WIN32
#include <Windows.h>
#include <dinput.h>
#include <array>
#include <unordered_map>
#include <atomic>
#include <cstdint>
#include <cstring>

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p) do { if (p) { (p)->Release(); (p)=nullptr; } } while(0)
#endif

class MelonPrimeDirectInputFilter {
public:
    // XInput �Ɠ��������ڂ̎����œ���
    enum class Axis : uint8_t { LXPos, LXNeg, LYPos, LYNeg, RXPos, RXNeg, RYPos, RYNeg, LT, RT };

    struct Binding {
        enum Type : uint8_t { None, Button, Analog, POV } type = None;
        union {
            uint8_t buttonIndex;   // 0..127
            Axis    axis;          // Analog
            uint8_t povDir;        // 0:Up 1:Right 2:Down 3:Left
        } u{};
        float threshold = 0.5f;    // Analog �p(0..1)
    };

public:
    MelonPrimeDirectInputFilter() = default;
    ~MelonPrimeDirectInputFilter();

    // �������ŏ��̐ڑ��BQt�Ȃ� panel->winId() �� HWND �ɂ��ēn��
    bool init(HWND hwnd);
    void shutdown();

    // ���t��1��i�ŏ��R�X�g�j
    void update() noexcept;

    // �o�C���hAPI�iXInput �Ɠ����V�O�l�`���j
    void bindButton(int hk, uint8_t diButtonIndex);
    void bindAxisThreshold(int hk, Axis axis, float threshold);
    void bindPOVDirection(int hk, uint8_t dir0123); // 0:U 1:R 2:D 3:L
    void clearBinding(int hk);

    // �擾�iXInput�Ɠ����j
    bool  hotkeyDown(int hk) const noexcept;
    bool  hotkeyPressed(int hk) noexcept;
    bool  hotkeyReleased(int hk) noexcept;
    float axisValue(Axis a) const noexcept;

    // �f�b�h�]�[���ݒ�i0..10000%�j�Ǝ��͈́i-32768..32767�j�Œ�
    void setDeadzone(short left = 7849, short right = 8689, short trigPct = 300) noexcept {
        m_dzLeft = left; m_dzRight = right; m_dzTriggerPct = trigPct;
    }

    bool connected() const noexcept { return m_connected.load(std::memory_order_acquire); }

private:
    // ����
    static BOOL CALLBACK enumCb(const DIDEVICEINSTANCE* inst, VOID* ctx);
    bool openDevice(const GUID& guid);
    void closeDevice();
    void ensureRanges(); // ���ׂĂ̎��� -32768..32767 �ɑ�����
    static float normStick(LONG v, short dz) noexcept;     // [-32768..32767]��[-1..1]�i�f�b�h�]�[���K�p�j
    static float normTrig(LONG v, short pctDZ) noexcept;   // [0..32767]��[0..1]�i%DZ�j

    bool evalBinding(const Binding& b) const noexcept;

private:
    // COM
    LPDIRECTINPUT8         m_di = nullptr;
    LPDIRECTINPUTDEVICE8   m_dev = nullptr;
    HWND                   m_hwnd = nullptr;

    // ���
    DIJOYSTATE2 m_now{};   // ���t�� update �ōX�V
    DIJOYSTATE2 m_prev{};  // �G�b�W���o�p
    std::atomic<bool> m_connected{ false };

    // �ݒ�
    short m_dzLeft = 7849, m_dzRight = 8689; // �X�e�B�b�N��DZ
    short m_dzTriggerPct = 300;              // �g���K�[��DZ(�番��: 300=3%)

    // HK��Binding
    std::unordered_map<int, Binding> m_bind;

    // �G�b�W���o
    std::array<std::atomic<uint8_t>, 512> m_prevDown{};
};

#endif // _WIN32

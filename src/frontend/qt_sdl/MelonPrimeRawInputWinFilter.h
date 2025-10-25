#pragma once
#ifdef _WIN32

// ���C�u�����Ǎ�(QAbstractNativeEventFilter��Qt�^�̉����̂���)
#include <QtCore/QAbstractNativeEventFilter>
// ���C�u�����Ǎ�(QByteArray�̉����̂���)
#include <QtCore/QByteArray>
// ���C�u�����Ǎ�(qintptr��`�̂���)
#include <QtCore/qglobal.h>

// ���C�u�����Ǎ�(Win32�^�g�p�̂���)
#include <windows.h>
// ���C�u�����Ǎ�(HID�֘A�萔�̂���)
#include <hidsdi.h>

// ���C�u�����Ǎ�(STL�e��̂���)
#include <array>
// ���C�u�����Ǎ�(�ϒ��z��̂���)
#include <vector>
// ���C�u�����Ǎ�(�A�z�z��̂���)
#include <unordered_map>
// ���C�u�����Ǎ�(���q����̂���)
#include <atomic>
// ���C�u�����Ǎ�(�W�������̂���)
#include <cstdint>
// ���C�u�����Ǎ�(����������̂���)
#include <cstring>

// �����C�����C����`(�z�b�g�p�X�œK���̂���)
#ifndef FORCE_INLINE
#  if defined(_MSC_VER)
#    define FORCE_INLINE __forceinline
#  else
#    define FORCE_INLINE __attribute__((always_inline)) inline
#  endif
#endif

/**
 * RawInput�t�B���^�{��.
 *
 * ��x���d����WM_INPUT�𒼐ڏ������A�L�[/�}�E�X��ԂƑ��΃f���^���W�v����.
 */
class RawInputWinFilter : public QAbstractNativeEventFilter
{
public:
    /**
     * �\�z�q.
     *
     *
     * @brief RawInput�f�o�C�X�o�^�Ɠ�����ԏ��������s��.
     */
    RawInputWinFilter();

    /**
     * �j���q.
     *
     *
     * @brief RawInput�f�o�C�X�o�^����������.
     */
    ~RawInputWinFilter() override;

    /**
     * �l�C�e�B�u�C�x���g�t�B���^.
     *
     *
     * @param eventType �C�x���g��ʕ�����.
     * @param message Win32 MSG�|�C���^.
     * @param result �\��̈�.
     * @return bool �p����.
     */
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    /**
     * �}�E�X���΃f���^�擾.
     *
     *
     * @param outDx X���Η�.
     * @param outDy Y���Η�.
     */
    void fetchMouseDelta(int& outDx, int& outDy);

    /**
     * ���ǃf���^�j��.
     *
     *
     * @brief �~�ς��ꂽ���΃f���^���[��������.
     */
    void discardDeltas();

    /**
     * �S�L�[�_�E����ԃ��Z�b�g.
     *
     *
     * @brief VK�z��ƌ݊��z��𖢉���������.
     */
    void resetAllKeys();

    /**
     * �S�}�E�X�{�^�����Z�b�g.
     *
     *
     * @brief �����r�b�g�ƌ݊��z��𖢉���������.
     */
    void resetMouseButtons();

    /**
     * �z�b�g�L�[�G�b�W���Z�b�g.
     *
     *
     * @brief ����/����G�b�W���o�p�̑O���Ԃ��N���A����.
     */
    void resetHotkeyEdges();

    /**
     * �z�b�g�L�[�o�^.
     *
     *
     * @param hk �z�b�g�L�[ID.
     * @param vks �\�����z�L�[�z��.
     */
    void setHotkeyVks(int hk, const std::vector<UINT>& vks);

    /**
     * �z�b�g�L�[����������.
     *
     *
     * @param hk �z�b�g�L�[ID.
     * @return bool ������.
     */
    bool hotkeyDown(int hk) const noexcept;

    /**
     * �z�b�g�L�[����茟�o.
     *
     *
     * @param hk �z�b�g�L�[ID.
     * @return bool �����.
     */
    bool hotkeyPressed(int hk) noexcept;

    /**
     * �z�b�g�L�[�����茟�o.
     *
     *
     * @param hk �z�b�g�L�[ID.
     * @return bool ������.
     */
    bool hotkeyReleased(int hk) noexcept;

private:
    // �����}�X�N�\���̒�`(�O�v�Z�ƍ��̂���)
    struct HotkeyMask {
        // �r�b�g�}�b�v�z���`(VK 256bit�ێ��̂���)
        uint64_t vkMask[4]{ 0,0,0,0 };
        // �}�E�X�{�^���W����`(5bit�ێ��̂���)
        uint8_t  mouseMask{ 0 };
        // �L���t���O��`(����Z���̂���)
        uint8_t  hasMask{ 0 };
        // �p�f�B���O��`(�A���C���ێ��̂���)
        uint16_t _pad{ 0 };
    };

    // ���͏�ԍ\���̒�`(��R�X�g�Q�Ƃ̂���)
    struct InputState {
        // VK������Ԓ�`(64bit�~4�̂���)
        std::array<std::atomic<uint64_t>, 4> vkDown{ {0,0,0,0} };
        // �}�E�X�{�^��������Ԓ�`(5bit�̂���)
        std::atomic<uint8_t>                 mouseButtons{ 0 };
    };

    // �z�b�g�L�[�������`(�z�񒷌���̂���)
    static constexpr int kMaxHotkeyId = 256;

    // RAWINPUT�o�^�z���`(�o�^/�����Ǘ��̂���)
    RAWINPUTDEVICE m_rid[2]{};

    // ���͏�ԕێ���`(�����W�v�̂���)
    InputState m_state{};

    // X/Y���΃f���^64bit�p�b�N��`(���qRMW�񐔍팸�̂���)
    alignas(8) std::atomic<uint64_t> m_dxyPack{ 0 };

    // �݊��z���`(����I/F�ێ��̂���)
    std::array<std::atomic<uint8_t>, 256> m_vkDownCompat{};
    // �݊��z���`(����I/F�ێ��̂���)
    std::array<std::atomic<uint8_t>, 5>   m_mbCompat{};

    // �G�b�W���o�p�O���Ԓ�`(64bit�����̂���)
    std::array<std::atomic<uint64_t>, (kMaxHotkeyId + 63) / 64> m_hkPrev{};

    // �O�v�Z�}�X�N�W����`(�����ƍ��̂���)
    std::array<HotkeyMask, kMaxHotkeyId> m_hkMask{};

    // �t�H�[���o�b�N�o�^��`(����/����HK�̂���)
    std::unordered_map<int, std::vector<UINT>> m_hkToVk;

    // RAWINPUT��M�p�o�b�t�@��`(�P��擾�̂���)
    alignas(8) BYTE m_rawBuf[sizeof(RAWINPUT) + 64]{};

    // VK��Ԑݒ�w���p��`(���q1��X�V�̂���)
    FORCE_INLINE void setVkBit(UINT vk, bool down) noexcept;
    // VK��Ԏ擾�w���p��`(��R�X�g�Q�Ƃ̂���)
    FORCE_INLINE bool getVkState(UINT vk) const noexcept;
    // �}�E�X�{�^���擾�w���p��`(��R�X�g�Q�Ƃ̂���)
    FORCE_INLINE bool getMouseButton(int b) const noexcept;
    // �}�X�N�\�z�w���p��`(���O�v�Z�̂���)
    FORCE_INLINE void addVkToMask(HotkeyMask& m, UINT vk) noexcept;
};

#endif // _WIN32

// �C���N���[�h�K�[�h�錾(���d��`�h�~�̂���)
#pragma once
// Qt���ۃC�x���g�t�B���^�Q�Ɛ錾(�l�C�e�B�u�t�b�N�����̂���)
#include <QtCore/QAbstractNativeEventFilter>
// QByteArray�Q�Ɛ錾(�V�O�l�`����v�̂���)
#include <QtCore/QByteArray>
// Qt�O���[�o���Q�Ɛ錾(Q_UNUSED���p�̂���)
#include <QtCore/QtGlobal>
// ���q����Q�Ɛ錾(���b�N���X��ԊǗ��̂���)
#include <atomic>
// �z��Q�Ɛ錾(�Œ蒷�W���ێ��̂���)
#include <array>
// �x�N�^�Q�Ɛ錾(HK��VK�}�b�s���O�ێ��̂���)
#include <vector>
// �A�z�z��Q�Ɛ錾(HK��VK�����ێ��̂���)
#include <unordered_map>
// �����^�Q�Ɛ錾(uint8_t���p�̂���)
#include <cstdint>

// �����t��Win32��荞��(��d��`����̂���)
#ifdef _WIN32
// �y��Windows�w�b�_�w��(�r���h���ԒZ�k�̂���)
#ifndef WIN32_LEAN_AND_MEAN
// �}�N����`(�œK���̂���)
#define WIN32_LEAN_AND_MEAN 1
#endif
// Windows API����(UINT��VK_*�̂���)
#include <windows.h>
#endif


///**
/// * RawInput�l�C�e�B�u�C�x���g�t�B���^�N���X�錾.
/// *
/// * �}�E�X���΃f���^�^�S�{�^����ԁ^�L�[�{�[�hVK������Ԃ����W���A
/// * HK��VK�}�b�s���O�Ɋ�Â������Ɖ�API��񋟂���.
/// */
 // �N���X��`�{�̐錾(�C�x���g�t�B���^�����̂���)
class RawInputWinFilter final : public QAbstractNativeEventFilter
{
public:
    ///**
    /// * �R���X�g���N�^�錾.
    /// *
    /// * �f�o�C�X�o�^�Ɠ�����ԏ��������s��.
    /// */
     // �R���X�g���N�^�錾(���������s�̂���)
    RawInputWinFilter();

    ///**
    /// * �f�X�g���N�^�錾.
    /// *
    /// * �f�o�C�X�o�^�������s��.
    /// */
     // �f�X�g���N�^�錾(��n�����s�̂���)
    ~RawInputWinFilter() override;

    ///**
    /// * �l�C�e�B�u�C�x���g�t�B���^�錾.
    /// *
    /// * WM_INPUT����Raw���͂����W����.
    /// *
    /// * @param eventType �C�x���g���.
    /// * @param message OS���b�Z�[�W�|�C���^.
    /// * @param result �ԋp�l�|�C���^.
    /// * @return �`�d��.
    /// */
     // �����o�֐��錾(�C�x���g�����̂���)
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    ///**
    /// * ���΃f���^�擾�֐��錾.
    /// *
    /// * �ݐ�dx,dy�����o���ă��Z�b�g����.
    /// */
     // �����o�֐��錾(�f���^��̂̂���)
    void fetchMouseDelta(int& outDx, int& outDy);

    ///**
    /// * ���΃f���^�j���֐��錾.
    /// *
    /// * �ݐ�dx,dy�𑦎��[��������.
    /// */
     // �����o�֐��錾(�c�������̂���)
    void discardDeltas();

    ///**
    /// * �S�L�[��ԃ��Z�b�g�֐��錾.
    /// *
    /// * ���ׂẴL�[�{�[�hVK�𖢉����֖߂�.
    /// */
     // �����o�֐��錾(�딚�h�~�̂���)
    void resetAllKeys();

    ///**
    /// * �}�E�X�{�^����ԃ��Z�b�g�֐��錾.
    /// *
    /// * ��/�E/��/X1/X2�𖢉����֖߂�.
    /// */
     // �����o�֐��錾(�딚�h�~�̂���)
    void resetMouseButtons();

    ///**
    /// * HK��VK�o�^�֐��錾.
    /// *
    /// * �w��HK�ɑ΂��Ή�����VK���ݒ肷��.
    /// *
    /// * @param hk �z�b�g�L�[ID.
    /// * @param vks ���z�L�[��.
    /// */
     // �����o�֐��錾(�ݒ蔽�f�̂���)
    void setHotkeyVks(int hk, const std::vector<UINT>& vks);

    ///**
    /// * HK��������֐��錾.
    /// *
    /// * �o�^�ς�VK�̂����ꂩ�������Ȃ�true.
    /// *
    /// * @param hk �z�b�g�L�[ID.
    /// * @return �������.
    /// */
     // �����o�֐��錾(�����Ɖ�̂���)
    bool hotkeyDown(int hk) const;

    ///**
    /// * ���{�^�������Q�ƃC�����C���֐��錾.
    /// *
    /// * �݊�API�p�r�̂���.
    /// */
     // �C�����C���֐��錾(�݊��񋟂̂���)
    inline bool leftPressed() const noexcept {
        // �ǂݎ�菈�����s(���q�Q�Ƃ̂���)
        return m_mb[kMB_Left].load(std::memory_order_relaxed);
    }

    ///**
    /// * �E�{�^�������Q�ƃC�����C���֐��錾.
    /// *
    /// * �݊�API�p�r�̂���.
    /// */
     // �C�����C���֐��錾(�݊��񋟂̂���)
    inline bool rightPressed() const noexcept {
        // �ǂݎ�菈�����s(���q�Q�Ƃ̂���)
        return m_mb[kMB_Right].load(std::memory_order_relaxed);
    }

private:
    // Win32�f�o�C�X�o�^�z��錾(�o�^/�����Ǘ��̂���)
#ifdef _WIN32
    // RAWINPUTDEVICE�z��錾(�}�E�X/�L�[�{�[�h�̂���)
    RAWINPUTDEVICE rid[2]{};
#endif

    // ����X�ݐϐ錾(���b�N���X���Z�̂���)
    std::atomic<int> dx{ 0 };
    // ����Y�ݐϐ錾(���b�N���X���Z�̂���)
    std::atomic<int> dy{ 0 };

    // �}�E�X�{�^�����萔�錾(�z�񒷌���̂���)
    static constexpr size_t kMouseBtnCount = 5;
    // �}�E�X�{�^���C���f�b�N�X�񋓐錾(�ǂ݂₷������̂���)
    enum : size_t { kMB_Left = 0, kMB_Right = 1, kMB_Middle = 2, kMB_X1 = 3, kMB_X2 = 4 };

    // �L�[�{�[�hVK������Ԕz��錾(256�L�[���̂���)
    std::array<std::atomic<uint8_t>, 256> m_vkDown{};
    // �}�E�X�{�^��������Ԕz��錾(��/�E/��/X1/X2�̂���)
    std::array<std::atomic<uint8_t>, kMouseBtnCount> m_mb{};

    // HK��VK�Ή��\�錾(������������̂���)
    std::unordered_map<int, std::vector<UINT>> m_hkToVk;
};

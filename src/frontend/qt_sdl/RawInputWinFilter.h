#pragma once
// Qt�C�x���g�t�B���^��`(Win32���b�Z�[�W�T��̂���)
#include <QAbstractNativeEventFilter>
// �A�g�~�b�N(���b�N���X�ݐς̂���)
#include <atomic>
// Win32 API(�o�^�Ǝ擾�̂���)
#include <windows.h>
 #include <QAbstractNativeEventFilter>  // �C���^�t�F�[�X�Q�Ƃ̂���
 #include <QByteArray>                  // �����^�Q�Ƃ̂���
 #include <QtCore/qglobal.h>            // qintptr ��`�̂���

/**
 * RawInputWinFilter �N���X��`.
 *
 * Win32��WM_INPUT���瑊�΃}�E�X�ړ����擾���A���b�N���X�ŗݐρE�擾����.
 */
class RawInputWinFilter final : public QAbstractNativeEventFilter {
public:
    /**
     * �R���X�g���N�^��`.
     *
     * �o�^�Ə��������s��.
     */
    RawInputWinFilter();

    /**
     * �f�X�g���N�^��`.
     *
     * �o�^�������̌㏈�����s��.
     */
    ~RawInputWinFilter() override;

    /**
     * �l�C�e�B�u�C�x���g�t�B���^�{�̒�`.
     *
     * @return ���������ꍇtrue.
     */
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    /**
     * �}�E�X�f���^�擾������`.
     *
     * �ݐς��ꂽX/Y�f���^�����o�����Z�b�g����.
     */
    inline void fetchMouseDelta(int& outDx, int& outDy) {
        outDx = dx.exchange(0, std::memory_order_acq_rel);
        outDy = dy.exchange(0, std::memory_order_acq_rel);
    }

private:
    // �A�g�~�b�N�f���^X(���b�N���X�ݐς̂���)
    std::atomic<int> dx{ 0 };
    // �A�g�~�b�N�f���^Y(���b�N���X�ݐς̂���)
    std::atomic<int> dy{ 0 };

    // RAWINPUT�p�f�o�C�X(�}�E�X/�L�[�{�[�h)
    RAWINPUTDEVICE rid[2]{};
};

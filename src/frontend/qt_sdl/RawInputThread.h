//����͌��ݕs�g�p

#pragma once
// �w�b�_�Q��(�N���X�錾�̎Q�Ƃ̂���)
#include <QThread>
// �A�g�~�b�N�g�p�錾(���b�N���X�ݐς̂���)
#include <atomic>
// QPair�g�p�錾(QPair�^�ԋp�̂���)
#include <QPair>

// Raw���C�u�����Q��(��Raw API�Ăяo���̂���)
#include "rawinput/rawinput.h"

/**
 * RawInputThread �N���X��`.
 *
 * Win32 Raw���C�u����(raw_*)���|�[�����O���A���΃}�E�X�f���^�����b�N���X�ŗݐρE�擾����.
 */
class RawInputThread final : public QThread {
    Q_OBJECT
public:
    /**
     * �R���X�g���N�^��`.
     *
     * @param parent �eQObject.
     */
    explicit RawInputThread(QObject* parent = nullptr);

    /**
     * �f�X�g���N�^��`.
     *
     * �X���b�h�̈��S��~�Ǝ���������s��.
     */
    ~RawInputThread() override;

    /**
     * �}�E�X�f���^�擾������`.
     *
     * �ݐς��ꂽX/Y�f���^�����o���A�����J�E���^���[���N���A����.
     *
     * @return �擾����(X,Y)�f���^.
     */
    QPair<int, int> fetchMouseDelta();

    /**
     * �����f���^��̏�����`.
     *
     * Raw�R�[���o�b�N����̑��Έړ��ʂ��X���b�h���o�b�t�@�֏W�v����.
     *
     * @param axis �����.
     * @param delta �ω���.
     */
    void internalReceiveDelta(Raw_Axis axis, int delta);

protected:
    /**
     * �X���b�h�{�̏�����`.
     *
     * Raw�������`�f�o�C�X�I�[�v���`�R�[���o�b�N�o�^�`�|�[�����O�p���`�I���������s��.
     */
    void run() override;

private:
    // �ݐσf���^X(���b�N���X�ݐς̂���)
    std::atomic<int> mouseDeltaX{ 0 };
    // �ݐσf���^Y(���b�N���X�ݐς̂���)
    std::atomic<int> mouseDeltaY{ 0 };
};

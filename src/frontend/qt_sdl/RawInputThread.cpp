// �w�b�_�Q��(�N���X�錾�Ɛ�API�Q�Ƃ̂���)
#include "RawInputThread.h"
// C�W�����o��(�f�o�b�O�o�͂̂���)
#include <cstdio>
// ��������`(PRI* �}�N�����p�̂���)
#include <inttypes.h>
// �����|�C���^����`(�^�O���\���̂���)
#include <cstdint>

/**
 * ���Έړ��R�[���o�b�N�֐���`.
 *
 * ���C�u��������Raw_On_Rel�́uvoid(*)(void*, Raw_Axis, int, void*)�v��4����.
 * ��4�����̃��[�U�[�f�[�^(ctx)��this��n���O��Ŏ�̂��Aself�֋��n������.
 *
 *
 * @param user ���C�u�������R���e�L�X�g(���g�p�܂��̓t�H�[���o�b�N).
 * @param axis �����.
 * @param delta �ω���.
 * @param ctx   �o�^���̃��[�U�[�f�[�^(this).
 */
static void sample_on_rel(void* user, Raw_Axis axis, int delta, void* ctx)
{
    // ���[�U�[�f�[�^����(�o�^����this�D��̂���)
    RawInputThread* self = static_cast<RawInputThread*>(ctx ? ctx : user);
    // �k���K�[�h(���S���m�ۂ̂���)
    if (!self) return;
    // �f���^���f(�W�v�X�V�̂���)
    self->internalReceiveDelta(axis, delta);
}

/**
 * �v���O�C�����o�R�[���o�b�N�֐���`.
 *
 * �V�K�f�o�C�X���o���ɘA�ԃ^�O�����蓖�ĂăI�[�v������.
 *
 *
 * @param idx �f�o�C�X�C���f�b�N�X.
 * @param user �A�ԃJ�E���^�Q�Ɨp�|�C���^.
 */
static void sample_on_plug(int idx, void* user)
{
    // �A�ԃJ�E���^�擾(��Ӄ^�O�t�^�̂���)
    auto* next_tag = static_cast<std::intptr_t*>(user);
    const std::intptr_t tag = *next_tag;
    *next_tag = tag + 1;

    // �f�o�C�X�I�[�v��(�^�O��void*���̂���)
    raw_open(idx, reinterpret_cast<void*>(tag));
    // ���o��(���ؗe�Չ��̂���)
    std::printf("Device %" PRIdPTR " at idx %d plugged.\n", tag, idx);
}

/**
 * �A���v���O�R�[���o�b�N�֐���`.
 *
 * �f�o�C�X���O�����̃N���[�Y�Ə��o�͂��s��.
 *
 *
 * @param tag �o�^���̃^�O(void*).
 * @param user ���g�p.
 */
static void sample_on_unplug(void* tag, void* /*user*/)
{
    // �f�o�C�X�N���[�Y(�Ή�����̂���)
    raw_close(tag);
    // �o�͊ȑf��(�|�C���^�����o�͂̂���)
    std::printf("Device %" PRIdPTR " unplugged.\n", reinterpret_cast<std::intptr_t>(tag));
}

/**
 * �R���X�g���N�^��`.
 *
 * �eQThread�������̂ݍs��(����������run���Ŏ��{).
 *
 *
 * @param parent �eQObject.
 */
RawInputThread::RawInputThread(QObject* parent) : QThread(parent) {}

/**
 * �f�X�g���N�^��`.
 *
 * ���N�G�X�g���荞�݁�����(wait)�ň��S�I������.
 */
RawInputThread::~RawInputThread()
{
    // ��~�v�����o(���[�v�E�o�̂���)
    requestInterruption();
    // �����ҋ@(���S�I���̂���)
    wait();
    // raw_quit��run�����œ���X���b�h����Ă�(�����h�~�̂���)
}

/**
 * �����f���^��̏�����`.
 *
 * Raw�R�[���o�b�N����̑��Έړ��ʂ����b�N���X�ɗݐς���.
 *
 *
 * @param axis �����.
 * @param delta �ω���.
 */
void RawInputThread::internalReceiveDelta(Raw_Axis axis, int delta)
{
    // X������(���b�N���X���Z�̂���)
    if (axis == Raw_Axis::RA_X) {
        mouseDeltaX.fetch_add(delta, std::memory_order_relaxed);
    }
    // Y������(���b�N���X���Z�̂���)
    else if (axis == Raw_Axis::RA_Y) {
        mouseDeltaY.fetch_add(delta, std::memory_order_relaxed);
    }
}

/**
 * �}�E�X�f���^�擾������`.
 *
 * �ݐς��ꂽX/Y�f���^�����q�I�Ɏ��o���[���N���A����.
 *
 *
 * @return �擾����(X,Y)�f���^.
 */
QPair<int, int> RawInputThread::fetchMouseDelta()
{
    // ���q�����Ŏ��o��(��ѐ��m�ۂ̂���)
    const int dx = mouseDeltaX.exchange(0, std::memory_order_acq_rel);
    const int dy = mouseDeltaY.exchange(0, std::memory_order_acq_rel);
    // �l�ԋp(�ďo���K�p�̂���)
    return QPair<int, int>(dx, dy);
}

/**
 * �X���b�h�{�̏�����`.
 *
 * Raw���������R�[���o�b�N�o�^�������f�o�C�X�I�[�v�����|�[�����O���I������ю��{����.
 */
void RawInputThread::run()
{
    // Raw�������Ăяo��(������ԏ����̂���)
    raw_init();

    // �A�ԃ^�O������(��ӎ��ʕt�^�̂���)
    std::intptr_t next_tag = 0;

    // ��ɃR�[���o�b�N�o�^(�C�x���g��肱�ڂ��h�~�̂���)
    raw_on_rel(sample_on_rel, this);
    raw_on_unplug(sample_on_unplug, nullptr);
    raw_on_plug(sample_on_plug, &next_tag);

    // �f�o�C�X���擾(�����f�o�C�X�����̂���)
    const int dev_cnt = raw_dev_cnt();
    std::printf("Detected %d devices.\n", dev_cnt);

    // �����f�o�C�X�����I�[�v��(�^�O�t�^�̂���)
    for (int i = 0; i < dev_cnt; ++i) {
        raw_open(i, reinterpret_cast<void*>(next_tag++));
    }

    // �|�[�����O���[�v�J�n(���荞�ݗv���Ď��̂���)
    while (!isInterruptionRequested()) {
        raw_poll();
    }

    // Raw�I���Ăяo��(�o�^�����Ɠ�����~�̂���)
    raw_quit();
}

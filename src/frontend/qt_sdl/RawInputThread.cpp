// �w�b�_�Q��(�N���X�錾�̎Q�Ƃ̂���)
#include "RawInputThread.h"
// QPair�g�p�錾(QPair�^���p�̂���)
#include <QPair>
// �~���[�e�b�N�XRAII(QMutexLocker���p�̂���)
#include <QMutexLocker>
// ��������`(�|�C���^���ƈ�v���鐮���^���p�̂���)
#include <cstdint>
// printf�}�N��(PRI* �}�N�����p�̂���)
#include <inttypes.h>
// C���o��(printf���p�̂���)
#include <cstdio>

///**
/// * ���Έړ��R�[���o�b�N�֐���`.
/// *
/// * Raw���C�u��������̑��Έړ��ʒm���󂯎��A�X���b�h�C���X�^���X�փf���^�����n������.
/// *
/// *
/// * @param user ���[�U�[�f�[�^�|�C���^(�o�^����this).
/// * @param axis �����.
/// * @param delta �ω���.
/// */
 // �ÓI�֐��w��(�R�[���o�b�N�Ƃ��Ă̗��p�̂���)
static void sample_on_rel(void* user, Raw_Axis axis, int delta)
{
    // ���[�U�[�f�[�^�擾(this�����̂���)
    RawInputThread* self = static_cast<RawInputThread*>(user);
    // �k���`�F�b�N(���S���m�ۂ̂���)
    if (!self) return;
    // �f���^���f�Ăяo��(�����W�v�̂���)
    self->internalReceiveDelta(axis, delta);
}

///**
/// * �v���O�C�����o�R�[���o�b�N�֐���`.
/// *
/// * �V�K�f�o�C�X���o���ɘA�ԃ^�O�����蓖�ĂăI�[�v������.
/// *
/// *
/// * @param idx �f�o�C�X�C���f�b�N�X.
/// * @param user ���[�U�[�f�[�^�|�C���^(�A�ԃJ�E���^�Q�Ɨp).
/// */
 // �ÓI�֐��w��(�R�[���o�b�N�Ƃ��Ă̗��p�̂���)
static void sample_on_plug(int idx, void* user) {
    // �A�ԃJ�E���^�Q��(�|�C���^���Ή��̂���)
    std::intptr_t* next_tag = static_cast<std::intptr_t*>(user);
    // ���݃^�O�擾(�C���N�������g�O�l�擾�̂���)
    const std::intptr_t tag = *next_tag;
    // �J�E���^�X�V(���񊄓��̂���)
    *next_tag = tag + 1;

    // �f�o�C�X�I�[�v��(�^�O��void*�Ɉ��S���ϊ��̂���)
    raw_open(idx, reinterpret_cast<void*>(tag));
    // ���o��(�|�C���^�����S�ȏo�͂̂���)
    std::printf("Device %" PRIdPTR " at idx %d plugged.\n", tag, idx);
}

///**
/// * �A���v���O�R�[���o�b�N�֐���`.
/// *
/// * ���O���ʒm��̎��ɃN���[�Y�����Ə��o�͂��s��.
/// *
/// *
/// * @param tag �o�^���̃^�O(void*).
/// * @param user ���g�p.
/// */
 // �ÓI�֐��w��(�R�[���o�b�N�Ƃ��Ă̗��p�̂���)
static void sample_on_unplug(void* tag, void* /*user*/) {
    // �f�o�C�X�N���[�Y(�Ή�����n���h������̂���)
    raw_close(tag);
    // �������o��(�|�C���^���̉��o�͂̂���)
    std::printf("Device %" PRIdPTR " unplugged.\n", static_cast<std::intptr_t>(reinterpret_cast<std::intptr_t>(tag)));
}

///**
/// * �R���X�g���N�^��`.
/// *
/// * Raw���͂̏������A�����f�o�C�X�̃I�[�v���A�R�[���o�b�N�o�^���s��.
/// *
/// *
/// * @param parent �eQObject.
/// */
 // �������q���X�g�Ăяo��(QThread�e�ݒ�̂���)
RawInputThread::RawInputThread(QObject* parent) : QThread(parent)
{
    // Raw�������Ăяo��(���C�u���������̂���)
    raw_init();

    // �f�o�C�X���擾(�񋓏����̂���)
    const int dev_cnt = raw_dev_cnt();
    // ���o��(���o���ʊm�F�̂���)
    std::printf("Detected %d devices.\n", dev_cnt);

    // �^�O�A�ԏ�����(��ӎ��ʂ̂���)
    std::intptr_t next_tag = 0;
    // �����f�o�C�X����(�S�f�o�C�X�I�[�v���̂���)
    for (int i = 0; i < dev_cnt; ++i) {
        // �f�o�C�X�I�[�v��(�^�O���|�C���^���ŕێ����邽��)
        raw_open(i, reinterpret_cast<void*>(next_tag++));
    }

    // ���Έړ��R�[���o�b�N�o�^(this���n���̂���)
    raw_on_rel((Raw_On_Rel)sample_on_rel, this);
    // �A���v���O�R�[���o�b�N�o�^(��Еt���̂���)
    raw_on_unplug(sample_on_unplug, nullptr);
    // �v���O�C���R�[���o�b�N�o�^(���I�ǉ��Ή��̂���)
    raw_on_plug(sample_on_plug, &next_tag);
}

///**
/// * �f�X�g���N�^��`.
/// *
/// * Raw���͂̏I���������s��.
/// */
 // �f�X�g���N�^�{�̒�`(���\�[�X����̂���)
RawInputThread::~RawInputThread() {
    // Raw�I���Ăяo��(�R�[���o�b�N�����Ɠ�����~�̂���)
    raw_quit();
}

///**
/// * �����f���^��̏�����`.
/// *
/// * �R�[���o�b�N����̑��Έړ��ʂ��X���b�h���o�b�t�@�֏W�v����.
/// *
/// *
/// * @param axis �����.
/// * @param delta �ω���.
/// */
 // �����o�֐��{�̒�`(�W�v�X�V�̂���)
void RawInputThread::internalReceiveDelta(Raw_Axis axis, int delta) {
    // ���b�N�擾(RAII�ŗ�O���S�m�ۂ̂���)
    QMutexLocker lock(&mouseDeltaLock);
    // X������X�V(X���ݐς̂���)
    if (axis == Raw_Axis::RA_X) {
        // X���Z����(�ݐς̂���)
        mouseDeltaX += delta;
    }
    // Y������X�V(Y���ݐς̂���)
    else if (axis == Raw_Axis::RA_Y) {
        // Y���Z����(�ݐς̂���)
        mouseDeltaY += delta;
    }
    // �X�R�[�v�I�[���(��������̂���)
}

///**
/// * �}�E�X�f���^�擾������`.
/// *
/// * �ݐς��ꂽX/Y�f���^�����o���A�����J�E���^���[���N���A����.
/// *
/// *
/// * @return �擾����(X,Y)�f���^.
/// */
 // �����o�֐��{�̒�`(�擾�ƃ��Z�b�g�̂���)
QPair<int, int> RawInputThread::fetchMouseDelta() {
    // ���b�N�擾(��ѐ��m�ۂ̂���)
    QMutexLocker lock(&mouseDeltaLock);
    // �擾�l����(�R�s�[�񐔍ŏ����̂���)
    const QPair<int, int> value(mouseDeltaX, mouseDeltaY);
    // X���Z�b�g(����W�v�̂���)
    mouseDeltaX = 0;
    // Y���Z�b�g(����W�v�̂���)
    mouseDeltaY = 0;
    // �l�ԋp(�Ăяo�����X�V�̂���)
    return value;
}

///**
/// * �X���b�h�{�̏�����`.
/// *
/// * Raw�|�[�����O���p�����s���郋�[�v��񋟂���.
/// */
 // �����o�֐��{�̒�`(�|�[�����O�p���̂���)
void RawInputThread::run()
{
    // ���s�����胋�[�v(�X���b�h�p���̂���)
    while (isRunning()) {
        // Raw�|�[�����O�Ăяo��(�C�x���g�擾�̂���)
        raw_poll();
    }
}

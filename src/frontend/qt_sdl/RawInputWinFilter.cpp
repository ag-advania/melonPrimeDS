// �w�b�_�Q��(�N���X��Win32 API�̂���)
#include "RawInputWinFilter.h"
// C�W�����o��(�f�o�b�O�̂���)
#include <cstdio>

RawInputWinFilter::RawInputWinFilter()
{
    // �}�E�X(usage page 0x01, usage 0x02)�ƃL�[�{�[�h(0x06)�o�^(���b�Z�[�W��M�̂���)
    rid[0] = { 0x01, 0x02, 0, nullptr }; // �}�E�X
    rid[1] = { 0x01, 0x06, 0, nullptr }; // �L�[�{�[�h

    // ����Ƀt�H�A�O���E���h�����ς��ꍇ�����邽�߁AHWND�͌�ŕ⊮(���S��̂���)
    // �����ł͈�U�o�^���s�͖���(����WM_CREATE/�N����ɍēo�^�ł���)
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
}

RawInputWinFilter::~RawInputWinFilter()
{
    // �o�^����(���S�I���̂���)
    rid[0].dwFlags = RIDEV_REMOVE; rid[0].hwndTarget = nullptr;
    rid[1].dwFlags = RIDEV_REMOVE; rid[1].hwndTarget = nullptr;
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
}

bool RawInputWinFilter::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    // Win32MSG�擾(���b�Z�[�W�T��̂���)
    MSG* msg = static_cast<MSG*>(message);
    if (!msg) return false;

    if (msg->message == WM_INPUT) {
        // RAWINPUT�擾�o�b�t�@(�Œ蒷�ŏ\���ȃP�[�X������)
        RAWINPUT raw{};
        UINT size = sizeof(RAWINPUT);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam),
            RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1) {
            return false;
        }

        if (raw.header.dwType == RIM_TYPEMOUSE) {
            // ���Έړ��̂ݗݐ�(�����̂���)
            dx.fetch_add(static_cast<int>(raw.data.mouse.lLastX), std::memory_order_relaxed);
            dy.fetch_add(static_cast<int>(raw.data.mouse.lLastY), std::memory_order_relaxed);
            // �����Ń{�^�������E��������� raw.data.mouse.usButtonFlags ������
        }
        // Qt���ɓn��(������ true ��Ԃ���Qt���������Ȃ��B�K�v�ɉ����Đؑ�)
        return false;
    }
#else
    Q_UNUSED(eventType)
        Q_UNUSED(message)
        Q_UNUSED(result)
#endif
        return false;
}

// �֐��{�̒�`(�ݐσJ�E���^�̌��q�I�N���A�̂���)
void RawInputWinFilter::discardDeltas() {
    // X�ݐϒl�[����(�����m�ۂ̂���)
    dx.exchange(0, std::memory_order_acq_rel);
    // Y�ݐϒl�[����(�����m�ۂ̂���)
    dy.exchange(0, std::memory_order_acq_rel);
}

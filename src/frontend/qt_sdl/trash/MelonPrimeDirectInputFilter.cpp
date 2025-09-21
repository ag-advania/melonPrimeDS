// MelonPrimeDirectInputFilter.cpp
#ifdef _WIN32
#include "MelonPrimeDirectInputFilter.h"

MelonPrimeDirectInputFilter::~MelonPrimeDirectInputFilter() { shutdown(); }

bool MelonPrimeDirectInputFilter::init(HWND hwnd) {
    m_hwnd = hwnd ? hwnd : GetActiveWindow();

    if (FAILED(DirectInput8Create(GetModuleHandleW(nullptr),
        DIRECTINPUT_VERSION, IID_IDirectInput8W,
        (void**)&m_di, nullptr))) {
        return false;
    }
    // �ŏ��Ɍ��������A�^�b�`�ς݃Q�[���R���g���[�����J��
    if (m_di->EnumDevices(DI8DEVCLASS_GAMECTRL, &enumCb, this, DIEDFL_ATTACHEDONLY) != DI_OK) {
        return false;
    }
    return m_dev != nullptr;
}

void MelonPrimeDirectInputFilter::shutdown() {
    closeDevice();
    SAFE_RELEASE(m_di);
}

BOOL CALLBACK MelonPrimeDirectInputFilter::enumCb(const DIDEVICEINSTANCE* inst, VOID* ctx) {
    auto* self = static_cast<MelonPrimeDirectInputFilter*>(ctx);
    if (!self) return DIENUM_STOP;
    if (self->openDevice(inst->guidInstance))
        return DIENUM_STOP; // 1�䌩�����OK
    return DIENUM_CONTINUE;
}

bool MelonPrimeDirectInputFilter::openDevice(const GUID& guid) {
    closeDevice();

    if (FAILED(m_di->CreateDevice(guid, &m_dev, nullptr)))
        return false;

    if (FAILED(m_dev->SetDataFormat(&c_dfDIJoystick2))) { closeDevice(); return false; }

    // �O�i�E��r���iRawInput�⑼�A�v���Ƌ����j
    if (FAILED(m_dev->SetCooperativeLevel(m_hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE))) {
        closeDevice(); return false;
    }

    // �����[�hABS
    DIPROPDWORD am{};
    am.diph.dwSize = sizeof(DIPROPDWORD);
    am.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    am.diph.dwObj = 0;
    am.diph.dwHow = DIPH_DEVICE;
    am.dwData = DIPROPAXISMODE_ABS;
    m_dev->SetProperty(DIPROP_AXISMODE, &am.diph);

    // �W���͈֑͂�����i-32768..32767�j
    ensureRanges();

    m_dev->Acquire();
    ZeroMemory(&m_now, sizeof(m_now));
    ZeroMemory(&m_prev, sizeof(m_prev));
    for (auto& a : m_prevDown) a.store(0, std::memory_order_relaxed);
    m_connected.store(true, std::memory_order_release);
    return true;
}

void MelonPrimeDirectInputFilter::closeDevice() {
    if (m_dev) { m_dev->Unacquire(); }
    SAFE_RELEASE(m_dev);
    m_connected.store(false, std::memory_order_release);
}

void MelonPrimeDirectInputFilter::ensureRanges() {
    const DWORD ofsList[] = { DIJOFS_X, DIJOFS_Y, DIJOFS_Z, DIJOFS_RX, DIJOFS_RY, DIJOFS_RZ };
    for (DWORD ofs : ofsList) {
        DIPROPRANGE rng{};
        rng.diph.dwSize = sizeof(DIPROPRANGE);
        rng.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        rng.diph.dwHow = DIPH_BYOFFSET;
        rng.diph.dwObj = ofs;
        rng.lMin = -32768;
        rng.lMax = 32767;
        m_dev->SetProperty(DIPROP_RANGE, &rng.diph);
    }
}

void MelonPrimeDirectInputFilter::update() noexcept {
    if (!m_dev) { m_connected.store(false, std::memory_order_release); return; }

    m_prev = m_now;

    HRESULT hr = m_dev->Poll();
    if (FAILED(hr)) {
        // �t�H�A�O���E���h/�Ď擾
        hr = m_dev->Acquire();
        if (FAILED(hr)) { m_connected.store(false, std::memory_order_release); return; }
        hr = m_dev->Poll();
        if (FAILED(hr)) { m_connected.store(false, std::memory_order_release); return; }
    }

    if (FAILED(m_dev->GetDeviceState(sizeof(DIJOYSTATE2), &m_now))) {
        m_connected.store(false, std::memory_order_release);
        ZeroMemory(&m_now, sizeof(m_now));
        return;
    }

    m_connected.store(true, std::memory_order_release);
}

void MelonPrimeDirectInputFilter::bindButton(int hk, uint8_t diButtonIndex) {
    Binding b; b.type = Binding::Button; b.u.buttonIndex = diButtonIndex; m_bind[hk] = b;
}
void MelonPrimeDirectInputFilter::bindAxisThreshold(int hk, Axis axis, float threshold) {
    Binding b; b.type = Binding::Analog; b.u.axis = axis; b.threshold = threshold; m_bind[hk] = b;
}
void MelonPrimeDirectInputFilter::bindPOVDirection(int hk, uint8_t dir0123) {
    Binding b; b.type = Binding::POV; b.u.povDir = dir0123 & 3; m_bind[hk] = b;
}
void MelonPrimeDirectInputFilter::clearBinding(int hk) { m_bind.erase(hk); }

static inline bool povMatches(uint32_t pov, uint8_t dir) {
    if (pov == 0xFFFFFFFFu) return false; // centered
    // POV��100���x�BU=0, R=9000, D=18000, L=27000 �t�߂����e
    const int want = (dir == 0) ? 0 : (dir == 1) ? 9000 : (dir == 2) ? 18000 : 27000;
    const int diff = std::abs((int)pov - want);
    return diff <= 2500; // �}25�x���炢�̂�邢����
}

bool MelonPrimeDirectInputFilter::evalBinding(const Binding& b) const noexcept {
    const auto& s = m_now;

    if (b.type == Binding::Button) {
        const uint8_t idx = b.u.buttonIndex;
        return idx < 128 && (s.rgbButtons[idx] & 0x80) != 0;
    }

    if (b.type == Binding::POV) {
        // POV0�̂ݑΉ��i�\���ȃP�[�X�������j
        return povMatches(s.rgdwPOV[0], b.u.povDir);
    }

    if (b.type == Binding::Analog) {
        return axisValue(b.u.axis) >= b.threshold;
    }

    return false;
}

bool MelonPrimeDirectInputFilter::hotkeyDown(int hk) const noexcept {
    auto it = m_bind.find(hk);
    if (it == m_bind.end()) return false;
    return evalBinding(it->second);
}

bool MelonPrimeDirectInputFilter::hotkeyPressed(int hk) noexcept {
    const bool d = hotkeyDown(hk);
    auto& p = m_prevDown[static_cast<size_t>(hk) & 511];
    const uint8_t prev = p.exchange(d, std::memory_order_acq_rel);
    return d && !prev;
}
bool MelonPrimeDirectInputFilter::hotkeyReleased(int hk) noexcept {
    const bool d = hotkeyDown(hk);
    auto& p = m_prevDown[static_cast<size_t>(hk) & 511];
    const uint8_t prev = p.exchange(d, std::memory_order_acq_rel);
    return (!d) && prev;
}

float MelonPrimeDirectInputFilter::normStick(LONG v, short dz) noexcept {
    const int sv = (int)v;
    const int av = sv >= 0 ? sv : -sv;
    if (av <= dz) return 0.0f;
    const float n = (av - dz) / (32767.0f - dz);
    return sv >= 0 ? n : -n;
}
float MelonPrimeDirectInputFilter::normTrig(LONG v, short pctDZ) noexcept {
    // v��0..32767 �z��B%DZ �͐番���i��: 300 �� 3%�j
    const float dzv = 32767.0f * (pctDZ / 10000.0f);
    if (v <= dzv) return 0.0f;
    return (v - dzv) / (32767.0f - dzv);
}

float MelonPrimeDirectInputFilter::axisValue(Axis a) const noexcept {
    const auto& g = m_now;
    switch (a) {
        // ���X�e�B�b�N�iX/Y�j
    case Axis::LXPos: return std::max(0.0f, normStick(g.lX, m_dzLeft));
    case Axis::LXNeg: return std::max(0.0f, -normStick(g.lX, m_dzLeft));
    case Axis::LYPos: return std::max(0.0f, normStick(g.lY, m_dzLeft));
    case Axis::LYNeg: return std::max(0.0f, -normStick(g.lY, m_dzLeft));
        // �E�X�e�B�b�N�iRx/Ry�j
    case Axis::RXPos: return std::max(0.0f, normStick(g.lRx, m_dzRight));
    case Axis::RXNeg: return std::max(0.0f, -normStick(g.lRx, m_dzRight));
    case Axis::RYPos: return std::max(0.0f, normStick(g.lRy, m_dzRight));
    case Axis::RYNeg: return std::max(0.0f, -normStick(g.lRy, m_dzRight));
        // �g���K�[�FZ or Rz �̕Б�/�������E���i�@�퍷�z���j
    case Axis::LT: {
        // 1) Z�̕���(LT) or 2) Rz �̐���
        const float zNeg = std::max(0.0f, -normStick(g.lZ, m_dzLeft));             // Z<0 ��LT����
        const float rzPos = normTrig(std::max<LONG>(0, g.lRz), m_dzTriggerPct);    // Rz>0 ��LT����
        return std::max(zNeg, rzPos);
    }
    case Axis::RT: {
        // 1) Z�̐���(RT) or 2) Rz �̐�����RT�����i�@��ɂ�荷����j
        const float zPos = std::max(0.0f, normStick(g.lZ, m_dzLeft));             // Z>0 ��RT����
        const float rzPos = normTrig(std::max<LONG>(0, g.lRz), m_dzTriggerPct);
        return std::max(zPos, rzPos);
    }
    }
    return 0.0f;
}
#endif // _WIN32

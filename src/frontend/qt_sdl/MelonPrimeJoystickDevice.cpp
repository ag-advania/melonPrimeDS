#include "MelonPrimeJoystickDevice.h"

#include "SDL_gamecontroller.h"
#include "SDL_sensor.h"
#include "MelonPrimePerfProbe.h"

#include <mutex>

namespace MelonPrime {

namespace {

// SDL_JoystickUpdate and enumeration mutate SDL's process-wide joystick
// bookkeeping. Keep that short operation serialized, but never hold it while
// sampling a device or while another instance's device lock is held for a
// longer operation.
std::mutex s_sdlProcessMutex;

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
class SdlProcessMutexGuard final {
public:
    explicit SdlProcessMutexGuard(std::mutex& mutex)
        : m_waitStart(MelonPrimePerf::ReadTicksIfEnabled())
        , m_lock(mutex)
        , m_holdStart(MelonPrimePerf::ReadTicksIfEnabled())
    {
    }

    ~SdlProcessMutexGuard()
    {
        const Uint64 releaseTick = MelonPrimePerf::ReadTicksIfEnabled();
        m_lock.unlock();
        if (m_holdStart >= m_waitStart)
            MelonPrimePerf::RecordInputMetricTicks(
                MelonPrimePerf::InputMetric::JoystickProcessMutexWait,
                m_holdStart - m_waitStart);
        if (releaseTick >= m_holdStart)
            MelonPrimePerf::RecordInputMetricTicks(
                MelonPrimePerf::InputMetric::JoystickProcessMutexHold,
                releaseTick - m_holdStart);
    }

    SdlProcessMutexGuard(const SdlProcessMutexGuard&) = delete;
    SdlProcessMutexGuard& operator=(const SdlProcessMutexGuard&) = delete;

private:
    Uint64 m_waitStart;
    std::unique_lock<std::mutex> m_lock;
    Uint64 m_holdStart;
};
#else
using SdlProcessMutexGuard = std::lock_guard<std::mutex>;
#endif

} // namespace

std::vector<JoystickDescriptor> MelonPrimeJoystickDevice::EnumerateJoysticks()
{
    SdlProcessMutexGuard processLock(s_sdlProcessMutex);
    const int count = SDL_NumJoysticks();
    if (count <= 0)
        return {};

    std::vector<JoystickDescriptor> result;
    result.reserve(static_cast<size_t>(count));
    for (int id = 0; id < count; ++id)
    {
        const char* const name = SDL_JoystickNameForIndex(id);
        result.push_back({id, name ? name : ""});
    }
    return result;
}

MelonPrimeJoystickDevice::MelonPrimeJoystickDevice()
    : m_mutex(SDL_CreateMutex(), SDL_DestroyMutex)
{}

MelonPrimeJoystickDevice::~MelonPrimeJoystickDevice()
{
    if (!m_mutex)
        return;
    SDL_LockMutex(m_mutex.get());
    CloseLocked();
    SDL_UnlockMutex(m_mutex.get());
}

void MelonPrimeJoystickDevice::CloseLockedWithoutProcessMutex() noexcept
{
    if (m_controller) {
        SDL_GameControllerClose(m_controller);
        m_controller = nullptr;
    }
    if (m_joystick) {
        SDL_JoystickClose(m_joystick);
        m_joystick = nullptr;
    }
    m_hasRumble = false;
    m_hasAccelerometer = false;
    m_hasGyroscope = false;
    m_isRumbling = false;
}

void MelonPrimeJoystickDevice::CloseLocked() noexcept
{
    SdlProcessMutexGuard processLock(s_sdlProcessMutex);
    CloseLockedWithoutProcessMutex();
}

bool MelonPrimeJoystickDevice::OpenLocked(int& joystickId) noexcept
{
    SdlProcessMutexGuard processLock(s_sdlProcessMutex);
    CloseLockedWithoutProcessMutex();

    const int numJoysticks = SDL_NumJoysticks();
    if (numJoysticks < 1)
        return false;

    if (joystickId >= numJoysticks)
        joystickId = 0;

    m_joystick = SDL_JoystickOpen(joystickId);
    if (SDL_IsGameController(joystickId))
        m_controller = SDL_GameControllerOpen(joystickId);

    if (m_controller) {
        m_hasRumble = SDL_GameControllerHasRumble(m_controller);
        if (SDL_GameControllerHasSensor(m_controller, SDL_SENSOR_ACCEL)) {
            m_hasAccelerometer = SDL_GameControllerSetSensorEnabled(
                m_controller, SDL_SENSOR_ACCEL, SDL_TRUE) == 0;
        }
        if (SDL_GameControllerHasSensor(m_controller, SDL_SENSOR_GYRO)) {
            m_hasGyroscope = SDL_GameControllerSetSensorEnabled(
                m_controller, SDL_SENSOR_GYRO, SDL_TRUE) == 0;
        }
    }

    return m_joystick != nullptr;
}

void MelonPrimeJoystickDevice::UpdateLocked() noexcept
{
    SdlProcessMutexGuard processLock(s_sdlProcessMutex);
    SDL_JoystickUpdate();
}

bool MelonPrimeJoystickDevice::IsAttachedLocked() const noexcept
{
    return m_joystick && SDL_JoystickGetAttached(m_joystick);
}

int MelonPrimeJoystickDevice::ButtonCountLocked() const noexcept
{
    return m_joystick ? SDL_JoystickNumButtons(m_joystick) : 0;
}

int MelonPrimeJoystickDevice::HatCountLocked() const noexcept
{
    return m_joystick ? SDL_JoystickNumHats(m_joystick) : 0;
}

int MelonPrimeJoystickDevice::AxisCountLocked() const noexcept
{
    return m_joystick ? SDL_JoystickNumAxes(m_joystick) : 0;
}

bool MelonPrimeJoystickDevice::SampleSourceLocked(
    JoystickSourceKind kind, uint16_t index, int32_t& value) const noexcept
{
    if (!m_joystick)
        return false;

    switch (kind) {
    case JoystickSourceKind::Button:
        value = SDL_JoystickGetButton(m_joystick, index);
        return true;
    case JoystickSourceKind::Hat:
        value = SDL_JoystickGetHat(m_joystick, index);
        return true;
    case JoystickSourceKind::Axis:
        value = SDL_JoystickGetAxis(m_joystick, index);
        return true;
    }
    return false;
}

void MelonPrimeJoystickDevice::RumbleStartLocked(uint32_t lenMs) noexcept
{
    if (m_controller && m_hasRumble && !m_isRumbling) {
        SDL_GameControllerRumble(m_controller, 0xFFFF, 0xFFFF, lenMs);
        m_isRumbling = true;
    }
}

void MelonPrimeJoystickDevice::RumbleStopLocked() noexcept
{
    if (m_controller && m_hasRumble && m_isRumbling) {
        SDL_GameControllerRumble(m_controller, 0, 0, 0);
        m_isRumbling = false;
    }
}

bool MelonPrimeJoystickDevice::ReadMotionLocked(
    melonDS::Platform::MotionQueryType type, float& value) const noexcept
{
    float values[3];
    if (type <= melonDS::Platform::MotionAccelerationZ) {
        if (!m_controller || !m_hasAccelerometer
            || SDL_GameControllerGetSensorData(
                   m_controller, SDL_SENSOR_ACCEL, values, 3) != 0)
            return false;

        switch (type) {
        case melonDS::Platform::MotionAccelerationX:
            value = values[0];
            return true;
        case melonDS::Platform::MotionAccelerationY:
            value = -values[2];
            return true;
        case melonDS::Platform::MotionAccelerationZ:
            value = values[1];
            return true;
        default:
            return false;
        }
    }

    if (type <= melonDS::Platform::MotionRotationZ) {
        if (!m_controller || !m_hasGyroscope
            || SDL_GameControllerGetSensorData(
                   m_controller, SDL_SENSOR_GYRO, values, 3) != 0)
            return false;

        switch (type) {
        case melonDS::Platform::MotionRotationX:
            value = values[0];
            return true;
        case melonDS::Platform::MotionRotationY:
            value = -values[2];
            return true;
        case melonDS::Platform::MotionRotationZ:
            value = values[1];
            return true;
        default:
            return false;
        }
    }
    return false;
}

} // namespace MelonPrime

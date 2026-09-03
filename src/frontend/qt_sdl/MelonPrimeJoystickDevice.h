#ifndef MELONPRIME_JOYSTICK_DEVICE_H
#define MELONPRIME_JOYSTICK_DEVICE_H

#include <SDL2/SDL.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Platform.h"

namespace MelonPrime {

// The device owns SDL pointer lifetime and the capability state. Logical
// binding/projection remains in EmuInstance and only calls the *_Locked
// methods while holding Mutex(). Cold mapping UI uses EmuInstance operations,
// so no raw SDL handle or external lock protocol crosses the device boundary.
enum class JoystickSourceKind : uint8_t {
    Button,
    Hat,
    Axis,
};

struct JoystickPhysicalSource {
    JoystickSourceKind kind = JoystickSourceKind::Button;
    uint16_t index = 0;
};

struct JoystickDescriptor {
    int id = -1;
    std::string name;
};

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
struct SdlProcessTiming {
    Uint64 waitTicks = 0;
    Uint64 holdTicks = 0;
    bool valid = false;
};
#endif

class MelonPrimeJoystickDevice final {
public:
    MelonPrimeJoystickDevice();
    ~MelonPrimeJoystickDevice();

    MelonPrimeJoystickDevice(const MelonPrimeJoystickDevice&) = delete;
    MelonPrimeJoystickDevice& operator=(const MelonPrimeJoystickDevice&) = delete;

    // Cold settings API. SDL's process-wide joystick registry is enumerated
    // under the same mutex used by open/close/update bookkeeping.
    [[nodiscard]] static std::vector<JoystickDescriptor> EnumerateJoysticks();

    [[nodiscard]] std::shared_ptr<SDL_mutex> Mutex() const noexcept
    {
        return m_mutex;
    }

    // All methods whose name ends in Locked require Mutex() to be held by the
    // caller. SDL's process-wide update/enumeration calls use a short internal
    // lock, but device pointer lifetime and reads stay on this device lock.
    bool OpenLocked(int& joystickId) noexcept;
    void CloseLocked() noexcept;
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    void UpdateLocked(SdlProcessTiming* timing) noexcept;
#else
    void UpdateLocked() noexcept;
#endif
    [[nodiscard]] bool IsAttachedLocked() const noexcept;
    [[nodiscard]] bool HasJoystickLocked() const noexcept
    {
        return m_joystick != nullptr;
    }
    [[nodiscard]] int ButtonCountLocked() const noexcept;
    [[nodiscard]] int HatCountLocked() const noexcept;
    [[nodiscard]] int AxisCountLocked() const noexcept;
    [[nodiscard]] bool SampleSourceLocked(
        JoystickSourceKind kind, uint16_t index, int32_t& value) const noexcept;

    void RumbleStartLocked(uint32_t lenMs) noexcept;
    void RumbleStopLocked() noexcept;
    [[nodiscard]] bool ReadMotionLocked(
        melonDS::Platform::MotionQueryType type, float& value) const noexcept;

private:
    void CloseLockedWithoutProcessMutex() noexcept;

    std::shared_ptr<SDL_mutex> m_mutex;
    SDL_Joystick* m_joystick = nullptr;
    SDL_GameController* m_controller = nullptr;
    bool m_hasAccelerometer = false;
    bool m_hasGyroscope = false;
    bool m_hasRumble = false;
    bool m_isRumbling = false;
};

} // namespace MelonPrime

#endif // MELONPRIME_JOYSTICK_DEVICE_H

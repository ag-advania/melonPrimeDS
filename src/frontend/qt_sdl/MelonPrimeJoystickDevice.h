#ifndef MELONPRIME_JOYSTICK_DEVICE_H
#define MELONPRIME_JOYSTICK_DEVICE_H

#include <SDL2/SDL.h>

#include <cstdint>
#include <memory>

#include "Platform.h"

namespace MelonPrime {

// The device owns SDL pointer lifetime and the capability state. Logical
// binding/projection remains in EmuInstance and only calls the *_Locked
// methods while holding Mutex(). This preserves the existing MapButton/UI
// contract while making the lifetime lock per device rather than process-wide.
enum class JoystickSourceKind : uint8_t {
    Button,
    Hat,
    Axis,
};

struct JoystickPhysicalSource {
    JoystickSourceKind kind = JoystickSourceKind::Button;
    uint16_t index = 0;
};

class MelonPrimeJoystickDevice final {
public:
    MelonPrimeJoystickDevice();
    ~MelonPrimeJoystickDevice();

    MelonPrimeJoystickDevice(const MelonPrimeJoystickDevice&) = delete;
    MelonPrimeJoystickDevice& operator=(const MelonPrimeJoystickDevice&) = delete;

    [[nodiscard]] std::shared_ptr<SDL_mutex> Mutex() const noexcept
    {
        return m_mutex;
    }

    // All methods whose name ends in Locked require Mutex() to be held by the
    // caller. SDL's process-wide update/enumeration calls use a short internal
    // lock, but device pointer lifetime and reads stay on this device lock.
    bool OpenLocked(int& joystickId) noexcept;
    void CloseLocked() noexcept;
    void UpdateLocked() noexcept;
    [[nodiscard]] bool IsAttachedLocked() const noexcept;
    [[nodiscard]] bool SampleSourceLocked(
        JoystickSourceKind kind, uint16_t index, int32_t& value) const noexcept;

    void RumbleStartLocked(uint32_t lenMs) noexcept;
    void RumbleStopLocked() noexcept;
    [[nodiscard]] bool ReadMotionLocked(
        melonDS::Platform::MotionQueryType type, float& value) const noexcept;

    [[nodiscard]] SDL_Joystick* GetJoystick() const noexcept
    {
        return m_joystick;
    }

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

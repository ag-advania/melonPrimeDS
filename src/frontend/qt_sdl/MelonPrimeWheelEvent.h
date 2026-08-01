#ifndef MELONPRIME_WHEEL_EVENT_H
#define MELONPRIME_WHEEL_EVENT_H

#include <QWheelEvent>

namespace MelonPrime {

    // Map a Qt wheel event to a physical wheel step.
    //
    // Returns +1 for wheel up (top of wheel rotating away from the user),
    // -1 for wheel down, or 0 when the event carries no usable delta.
    //
    // When the OS uses natural scrolling, Qt flips angle/pixel deltas and sets
    // inverted(). Undo that so binding labels and runtime pulses follow the
    // hardware wheel, not the content-scroll direction.
    [[nodiscard]] inline int PhysicalWheelSteps(const QWheelEvent& event) noexcept
    {
        int dy = event.angleDelta().y();
        if (dy == 0)
            dy = event.pixelDelta().y();
        if (dy == 0)
            return 0;
        if (event.inverted())
            dy = -dy;
        return (dy > 0) ? 1 : -1;
    }

} // namespace MelonPrime

#endif // MELONPRIME_WHEEL_EVENT_H

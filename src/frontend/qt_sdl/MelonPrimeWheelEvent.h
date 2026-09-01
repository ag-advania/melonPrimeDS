#ifndef MELONPRIME_WHEEL_EVENT_H
#define MELONPRIME_WHEEL_EVENT_H

#include <QWheelEvent>

namespace MelonPrime {

    // GUI-thread-only normalizer for Qt wheel events. angleDelta is expressed
    // in eighths of a degree; one physical detent is 120 units. Fractional
    // high-resolution angle deltas stay local until they form a full detent.
    // Pixel-only trackpad scrolling has no portable detent conversion and is
    // deliberately not exposed as a physical wheel binding.
    class PhysicalWheelStepAccumulator final
    {
    public:
        [[nodiscard]] int Consume(const QWheelEvent& event) noexcept
        {
            int angle = event.angleDelta().y();
            if (angle == 0)
                return 0;
            if (event.inverted())
                angle = -angle;

            const int total = m_angleRemainder + angle;
            const int steps = total / kAngleUnitsPerDetent;
            m_angleRemainder = total - steps * kAngleUnitsPerDetent;
            return steps;
        }

        void Reset() noexcept { m_angleRemainder = 0; }

    private:
        static constexpr int kAngleUnitsPerDetent = 120;
        int m_angleRemainder = 0;
    };

} // namespace MelonPrime

#endif // MELONPRIME_WHEEL_EVENT_H

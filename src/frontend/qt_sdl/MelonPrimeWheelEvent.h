#ifndef MELONPRIME_WHEEL_EVENT_H
#define MELONPRIME_WHEEL_EVENT_H

#include <cstdint>
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
            return ConsumeEvent(event);
        }

        // The GUI event path is the only caller that needs to observe the
        // emulation input-generation boundary. Keeping this overload off the
        // frame loop leaves the normal-frame cost unchanged.
        [[nodiscard]] int Consume(
            const QWheelEvent& event, uint32_t generation) noexcept
        {
            if (!m_generationInitialized || m_generation != generation) {
                m_generation = generation;
                m_generationInitialized = true;
                m_angleRemainder = 0;
            }
            return ConsumeEvent(event);
        }

        void Reset() noexcept { m_angleRemainder = 0; }

    private:
        [[nodiscard]] int ConsumeEvent(const QWheelEvent& event) noexcept
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
        static constexpr int kAngleUnitsPerDetent = 120;
        int m_angleRemainder = 0;
        uint32_t m_generation = 0;
        bool m_generationInitialized = false;
    };

} // namespace MelonPrime

#endif // MELONPRIME_WHEEL_EVENT_H

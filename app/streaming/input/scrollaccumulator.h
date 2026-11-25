#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

/**
 * Accumulates high-resolution fractional wheel and pinch-gesture deltas without losing
 * sub-tick trackpad precision across Wayland/SDL2 events.
 */
class ScrollAccumulator
{
public:
    static constexpr float kWheelDelta = 120.0f;
    static constexpr float kPinchDeltaScale = 400.0f;

    int16_t accumulate(float preciseDelta)
    {
        return accumulateScaled(preciseDelta, kWheelDelta);
    }

    int16_t accumulatePinch(float dDist)
    {
        return accumulateScaled(dDist, kPinchDeltaScale);
    }

    void reset()
    {
        m_Remainder = 0.0f;
        m_LastSign = 0;
    }

private:
    int16_t accumulateScaled(float delta, float scale)
    {
        if (!std::isfinite(delta) || delta == 0.0f) {
            return 0;
        }

        const int sign = delta > 0.0f ? 1 : -1;
        if (m_LastSign != 0 && m_LastSign != sign) {
            // Drop stale opposite-direction fractional residue so reversals respond immediately.
            m_Remainder = 0.0f;
        }
        m_LastSign = sign;

        const float total = m_Remainder + delta * scale;
        const float wholeFloat = std::trunc(total);
        m_Remainder = total - wholeFloat;

        if (wholeFloat == 0.0f) {
            return 0;
        }

        if (wholeFloat >= static_cast<float>((std::numeric_limits<int16_t>::max)())) {
            return (std::numeric_limits<int16_t>::max)();
        }
        if (wholeFloat <= static_cast<float>((std::numeric_limits<int16_t>::min)())) {
            return (std::numeric_limits<int16_t>::min)();
        }
        return static_cast<int16_t>(wholeFloat);
    }

    float m_Remainder {0.0f};
    int m_LastSign {0};
};

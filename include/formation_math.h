#pragma once

#include "party_constants.h"
#include <cstddef>

namespace Wayfarer
{
    enum class FormationMode : int
    {
        kDynamic = 0,
        kLead = 1,
        kCompanion = 2,
        kRear = 3,
        kSandbox = 4,
        kVanilla = 5
    };

    struct Vec2
    {
        float x{0.0F};
        float y{0.0F};
    };

    struct FormationSlot
    {
        float lateral{0.0F};
        float longitudinal{0.0F};
    };

    [[nodiscard]] float Length(Vec2 a_value) noexcept;
    [[nodiscard]] Vec2 Normalize(Vec2 a_value, Vec2 a_fallback = {0.0F, 1.0F}) noexcept;
    [[nodiscard]] Vec2 ForwardFromYaw(float a_yawRadians) noexcept;
    [[nodiscard]] Vec2 RightFromForward(Vec2 a_forward) noexcept;
    [[nodiscard]] Vec2 SmoothDirection(Vec2 a_current, Vec2 a_target, float a_deltaSeconds,
                                       float a_sharpness) noexcept;
} // namespace Wayfarer

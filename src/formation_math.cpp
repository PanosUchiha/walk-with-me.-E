#include "formation_math.h"

#include <algorithm>
#include <cmath>

namespace Wayfarer
{
    float Length(Vec2 a_value) noexcept
    {
        return std::sqrt(a_value.x * a_value.x + a_value.y * a_value.y);
    }

    Vec2 Normalize(Vec2 a_value, Vec2 a_fallback) noexcept
    {
        const float length = Length(a_value);
        if (length < 0.0001F) {
            const float fallbackLength = Length(a_fallback);
            if (fallbackLength < 0.0001F) {
                return {0.0F, 1.0F};
            }
            return {a_fallback.x / fallbackLength, a_fallback.y / fallbackLength};
        }
        return {a_value.x / length, a_value.y / length};
    }

    Vec2 ForwardFromYaw(float a_yawRadians) noexcept
    {
        return {std::sin(a_yawRadians), std::cos(a_yawRadians)};
    }

    Vec2 RightFromForward(Vec2 a_forward) noexcept
    {
        const auto forward = Normalize(a_forward);
        return {forward.y, -forward.x};
    }

    Vec2 SmoothDirection(Vec2 a_current, Vec2 a_target, float a_deltaSeconds, float a_sharpness) noexcept
    {
        const auto current = Normalize(a_current);
        const auto target = Normalize(a_target, current);
        const float alpha = std::clamp(
            1.0F - std::exp(-std::max(0.0F, a_sharpness) * std::max(0.0F, a_deltaSeconds)), 0.0F, 1.0F);
        const float yaw = std::atan2(current.x, current.y);
        const float difference = std::remainder(std::atan2(target.x, target.y) - yaw, 6.283185307F);
        return ForwardFromYaw(yaw + difference * alpha);
    }

} // namespace Wayfarer

#pragma once
#include "travel_planner.h"

namespace Wayfarer::HandHolding
{
    inline constexpr std::uint32_t approachRadius = 12;
    inline Vec2 ApproachPoint(Vec2 player, float heading, int side)
    {
        const auto forward = ForwardFromYaw(heading), right = RightFromForward(forward);
        return {player.x + right.x * (side < 0 ? -40.0F : 40.0F) - forward.x * 10,
                player.y + right.y * (side < 0 ? -40.0F : 40.0F) - forward.y * 10};
    }
    inline TravelTuning ApproachTuning(TravelTuning normal, bool active)
    {
        if (active) {
            normal.arrivalRadius = approachRadius;
            normal.goalThreshold = 12;
        }
        return normal;
    }
    // Each WWM slot has its own package. Restore the value we borrowed, including
    // on dialogue, combat, failed navigation, handoff and load/reset.
    struct ArrivalRadiusLease
    {
        std::optional<std::uint32_t> original;
        bool Update(std::uint32_t& radius, bool active)
        {
            const auto before = radius;
            if (active) {
                if (!original) {
                    original = radius;
                }
                radius = std::min(*original, approachRadius);
            }
            else if (original) {
                if (radius == std::min(*original, approachRadius)) {
                    radius = *original;
                }
                original.reset();
            }
            return radius != before;
        }
    };
} // namespace Wayfarer::HandHolding

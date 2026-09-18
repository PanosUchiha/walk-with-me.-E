#pragma once
#include "travel_planner.h"

namespace Wayfarer::HandHolding
{
    inline float LocomotionLead(float speed)
    {
        return speed > 5 ? std::clamp(180 + speed * 2.0F, 300.0F, 1000.0F) : 0;
    }

    inline Vec2 LocomotionDirection(bool attached, float heading, Vec2 velocity, Vec2 fallback)
    {
        if (attached) {
            return std::isfinite(heading) ? ForwardFromYaw(heading) : fallback;
        }
        return std::isfinite(velocity.x) && std::isfinite(velocity.y) && Length(velocity) > 5
                   ? Normalize(velocity)
                   : fallback;
    }

    inline bool RepathAttached(const TravelGoalState& goal, Vec2 actor, float speed, bool ownsPackage,
                               const TravelTuning& tuning)
    {
        if (!goal.active || goal.recoveryTime > 0) {
            return false;
        }
        if (!goal.hasRoute) {
            return true;
        }
        if (goal.sinceRepath < .3F) {
            return false;
        }
        if (!ownsPackage) {
            return goal.sinceRepath >= 1;
        }
        if (goal.pace != goal.submittedPace) {
            return true;
        }
        const float change = Length({goal.goal.x - goal.submittedGoal.x, goal.goal.y - goal.submittedGoal.y});
        const float remaining = Length({goal.submittedGoal.x - actor.x, goal.submittedGoal.y - actor.y});
        const float current = Length({goal.goal.x - actor.x, goal.goal.y - actor.y});

        if (speed <= 5) {
            return change > 25 && remaining > tuning.arrivalRadius;
        }
        if (remaining <= tuning.arrivalRadius && current > tuning.arrivalRadius + 40) {
            return true;
        }
        const auto previous = Normalize({goal.submittedGoal.x - actor.x, goal.submittedGoal.y - actor.y});
        const auto next = Normalize({goal.goal.x - actor.x, goal.goal.y - actor.y});
        if (change > 40 && previous.x * next.x + previous.y * next.y < .85F) {
            return true;
        }
        return goal.sinceRepath >= .9F && change > 40 &&
               remaining < tuning.arrivalRadius + std::max(60.0F, speed * .75F);
    }
} // namespace Wayfarer::HandHolding

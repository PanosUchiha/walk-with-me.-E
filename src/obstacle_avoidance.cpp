#include "obstacle_avoidance.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Wayfarer
{
    namespace
    {
        bool Finite(GroundPoint p)
        {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        }
        float Distance(GroundPoint a, GroundPoint b)
        {
            return std::hypot(a.x - b.x, a.y - b.y);
        }
    } // namespace
    bool UseLocalObstacleSteering(bool wilderness, bool handGuided, bool distantCatchup, float playerDistance)
    {

        return wilderness && !handGuided && !distantCatchup && std::isfinite(playerDistance) &&
               playerDistance >= 0 && playerDistance <= 650;
    }
    ObstaclePlan ObstacleAvoidance::UseNative(GroundPoint goal)
    {
        Reset();
        nativeTime = 8;
        return {ObstacleRoute::Native, goal, true};
    }
    ObstaclePlan ObstacleAvoidance::Plan(GroundPoint origin, GroundPoint goal, float speed, float dt,
                                         float arrivalRadius, const ObstacleProbe& probe)
    {
        if (!Finite(origin) || !Finite(goal) || !std::isfinite(speed) || !std::isfinite(dt) || dt <= 0 ||
            !std::isfinite(arrivalRadius) || !probe) {
            Reset();
            return {};
        }
        dt = std::min(dt, .5F);
        if (nativeTime > 0) {
            nativeTime = std::max(0.0F, nativeTime - dt);
            return {ObstacleRoute::Native, goal, false};
        }
        const float distance = Distance(origin, goal);
        if (distance < 1) {
            const bool changed = active;
            Reset();
            return {ObstacleRoute::Direct, goal, changed};
        }
        const auto forward = Normalize({goal.x - origin.x, goal.y - origin.y});
        const auto right = RightFromForward(forward);

        const float horizon =
            std::min(distance, std::clamp(std::max(0.0F, speed) * 1.1F + 140.0F, 220.0F, 480.0F));
        const Vec2 ahead{origin.x + forward.x * horizon, origin.y + forward.y * horizon};
        auto checked = [&](Vec2 target) -> std::optional<GroundPoint> {
            auto hit = probe(origin, target);
            if (!hit || !Finite(*hit) || std::hypot(hit->x - target.x, hit->y - target.y) > 2) {
                return {};
            }
            return hit;
        };
        const bool wasActive = active;
        const bool reversal = active && forward.x * entryDirection.x + forward.y * entryDirection.y < 0;
        if (reversal) {
            Reset();
        }
        if (legs) {
            episodeTime += dt;

            if (episodeTime >= 8) {
                return UseNative(goal);
            }
        }
        bool directClear = false;
        if (active) {
            age += dt;
            const bool reached = Distance(origin, waypoint) <= std::max(90.0F, arrivalRadius + 25.0F);
            const bool behind = (waypoint.x - origin.x) * forward.x + (waypoint.y - origin.y) * forward.y < 0;
            if (!reached && !behind && age < 6.0F) {
                if (auto held = checked({waypoint.x, waypoint.y})) {
                    directClear = checked(ahead).has_value();
                    clearTime = directClear ? clearTime + dt : 0;

                    if (clearTime < .65F || age < .8F) {
                        return {ObstacleRoute::Detour, *held, false};
                    }
                }
            }
            active = false;
            age = clearTime = 0;
        }
        if (directClear || checked(ahead)) {
            Reset();
            return {ObstacleRoute::Direct, goal, wasActive};
        }
        if (legs >= 3) {
            return UseNative(goal);
        }
        std::optional<GroundPoint> best;
        float score = std::numeric_limits<float>::max();
        int selectedSide = side;
        const float step = std::clamp(horizon, 180.0F, 420.0F);
        for (float degrees : {35.0F, 60.0F, 85.0F}) {
            for (int sign : {side, -side}) {
                if (legs && sign != side) {
                    continue;
                }
                const float angle = degrees * .01745329252F;
                const Vec2 direction{forward.x * std::cos(angle) + right.x * sign * std::sin(angle),
                                     forward.y * std::cos(angle) + right.y * sign * std::sin(angle)};
                const float candidateScore = degrees + (sign == side ? 0.0F : wasActive ? 45.0F : 5.0F);
                if (candidateScore >= score) {
                    continue;
                }
                const Vec2 candidate{origin.x + direction.x * step, origin.y + direction.y * step};
                const auto reference = legs ? episodeStart : origin;
                const auto heading = legs ? entryDirection : forward;
                const float lateral = std::abs((candidate.x - reference.x) * heading.y -
                                               (candidate.y - reference.y) * heading.x);
                if (lateral > 360) {
                    continue;
                }
                if (auto hit = checked(candidate)) {
                    score = candidateScore;
                    best = hit;
                    selectedSide = sign;
                }
            }
        }
        if (!best) {
            return {ObstacleRoute::Blocked, {}, wasActive};
        }
        if (!legs) {
            episodeStart = origin;
            entryDirection = forward;
            episodeTime = 0;
        }
        ++legs;
        active = true;
        waypoint = *best;
        side = selectedSide;
        age = clearTime = 0;
        return {ObstacleRoute::Detour, waypoint, true};
    }
} // namespace Wayfarer

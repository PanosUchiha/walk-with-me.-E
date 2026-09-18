#pragma once
#include "travel_planner.h"

namespace Wayfarer
{
    enum class ObstacleRoute
    {
        Direct,
        Detour,
        Blocked,
        Native
    };
    bool UseLocalObstacleSteering(bool wilderness, bool handGuided, bool distantCatchup,
                                  float playerDistance);
    struct ObstaclePlan
    {
        ObstacleRoute route{ObstacleRoute::Blocked};
        GroundPoint target{};
        bool changed{};
        std::uint32_t blocker{};
        unsigned rays{}, navRejects{};
        bool queryFailed{};
    };

    using ObstacleProbe = std::function<std::optional<GroundPoint>(GroundPoint, Vec2)>;
    struct ObstacleAvoidance
    {
        bool active{};
        GroundPoint waypoint{};
        Vec2 entryDirection{};
        int side{1};
        float age{}, clearTime{};
        GroundPoint episodeStart{};
        float episodeTime{}, nativeTime{};
        unsigned legs{};
        void Reset() { *this = {}; }
        ObstaclePlan UseNative(GroundPoint goal);
        ObstaclePlan Plan(GroundPoint origin, GroundPoint goal, float speed, float dt, float arrivalRadius,
                          const ObstacleProbe& probe);
    };
} // namespace Wayfarer

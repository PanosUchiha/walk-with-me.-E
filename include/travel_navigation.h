#pragma once

#include "obstacle_avoidance.h"
#include "rest_navigation.h"
#include "travel_planner.h"

namespace Wayfarer
{

    class TravelNavigation
    {
      public:
        void Collect(RE::PlayerCharacter& a_player);
        [[nodiscard]] std::optional<RE::NiPoint3> ResolveConnected(RE::Actor& anchor, Vec2 goal,
                                                                   Vec2 direction) const;
        [[nodiscard]] std::optional<RE::NiPoint3> RestPoint(RE::NiPoint3 position) const;
        [[nodiscard]] std::optional<RE::NiPoint3> Resolve(RE::NiPoint3 a_origin, Vec2 a_goal,
                                                          Vec2 a_direction) const;
        [[nodiscard]] std::optional<RE::NiPoint3> ResolveClear(RE::Actor& anchor, Vec2 goal, Vec2 direction,
                                                               ObstaclePlan& diagnostics) const;
        [[nodiscard]] std::optional<RE::NiPoint3> AttachmentPoint(RE::NiPoint3 origin,
                                                                  RE::NiPoint3 goal) const;
        [[nodiscard]] RestReachability RestArea(RE::NiPoint3 origin, RE::NiPoint3 center, float radius,
                                                float height) const;
        [[nodiscard]] std::optional<RestDestination> FindRestDestination(const RestReachability& area,
                                                                         RE::NiPoint3 goal) const;
        [[nodiscard]] ObstaclePlan AvoidObstacles(RE::Actor& actor, RE::NiPoint3 goal, float speed, float dt,
                                                  float arrivalRadius, ObstacleAvoidance& state) const;

      private:
        [[nodiscard]] std::optional<GroundPoint> ProbeCorridor(RE::Actor& anchor, GroundPoint origin,
                                                               Vec2 destination,
                                                               ObstaclePlan& diagnostics) const;
        struct Location
        {
            RE::NavMesh* mesh{};
            std::uint16_t triangle{};
        };
        [[nodiscard]] std::optional<std::array<GroundPoint, 3>> Vertices(Location a_location) const;
        [[nodiscard]] std::optional<Location> Find(RE::NiPoint3 a_position, float maxHeight = 96.0F) const;
        [[nodiscard]] std::optional<GroundTriangle> RestTriangle(GroundKey key) const;
        [[nodiscard]] std::optional<Location> Neighbor(Location a_location, int a_edge) const;
        [[nodiscard]] std::optional<RE::NiPoint3> Trace(RE::NiPoint3 a_origin, Vec2 a_goal,
                                                        std::optional<float> stepHeight = {}) const;
        std::vector<RE::BSTSmartPointer<RE::BSNavmesh>> meshes;
        std::optional<Location> start;
    };
} // namespace Wayfarer

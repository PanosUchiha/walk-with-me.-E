#pragma once
#include "formation_math.h"
#include <array>
#include <span>
#include <vector>

namespace Wayfarer
{

    std::vector<int> AssignNearestRoles(std::span<const Vec2> actors, std::span<const Vec2> goals,
                                        std::span<const int> currentRoles, float switchDistance = 80.0F);
    FormationSlot BlendRoleOffset(FormationSlot current, FormationSlot target, float dt);

    std::vector<int> PairNearbyRoles(std::span<const Vec2> actors, std::span<const Vec2> goals,
                                     std::span<const int> roles, std::span<const std::array<int, 2>> pairs,
                                     Vec2 forward);
} // namespace Wayfarer

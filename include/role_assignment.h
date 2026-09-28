#pragma once
#include "formation_math.h"
#include <array>
#include <span>
#include <vector>

namespace Wayfarer
{

    // Squared-distance matching. Each companion whose role would change costs switchDistance
    // squared, so near-equal assignments keep the roles they have.
    std::vector<int> AssignNearestRoles(std::span<const Vec2> actors, std::span<const Vec2> goals,
                                        std::span<const int> currentRoles, float switchDistance = 200.0F);

    // A new assignment has to win every check for `hold` seconds before it replaces the current
    // one. A companion stepping round a rock, or the player weaving along a path, used to swap
    // places across the formation and back every second. A party with an unplaced member
    // (joined, or roles missing or repeated) is assigned at once.
    struct RoleDebounce
    {
        std::vector<int> pending;
        float age{};
        void Reset()
        {
            pending.clear();
            age = 0;
        }
        // The roles to apply now, or empty to keep the current ones. `elapsed`: time since the
        // previous check.
        std::vector<int> Update(std::span<const int> current, std::span<const int> proposed, float elapsed,
                                float hold = 1.5F);
    };
    FormationSlot BlendRoleOffset(FormationSlot current, FormationSlot target, float dt);

    std::vector<int> PairNearbyRoles(std::span<const Vec2> actors, std::span<const Vec2> goals,
                                     std::span<const int> roles, std::span<const std::array<int, 2>> pairs,
                                     Vec2 forward);
} // namespace Wayfarer

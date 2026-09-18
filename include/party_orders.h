#pragma once
#include "formation_math.h"
#include <algorithm>
#include <array>
namespace Wayfarer
{
    inline constexpr std::array<FormationMode, 6> OrderModes{
        FormationMode::kDynamic, FormationMode::kLead,    FormationMode::kCompanion,
        FormationMode::kRear,    FormationMode::kSandbox, FormationMode::kVanilla};
    inline constexpr FormationMode OrderMode(int index)
    {
        return OrderModes[std::clamp(index, 0, 5)];
    }

    inline int OrderSelection(int shortcut, int mouse, int focus, bool confirm)
    {
        if (shortcut >= 0 && shortcut < 6) {
            return shortcut;
        }
        if (mouse >= 0 && mouse < 6) {
            return mouse;
        }
        return confirm ? std::clamp(focus, 0, 5) : -1;
    }
    struct RegroupState
    {
        bool active{};
        float elapsed{};
        void Begin()
        {
            active = true;
            elapsed = 0;
        }
        void Reset()
        {
            active = false;
            elapsed = 0;
        }
        bool Tick(float dt, bool moving, bool gathered)
        {
            if (!active) {
                return false;
            }
            elapsed += std::clamp(dt, 0.0F, .5F);
            if (moving || (gathered && elapsed >= 8) || elapsed >= 45) {
                active = false;
                return true;
            }
            return false;
        }
    };
} // namespace Wayfarer

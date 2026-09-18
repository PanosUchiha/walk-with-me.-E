#pragma once
#include "party_behavior.h"
#include <array>
#include <cstdint>
namespace Wayfarer
{
    enum class RestContext
    {
        Wilderness,
        Settlement,
        Interior,
        Tavern,
        Dungeon
    };
    enum class RestActivity
    {
        Stand,
        Ground,
        Eat,
        Drink,
        Read,
        Stretch,
        Examine
    };
    inline bool SettledRest(RestContext context)
    {
        return context == RestContext::Tavern || context == RestContext::Interior ||
               context == RestContext::Settlement;
    }
    inline float ActivityRadius(RestContext context, float currentRadius, const SandboxTuning& tuning)
    {
        return SettledRest(context)
                   ? std::max(currentRadius, std::min(tuning.settledActivityRadius, tuning.maxRadius))
                   : currentRadius;
    }
    inline bool MealsAllowed(RestContext context, const SandboxTuning& tuning)
    {
        return tuning.meals && context != RestContext::Dungeon;
    }
    inline bool SettledRefreshments(RestContext context, const SandboxTuning& tuning)
    {
        return SettledRest(context) && tuning.meals;
    }
    inline RestActivity ChooseRefreshment(RestContext context, std::uint32_t roll)
    {
        const bool drink = context == RestContext::Tavern ? roll % 5 != 0 : roll % 5 == 0;
        return drink ? RestActivity::Drink : RestActivity::Eat;
    }
    inline RestActivity ChooseRestActivity(RestContext context, std::uint32_t roll,
                                           const SandboxTuning& tuning)
    {
        std::array<RestActivity, 16> choices{};
        int count = 0;
        auto add = [&](RestActivity activity, int weight = 1) {
            while (weight-- > 0) {
                choices[count++] = activity;
            }
        };
        if (roll % 32 == 0) {
            return RestActivity::Stand;
        }
        if (context == RestContext::Wilderness && tuning.groundSitting) {
            add(RestActivity::Ground, 3);
        }
        if (MealsAllowed(context, tuning)) {
            add(RestActivity::Eat, context == RestContext::Tavern ? 2 : 1);
            if (context == RestContext::Tavern) {
                add(RestActivity::Drink, 4);
            }
        }
        if (tuning.reading && context != RestContext::Dungeon) {
            add(RestActivity::Read, 2);
        }
        if (tuning.stretching) {
            add(RestActivity::Stretch);
        }
        add(RestActivity::Examine, context == RestContext::Dungeon ? 5 : 1);
        return choices[roll % count];
    }
    inline std::uint32_t RestIdleID(RestActivity activity)
    {
        switch (activity) {
        case RestActivity::Eat:
            return 0x64100;
        case RestActivity::Drink:
            return 0x640FC;
        case RestActivity::Read:
            return 0xBB052;
        case RestActivity::Stretch:
            return 0xF50F1;
        case RestActivity::Examine:
            return 0x75C3D;
        default:
            return 0xB240A;
        }
    }
    inline bool FiniteRestActivity(RestActivity activity)
    {
        return activity == RestActivity::Stretch || activity == RestActivity::Examine;
    }
} // namespace Wayfarer

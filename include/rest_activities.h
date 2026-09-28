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
        Examine,
        // Area activities.
        LookFar,
        WarmHands,
        WarmHandsCrouched,
        Pray,
        Kneel,
        Study,
        Sweep,
        Nervous,
        WipeBrow,
        CleanSword,
        WarmArms,
        TableSeat, // A chair at the inn table the party shares; held with the seat package.
        Applaud
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
    // Not in dungeons, and not out in a town's streets. The controller narrows this further by
    // place (see MealPlace in area_activities.h).
    inline bool MealsAllowed(RestContext context, const SandboxTuning& tuning)
    {
        return tuning.meals && context != RestContext::Dungeon && context != RestContext::Settlement;
    }
    inline bool SettledRefreshments(RestContext context, const SandboxTuning& tuning)
    {
        return (context == RestContext::Tavern || context == RestContext::Interior) && tuning.meals;
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
        case RestActivity::LookFar:
            return 0x75C62; // IdleLookFar
        case RestActivity::WarmHands:
            return 0xE8642; // IdleWarmHandsStanding
        case RestActivity::WarmHandsCrouched:
            return 0xE8643; // IdleWarmHandsCrouched
        case RestActivity::Pray:
            return 0x6F300; // IdlePray
        case RestActivity::Kneel:
            return 0xEFC64; // IdleSearchBody: the kneeling loop
        case RestActivity::Study:
            return 0x977ED; // IdleStudy
        case RestActivity::Sweep:
            return 0x640FE; // IdleLooseSweepingStart; the graph supplies the broom
        case RestActivity::Nervous:
            return 0x10ACB1; // IdleNervous
        case RestActivity::WipeBrow:
            return 0x977EC; // IdleWipeBrow
        case RestActivity::CleanSword:
            return 0x3EA33; // IdleCleanSword
        case RestActivity::WarmArms:
            return 0x19793; // IdleWarmArms
        case RestActivity::Applaud:
            return 0xD8731; // IdleApplaud3
        default:
            return 0xB240A;
        }
    }
    // One clip and done; everything else is an enter/loop/exit state held until ended.
    inline bool FiniteRestActivity(RestActivity activity)
    {
        switch (activity) {
        case RestActivity::Stretch:
        case RestActivity::Examine:
        case RestActivity::LookFar:
        case RestActivity::Pray:
        case RestActivity::WipeBrow:
        case RestActivity::CleanSword:
        case RestActivity::WarmArms:
        case RestActivity::Applaud:
            return true;
        default:
            return false;
        }
    }
} // namespace Wayfarer

#pragma once
#include "travel_planner.h"
namespace Wayfarer
{
    struct SandboxTuning
    {
        bool automatic{true};
        float startRadius{180};
        float maxRadius{1000};
        float settledActivityRadius{1000};
        float activityHeight{768};
        float fullSandboxAfter{30};
        float idleRecoveryAfter{45};
        float resumeDistance{250};
        bool social{true};
        bool extraPoses{false};
        bool groundSitting{true}, meals{true}, reading{true}, stretching{true};
    };
    struct SandboxState
    {
        bool active{}, initialized{}, expanded{};
        Vec2 center{}, stillOrigin{};
        float stationaryTime{}, activeTime{}, radius{};
        void Reset() { *this = {}; }
        [[nodiscard]] bool AllowsSocial(const SandboxTuning& tuning) const { return active && tuning.social; }
        void Update(Vec2 player, float speed, float dt, bool explicitOrder, float delay,
                    const SandboxTuning& tuning);
    };
    [[nodiscard]] bool DistantCatchup(bool active, float distance, float enter, float leave);
    [[nodiscard]] float TravelSpeedTarget(float normalScale, float selectedCap, bool distant,
                                          float playerDistance, float goalDistance, float enter, float leave);
} // namespace Wayfarer

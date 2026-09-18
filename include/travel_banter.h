#pragma once
#include <cstdint>

namespace Wayfarer
{

    inline bool CanWalkBanterScene(unsigned actors, bool allManaged, bool dialogue, bool packageAction,
                                   bool sceneOnly, bool playerTarget, bool looping)
    {
        return actors == 2 && allManaged && dialogue && !packageAction && !sceneOnly && !playerTarget &&
               !looping;
    }

    struct TravelBanterPair
    {
        std::uint32_t first{}, second{};
        float remaining{};
        bool Contains(std::uint32_t actor) const { return first == actor || second == actor; }
    };

    inline bool CanRelaxBanterFacing(const TravelBanterPair& pair, std::uint32_t actor, std::uint32_t target,
                                     bool admittedScene, bool speaking, bool playerDialogue, bool forceGreet)
    {
        if (!pair.first || !pair.second || pair.first == pair.second || !pair.Contains(actor) ||
            playerDialogue || forceGreet) {
            return false;
        }
        if (target && (target == actor || !pair.Contains(target))) {
            return false;
        }
        return admittedScene || (speaking && target != 0);
    }
} // namespace Wayfarer

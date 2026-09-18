#pragma once

namespace Wayfarer
{
    template <class Actor, class Player> bool InSameTravelSpace(Actor& actor, Player& player)
    {
        const auto* actorCell = actor.GetParentCell();
        const auto* playerCell = player.GetParentCell();
        if (!actorCell || !playerCell) {
            return false;
        }
        if (actorCell == playerCell) {
            return true;
        }
        if (actorCell->IsInteriorCell() || playerCell->IsInteriorCell()) {
            return false;
        }
        // Adjacent exterior cells are continuous terrain within one worldspace.
        const auto* world = actor.GetWorldspace();
        return world && world == player.GetWorldspace();
    }
} // namespace Wayfarer

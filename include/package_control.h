#pragma once
#include <cmath>
#include <cstdint>

namespace Wayfarer
{
    // Per-save, explicit player ownership, separate from API registrations.
    // Unknown private systems require the player to release control themselves.
    constexpr std::uint32_t PackageControlFlags(bool nff, bool teammate)
    {
        return nff ? 3U : teammate ? 5U : 1U;
    }
    constexpr bool ValidPackageControlFlags(std::uint32_t flags)
    {
        return flags == 1 || flags == 3 || flags == 5;
    }
    inline const char* PackageControlRejection(std::uint32_t flags, bool nff, bool teammate, float waiting)
    {
        if (!ValidPackageControlFlags(flags)) {
            return "invalid control registration";
        }
        if (!std::isfinite(waiting) || waiting < 0) {
            return "dismissed or invalid follow state";
        }
        if (nff) {
            return nullptr; // NFF can change teammate state while retaining its active slot.
        }
        if ((flags & 2) && !nff) {
            return "left NFF's active roster";
        }
        if ((flags & 4) && !teammate) {
            return "no longer a player teammate";
        }
        return nullptr;
    }

    // Frameworks clear a slot or a teammate flag for a moment during their own scenes, reloads and
    // re-slotting. A loss only counts once it has lasted this long. After that the import sleeps
    // until the NPC follows again; only the player's Release ends it.
    inline constexpr std::uint64_t packageControlGraceMs = 5000;

    // When a loss began, for the loss first seen now. An import that was never seen following
    // (straight after a load, say) gets no grace.
    constexpr std::uint64_t PackageControlLossStart(std::uint64_t now, bool seenFollowing)
    {
        return seenFollowing || now < packageControlGraceMs ? now : now - packageControlGraceMs;
    }
    constexpr bool PackageControlPaused(std::uint64_t lostSince, std::uint64_t now)
    {
        return now >= lostSince && now - lostSince >= packageControlGraceMs;
    }
} // namespace Wayfarer

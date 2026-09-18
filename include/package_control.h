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
} // namespace Wayfarer

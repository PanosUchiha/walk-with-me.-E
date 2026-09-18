#pragma once

namespace Wayfarer
{

    constexpr int FollowerDialogueRank(bool manual, bool managed, bool registered, bool excluded,
                                       bool eligible, bool enabled)
    {
        if (manual) {
            return 2;
        }
        if (!excluded && (managed || registered)) {
            return 1;
        }
        return eligible && enabled ? 0 : -1;
    }
} // namespace Wayfarer

#pragma once

#include <cmath>

namespace Wayfarer
{
    struct FollowerRecruitmentState
    {
        bool nffInstalled{};
        bool nffMember{};
        bool knownIndependent{};
        bool teammate{};
        int currentFollowerRank{-1};
        float waiting{};
    };

    // Shared by discovery and dialogue registrations. Home/history membership
    // and a teammate flag alone do not establish an active recruitment.
    inline const char* RecruitmentRejection(const FollowerRecruitmentState& state,
                                            bool requireTeammate = true)
    {
        if (!std::isfinite(state.waiting) || state.waiting < 0.0F) {
            return "follower has a dismissed or invalid follow state";
        }
        if (state.nffMember) {
            return nullptr;
        }
        if (state.knownIndependent) {
            return state.teammate ? nullptr : "independent follower is not recruited";
        }
        if (state.nffInstalled) {
            return "not in NFF's active follower roster";
        }
        if (requireTeammate && !state.teammate) {
            return "not a player teammate";
        }
        if (state.currentFollowerRank < 0) {
            return "not an active member of CurrentFollowerFaction";
        }
        return nullptr;
    }
} // namespace Wayfarer

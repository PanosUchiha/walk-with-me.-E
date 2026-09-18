#pragma once
#include <cstdint>
#include <initializer_list>
#include <optional>

namespace Wayfarer::CommandGesture
{
    struct Profile
    {
        int selector{140}, arm{1};
        float duration{1.5F};
        const char* folder{"Wave Children"};
        const char* label{"Wave"};
    };
    inline Profile ForMode(int mode)
    {
        if (mode == 1) {
            return {20, 2, 1.2F, "Open Doors", "Forward reach"};
        }

        if (mode == 3) {
            return {10, 1, 1.8F, "Loot Stuff", "Right-hand sweep"};
        }
        return {};
    }
    inline Profile LeadPoint()
    {
        return {901, 2, 1.8F, "Lead Point", "Left-hand point"};
    }

    inline Profile CompanionInvite()
    {
        return {902, 0, 1.8F, "Companion Invite", "Two-arm invitation"};
    }

    inline std::optional<Profile> PrototypeForMode(int mode)
    {
        if (mode == 1) {
            return Profile{911, 2, 2.35F, "Prototype Lead", "Double forward point"};
        }
        if (mode == 2) {
            return Profile{912, 0, 2.35F, "Prototype Companion", "Two-arm beckon"};
        }
        if (mode == 3) {
            return Profile{913, 1, 2.35F, "Prototype Rear", "Open-palm elbow pull"};
        }
        return std::nullopt;
    }
    inline Profile SelectPrototypeProfile(int mode, Profile fallback, bool enabled, bool available,
                                          bool firstPerson)
    {
        const auto prototype = PrototypeForMode(mode);
        return prototype && enabled && available && !firstPerson ? *prototype : fallback;
    }

    inline std::optional<Profile> ApprovedForMode(int mode)
    {
        if (mode == 1) {
            return Profile{921, 2, 2.35F, "Prototype Lead Female", "Forward invitation"};
        }
        if (mode == 2) {
            return Profile{922, 3, 2.35F, "Prototype Companion Female", "Waist invitation"};
        }
        if (mode == 3) {
            return Profile{923, 3, 2.35F, "Prototype Rear Female", "Beside-head back wave"};
        }
        if (mode == 0) {
            return Profile{924, 3, 2.95F, "Natural Formation Female", "Index-up formation circle"};
        }
        return std::nullopt;
    }
    inline bool ThirdPersonOnly(Profile p)
    {
        return (p.selector >= 911 && p.selector <= 913) || (p.selector >= 921 && p.selector <= 924);
    }
    inline bool AuthoredGlance(Profile p)
    {
        return p.selector >= 922 && p.selector <= 924;
    }
    inline Profile SelectApprovedProfile(int mode, Profile fallback, bool enabled, bool available,
                                         bool firstPerson)
    {
        const auto p = ApprovedForMode(mode);
        return p && enabled && available && !firstPerson ? *p : fallback;
    }
    inline Profile SelectProfile(Profile requested, bool pointEnabled, bool pointAvailable, bool firstPerson)
    {

        return requested.selector == 20 && pointEnabled && pointAvailable && !firstPerson ? LeadPoint()
                                                                                          : requested;
    }
    inline Profile SelectCompanionProfile(int mode, Profile selected, bool enabled, bool available,
                                          bool firstPerson)
    {
        return mode == 2 && enabled && available && !firstPerson ? CompanionInvite() : selected;
    }
    inline std::uint32_t Encode(Profile p)
    {
        return 0x10000U | (static_cast<std::uint32_t>(p.selector) << 4) | p.arm;
    }
    inline std::optional<Profile> Decode(std::uint32_t saved)
    {
        if (saved == 1) {
            Profile p;
            p.arm = 0;
            return p;
        }
        if (saved == 2) {
            return Profile{};
        }
        if (saved == Encode(LeadPoint())) {
            return LeadPoint();
        }
        if (saved == Encode(CompanionInvite())) {
            return CompanionInvite();
        }
        for (int mode : {1, 2, 3}) {
            const auto p = *PrototypeForMode(mode);
            if (saved == Encode(p)) {
                return p;
            }
        }
        for (int mode : {0, 1, 2, 3}) {
            const auto p = *ApprovedForMode(mode);
            if (saved == Encode(p)) {
                return p;
            }
        }

        for (int mode : {0, 2, 3}) {
            auto p = *ApprovedForMode(mode);
            p.arm = 0;
            if (saved == Encode(p)) {
                return p;
            }
        }

        for (const Profile old : {Profile{902, 1, 1.8F, "Companion Invite", "Companion invitation"},
                                  Profile{70, 1, 1.8F, "Touch Stones", "Open palm"},
                                  Profile{140, 0, 1.5F, "Wave Children", "Wave"}}) {
            if (saved == Encode(old)) {
                return old;
            }
        }
        for (const int mode : {0, 1, 3}) {
            const auto p = ForMode(mode);
            if (Encode(p) == saved) {
                return p;
            }
        }
        return std::nullopt;
    }
} // namespace Wayfarer::CommandGesture

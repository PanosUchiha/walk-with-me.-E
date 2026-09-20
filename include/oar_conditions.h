#pragma once
#include "OpenAnimationReplacerAPI-Conditions.h"

namespace Wayfarer::OAR
{
    // OAR 3.x grants conditions interface V3. OAR 2.x refuses V3 outright (2.3.6 is
    // what Nolvus and many other lists ship), which left every WWM condition
    // unregistered and OAR reporting "Walk With Me: Missing". V2 is the same
    // three-function interface, so fall back to it rather than go without.
    struct Grant
    {
        OAR_API::Conditions::IConditionsInterface* api{};
        bool legacy{};
    };

    inline const Grant& Interface()
    {
        static const Grant grant = [] {
            using OAR_API::Conditions::InterfaceVersion;
            Grant result{OAR_API::Conditions::GetAPI(InterfaceVersion::V3), false};
            if (!result.api) {
                result = {OAR_API::Conditions::GetAPI(InterfaceVersion::V2), true};
            }
            logger::info("[OAR] Conditions interface: {}",
                         !result.api ? "unavailable" : result.legacy ? "V2 (OAR 2.x)" : "V3 (OAR 3.x)");
            if (!result.api) {
                logger::warn("[OAR] WWM custom conditions could not register. Check that "
                             "OpenAnimationReplacer.dll loaded for this Skyrim runtime in "
                             "skse64.log and OpenAnimationReplacer.log. requiredPlugin must remain "
                             "'Walk With Me'; changing it to Wayfarer.esp or installing Detection "
                             "Plugin will not register WWM conditions.");
            }
            return result;
        }();
        return grant;
    }

    // OAR 2.x reads this vtable slot as GetConditionType(), where 1 is kCustom. 3.x
    // renamed the slot and appended the rest of the vtable, which 2.x never calls.
    inline ::Conditions::ConditionAPIVersion CustomConditionVersion()
    {
        return Interface().legacy ? ::Conditions::ConditionAPIVersion::kOld_Custom
                                  : ::Conditions::ConditionAPIVersion::Latest;
    }
} // namespace Wayfarer::OAR

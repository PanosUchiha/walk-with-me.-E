#pragma once

#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <unordered_set>

namespace Wayfarer
{
    inline constexpr std::array<std::string_view, 7> legacyCustomExclusions{
        "inigo.esp",    "lucien.esp",       "018auri.esp", "remiel.esp",
        "hlioremi.esp", "hliiremilotd.esp", "katana.esp"};

    inline bool KnownCustomFollower(std::string_view plugin, unsigned int localBaseID)
    {
        for (auto name : legacyCustomExclusions) {
            if (plugin == name) {
                return true;
            }
        }
        return plugin == "dawnguard.esm" && localBaseID == 0x2B6C;
    }

    inline bool HasCustomFollowState(bool teammate, float waiting)
    {

        return teammate && std::isfinite(waiting) && waiting >= 0.0F;
    }

    inline bool UsesCustomFollowerAI(std::string_view plugin, unsigned int localBaseID, bool teammate,
                                     bool vanillaFaction)
    {
        if (KnownCustomFollower(plugin, localBaseID)) {
            return true;
        }
        const bool vanillaSource = plugin.empty() || plugin == "skyrim.esm" || plugin == "update.esm" ||
                                   plugin == "dawnguard.esm" || plugin == "hearthfires.esm" ||
                                   plugin == "dragonborn.esm";
        // Unknown followers need actual recruitment evidence. A vanilla NPC
        // with a stale teammate flag must not acquire the custom-AI bypass.
        return teammate && vanillaFaction && !vanillaSource;
    }

    inline bool IsLegacyCustomBlocklist(const std::unordered_set<std::string>& plugins)
    {
        if (plugins.size() != legacyCustomExclusions.size()) {
            return false;
        }
        for (auto name : legacyCustomExclusions) {
            if (!plugins.contains(std::string(name))) {
                return false;
            }
        }
        return true;
    }
} // namespace Wayfarer

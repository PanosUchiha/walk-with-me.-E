#pragma once
#include "config.h"
#include <SimpleIni.h>
#include <algorithm>

namespace Wayfarer
{
    inline void LoadHotkeys(const CSimpleIniA& ini, SettingsData& settings)
    {
        for (const auto& binding : hotkeyBindings) {
            const auto key = static_cast<std::uint32_t>(std::clamp<long>(
                ini.GetLongValue("Hotkeys", binding.iniKey, static_cast<std::int32_t>(settings.*binding.key)),
                -1, 255));

            binding.Set(settings, {key, Hotkeys::NormalizeModifiers(
                                            key, ini.GetLongValue("Hotkeys", binding.iniModifiers, 0))});
        }
        // A newly introduced default must not steal an existing custom binding.
        if (!ini.GetValue("Hotkeys", "iCompanionKey")) {
            const auto companion = hotkeyBindings.back().Get(settings);
            for (std::size_t i = 0; i + 1 < hotkeyBindings.size(); ++i) {
                if (hotkeyBindings[i].Get(settings) == companion) {
                    hotkeyBindings.back().Set(settings, {});
                    break;
                }
            }
        }
    }

    inline void SaveHotkeys(CSimpleIniA& ini, const SettingsData& settings)
    {
        for (const auto& binding : hotkeyBindings) {
            const auto chord = binding.Get(settings);
            ini.SetLongValue("Hotkeys", binding.iniKey, static_cast<std::int32_t>(chord.key));
            ini.SetLongValue("Hotkeys", binding.iniModifiers,
                             Hotkeys::NormalizeModifiers(chord.key, chord.modifiers));
        }
    }
} // namespace Wayfarer

#pragma once

#include "hand_hold_state.h"
#include "key_chord.h"
#include "loot_scout_policy.h"
#include "party_behavior.h"
#include "travel_planner.h"
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace Wayfarer
{
    struct SettingsData
    {
        bool enabled{true};
        HandHolding::Tuning handHolding;
        bool autoDiscover{false};
        bool requirePlayerTeammate{true};
        bool releaseInCombat{true};
        bool releaseWhenSneaking{true};
        bool releaseWhenWeaponDrawn{true};
        bool releaseWhenControlsDisabled{true};
        bool requireTravelPackage{true};
        bool disableIndoors{false};
        bool logDiagnostics{true};
        std::string pluginName{"Wayfarer.esp"};
        bool traceMovement{true};
        bool enforceNFF{true};
        bool enforceCustomFollowers{true};
        bool walkingBanter{true};
        bool victoryCelebrations{false}; // Shelved; ignored by release configuration.
        bool conversationAwareness{true}, personalities{true}, lookouts{true};
        float conversationClearance{150}, lookoutAfter{60};
        std::unordered_map<std::string, int> personalityOverrides;
        bool showOrderHUD{true};
        bool restDialogueIcons{true};
        bool animateChatIcons{true};
        bool hudPanel{true}, commandGestures{true}, pointingSignal{true}, companionSignal{true};
        bool gestureAwareness{true};
        int gamepadBinding{1};
        float hudScale{0.97F};
        float hudVertical{0.44F};
        TravelTuning travel = [] {
            TravelTuning value;
            value.idleRelease = 4.0F;
            value.spacing = .88F;
            value.catchUpSeconds = 1.6F;
            value.maxSpeedScale = 1.75F;
            return value;
        }();
        SandboxTuning sandbox = [] {
            SandboxTuning value;
            value.resumeDistance = 508.0F;
            return value;
        }();
        Loot::Tuning loot;
        float distantCatchupStart{900};
        float distantCatchupEnd{500};
        bool teleportCatchup{true};
        bool forwardCollision{true};
        float teleportDistance{4000};
        int maxFollowers{PARTY_CAPACITY};
        FormationMode mode{FormationMode::kDynamic};
        int preferredSide{1};
        std::uint32_t toggleKey{0x44};
        std::uint32_t cycleModeKey{0xFFFFFFFF};
        std::uint32_t reloadKey{0x58};
        std::uint32_t commandKey{0x48};
        std::uint32_t companionKey{0x22}; // G: control the targeted companion.
        std::uint32_t companionModifiers{};
        std::uint32_t scoutKey{0xFFFFFFFF};
        std::uint32_t rearKey{0xFFFFFFFF};
        std::uint32_t roamKey{0xFFFFFFFF};
        std::uint32_t toggleModifiers{}, cycleModeModifiers{}, reloadModifiers{};
        std::uint32_t commandModifiers{}, scoutModifiers{}, rearModifiers{}, roamModifiers{};
        float updateInterval{0.15F};
        float scanInterval{1.0F};
        float releaseDistance{2500.0F};
        std::unordered_set<std::string> excludedPlugins;
    };

    struct HotkeyBinding
    {
        const char* label;
        std::uint32_t SettingsData::* key;
        std::uint32_t SettingsData::* modifiers;
        const char* iniKey;
        const char* iniModifiers;

        Hotkeys::Chord Get(const SettingsData& settings) const
        {
            return {settings.*key, settings.*modifiers};
        }
        void Set(SettingsData& settings, Hotkeys::Chord chord) const
        {
            settings.*key = chord.key;
            settings.*modifiers = Hotkeys::NormalizeModifiers(chord.key, chord.modifiers);
        }
    };

    inline constexpr std::array<HotkeyBinding, 8> hotkeyBindings{
        {{"Party orders", &SettingsData::commandKey, &SettingsData::commandModifiers, "iCommandKey",
          "iCommandModifiers"},
         {"Enable / disable", &SettingsData::toggleKey, &SettingsData::toggleModifiers, "iToggleKey",
          "iToggleModifiers"},
         {"Cycle formation", &SettingsData::cycleModeKey, &SettingsData::cycleModeModifiers, "iCycleModeKey",
          "iCycleModeModifiers"},
         {"Lead", &SettingsData::scoutKey, &SettingsData::scoutModifiers, "iScoutKey", "iScoutModifiers"},
         {"Rear", &SettingsData::rearKey, &SettingsData::rearModifiers, "iRearKey", "iRearModifiers"},
         {"Relax", &SettingsData::roamKey, &SettingsData::roamModifiers, "iRoamKey", "iRoamModifiers"},
         {"Reload INI", &SettingsData::reloadKey, &SettingsData::reloadModifiers, "iReloadKey",
          "iReloadModifiers"},
         {"Companion control", &SettingsData::companionKey, &SettingsData::companionModifiers,
          "iCompanionKey", "iCompanionModifiers"}}};

    inline void RebindHotkey(SettingsData& settings, std::size_t row, Hotkeys::Chord chord)
    {
        if (row >= hotkeyBindings.size()) {
            return;
        }
        chord.modifiers = Hotkeys::NormalizeModifiers(chord.key, chord.modifiers);

        if (chord.key != Hotkeys::unbound) {
            for (const auto& binding : hotkeyBindings) {
                if (binding.Get(settings) == chord) {
                    binding.Set(settings, {});
                }
            }
        }
        hotkeyBindings[row].Set(settings, chord);
    }

    class Settings final
    {
      public:
        static Settings& GetSingleton();

        void Load();
        bool Save(const SettingsData& a_data);
        [[nodiscard]] const SettingsData& Get() const noexcept;
        [[nodiscard]] const std::filesystem::path& GetPath() const noexcept;
        [[nodiscard]] bool IsPluginExcluded(std::string_view a_pluginName) const;

      private:
        Settings() = default;

        SettingsData data;
        std::filesystem::path path;
    };
} // namespace Wayfarer

#include "victory_animation.h"
#include "Plugin.h"
#include "oar_conditions.h"
#include "victory_reaction.h"

namespace Wayfarer::Victory::Animation
{
    namespace
    {
        ShoulderSelections selections;
        bool registered{};

        bool Selected(RE::TESObjectREFR* reference)
        {
            if (!reference || !selections.Contains(reference->GetFormID())) {
                return false;
            }
            const auto* actor = reference->As<RE::Actor>();
            const auto* state = actor ? actor->AsActorState() : nullptr;
            return state && state->GetWeaponState() == RE::WEAPON_STATE::kSheathed && !actor->IsInCombat();
        }

        class ShoulderCondition final : public Conditions::CustomCondition
        {
          public:
            RE::BSString GetName() const override { return "WWM_PostCombatShoulderRecovery"; }
            RE::BSString GetDescription() const override
            {
                return "This companion was selected for WWM's sheathed post-combat shoulder recovery.";
            }
            RE::BSString GetArgument() const override { return ""; }
            RE::BSString GetCurrent(RE::TESObjectREFR* reference) const override
            {
                return Selected(reference) ? "true" : "false";
            }
            REL::Version GetRequiredVersion() const override { return {0, 2, 0}; }
            RE::BSString GetRequiredPluginName() const override { return Plugin::NAME.data(); }
            RE::BSString GetRequiredPluginAuthor() const override { return "Codex and Tobih"; }
            Conditions::ConditionAPIVersion GetConditionAPIVersion() const override
            {
                return OAR::CustomConditionVersion();
            }

          protected:
            bool EvaluateImpl(RE::TESObjectREFR* reference, RE::hkbClipGenerator*, void*) const override
            {
                const bool selected = Selected(reference);
                if (selected) {
                    selections.Observe(reference->GetFormID());
                }
                return selected;
            }
        };
    } // namespace

    void Register()
    {
        auto* api = OAR::Interface().api;
        if (api) {
            registered =
                api->AddCustomCondition(SKSE::GetPluginHandle(), Plugin::NAME.data(), Plugin::VERSION,
                                        "WWM_PostCombatShoulderRecovery",
                                        Conditions::CustomCondition::GetFactory<ShoulderCondition>()) ==
                OAR_API::Conditions::APIResult::OK;
        }
        logger::info("[Victory] Shoulder recovery OAR condition: {}",
                     registered ? "registered" : "unavailable; applause fallback");
    }

    bool Available()
    {
        static const bool assets = [] {
            const std::filesystem::path root{
                "Data/meshes/OpenAnimationReplacer/Walk With Me Victory/Shoulder Recovery"};
            return std::filesystem::is_regular_file(root / "config.json") &&
                   std::filesystem::is_regular_file(root / "actors/character/animations/npc_applaud3.hkx");
        }();
        return registered && assets;
    }
    void Select(std::uint32_t actor, bool selected)
    {
        selections.Set(actor, selected);
    }
    void Clear()
    {
        selections.Clear();
    }
    bool WasObserved(std::uint32_t actor)
    {
        return selections.WasObserved(actor);
    }
} // namespace Wayfarer::Victory::Animation

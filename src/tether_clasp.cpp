#include "tether_clasp.h"
#include "Plugin.h"
#include "oar_conditions.h"

namespace Wayfarer::HandHolding::Clasp
{
    namespace
    {
        Selection selection;
        bool registered{};
        template <bool Left> class Condition final : public Conditions::CustomCondition
        {
          public:
            RE::BSString GetName() const override { return Left ? "WWM_TetherClaspL" : "WWM_TetherClaspR"; }
            RE::BSString GetDescription() const override
            {
                return "This actor's selected hand belongs to the active WWM TETHER grip.";
            }
            RE::BSString GetArgument() const override { return ""; }
            RE::BSString GetCurrent(RE::TESObjectREFR* actor) const override
            {
                return Matches(actor) ? "true" : "false";
            }
            REL::Version GetRequiredVersion() const override { return {0, 2, 0}; }
            RE::BSString GetRequiredPluginName() const override { return Plugin::NAME.data(); }
            RE::BSString GetRequiredPluginAuthor() const override { return "Codex and Tobih"; }
            Conditions::ConditionAPIVersion GetConditionAPIVersion() const override
            {
                return OAR::CustomConditionVersion();
            }

          protected:
            bool EvaluateImpl(RE::TESObjectREFR* actor, RE::hkbClipGenerator*, void*) const override
            {
                return Matches(actor);
            }

          private:
            static bool Matches(RE::TESObjectREFR* actor)
            {
                return actor && selection.Matches(actor->GetFormID(), Left);
            }
        };
    } // namespace
    void Register()
    {
        auto* api = OAR::Interface().api;
        if (api) {
            const auto left = api->AddCustomCondition(
                SKSE::GetPluginHandle(), Plugin::NAME.data(), Plugin::VERSION, "WWM_TetherClaspL",
                Conditions::CustomCondition::GetFactory<Condition<true>>());
            const auto right = api->AddCustomCondition(
                SKSE::GetPluginHandle(), Plugin::NAME.data(), Plugin::VERSION, "WWM_TetherClaspR",
                Conditions::CustomCondition::GetFactory<Condition<false>>());
            registered =
                left == OAR_API::Conditions::APIResult::OK && right == OAR_API::Conditions::APIResult::OK;
        }
        logger::info("[HandHolding/Clasp] OAR conditions: {}",
                     registered ? "registered" : "unavailable; grip disabled");
    }
    bool Available()
    {
        static const bool assets = [] {
            const std::filesystem::path root{"Data/meshes/OpenAnimationReplacer/WWM Clasp"};
            for (const auto* side : {"Left", "Right"}) {
                if (!std::filesystem::is_regular_file(root / side / "config.json")) {
                    return false;
                }
                for (const auto* gender : {"male", "female"}) {
                    for (const auto* clip : {"mt_idle.hkx", "mt_walkforward.hkx", "mt_runforward.hkx"}) {
                        if (!std::filesystem::is_regular_file(root / side / "actors/character/animations" /
                                                              gender / clip)) {
                            return false;
                        }
                    }
                }
            }
            return true;
        }();
        return registered && assets;
    }
    void Select(std::uint32_t player, std::uint32_t follower, bool playerLeft)
    {
        selection.Set(player, follower, playerLeft);
    }
    void Clear()
    {
        selection.Clear();
    }
} // namespace Wayfarer::HandHolding::Clasp

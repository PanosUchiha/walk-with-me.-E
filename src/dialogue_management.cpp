#include "engine_compatibility.h"
#include "follower_dialogue.h"
#include "formation_controller.h"
#include "package_control.h"
#include "travel_space.h"

namespace
{
    void RemoveDialogueFaction(RE::Actor& actor, RE::TESFaction* faction)
    {
#ifdef WAYFARER_MODERN_COMMONLIB
        actor.RemoveFromFaction(faction);
#else
        using Function = void (*)(RE::Actor*, RE::TESFaction*);
        static REL::Relocation<Function> function{REL::RelocationID(36680, 37688)};
        function(&actor, faction);
#endif
    }
} // namespace

namespace Wayfarer
{
    bool FormationController::IsDialogueFollower(RE::Actor& actor) const
    {
        if (actor.IsPlayerRef() || actor.IsDead() || actor.IsDisabled()) {
            return false;
        }
        return RecruitmentRejectionFor(actor) == nullptr;
    }

    bool FormationController::IsRegisteredFollower(RE::FormID id) const
    {
        if (manualRegistrations.contains(id)) {
            return true;
        }
        if (const auto entry = packageRegistrations.find(id); entry != packageRegistrations.end()) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
            return actor && !PackageControlLoss(*actor, entry->second);
        }
        if (!dialogueRegistrations.contains(id)) {
            return false;
        }
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
        return actor && IsDialogueFollower(*actor);
    }

    void FormationController::SyncFollowerDialogue(RE::Actor& actor)
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* faction =
            data ? data->LookupForm<RE::TESFaction>(0x880, Settings::GetSingleton().Get().pluginName)
                 : nullptr;
        if (!faction) {
            return;
        }
        const auto id = actor.GetFormID();
        const auto rank =
            FollowerDialogueRank(dialogueRegistrations.contains(id), managed.contains(id),
                                 manualRegistrations.contains(id) || packageRegistrations.contains(id),
                                 explicitExclusions.contains(id), IsDialogueFollower(actor),
                                 enabled && mode != FormationMode::kVanilla);
        if (Engine::FactionRank(actor, faction) == rank) {
            return;
        }
        if (rank < 0) {
            RemoveDialogueFaction(actor, faction);
        }
        else {
            actor.AddToFaction(faction, static_cast<std::int8_t>(rank));
        }
    }

    void FormationController::UpdateFollowerDialogue(float dt)
    {
        dialogueRosterTimer -= dt;
        if (dialogueRosterTimer > 0) {
            return;
        }
        dialogueRosterTimer = 0.25F;
        if (loadReleaseTime <= 0) {
            UpdatePackageControl();
        }
        if (auto* lists = RE::ProcessLists::GetSingleton()) {
            Engine::ForEachHighActor(*lists, [&](RE::Actor& actor) {
                SyncFollowerDialogue(actor);
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }
    }

    void FormationController::SetDialogueManagement(RE::Actor* actor, bool add)
    {
        std::lock_guard lock(stateMutex);
        if (!actor || actor->IsPlayerRef()) {
            return;
        }
        const auto id = actor->GetFormID();
        if (add) {
            if (dialogueRegistrations.contains(id) || manualRegistrations.contains(id) ||
                packageRegistrations.contains(id) ||
                (managed.contains(id) && !explicitExclusions.contains(id))) {
                return;
            }
            if (!IsDialogueFollower(*actor)) {
                Notify("This companion is no longer following you.");
                return;
            }
            if (!enabled || mode == FormationMode::kVanilla) {
                Notify("Enable Walk With Me before adding a companion.");
                return;
            }
            if (!managed.contains(id) &&
                managed.size() >= static_cast<std::size_t>(Settings::GetSingleton().Get().maxFollowers)) {
                Notify("Walk With Me's companion limit has been reached.");
                return;
            }
            dialogueRegistrations.insert(id);
            explicitExclusions.erase(id);
            Notify("Companion added to Walk With Me.");
        }
        else {
            if (!dialogueRegistrations.erase(id)) {
                return;
            }
            explicitExclusions.insert(id);
            Notify("Companion removed from Walk With Me.");
        }

        scanTimer = Settings::GetSingleton().Get().scanInterval;
        SyncFollowerDialogue(*actor);
        logger::info("[Dialogue] {} manual management for {} ({:08X})", add ? "Enabled" : "Removed",
                     actor->GetName(), id);
    }

    const char* FormationController::PackageControlLoss(RE::Actor& actor, std::uint32_t flags) const
    {
        if (actor.IsDead() || actor.IsDisabled()) {
            return "NPC is unavailable";
        }
        if (auto* player = RE::PlayerCharacter::GetSingleton(); player && actor.IsHostileToActor(player)) {
            return "NPC is hostile";
        }
        return PackageControlRejection(
            flags, IsNFFFollower(actor), actor.IsPlayerTeammate(),
            actor.AsActorValueOwner()->GetActorValue(RE::ActorValue::kWaitingForPlayer));
    }

    const char* FormationController::PackageControlStartRejection(RE::Actor& actor) const
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || actor.IsPlayerRef() || actor.IsDead() || actor.IsDisabled() || !actor.Is3DLoaded()) {
            return "Select a nearby living NPC";
        }
        if (!InSameTravelSpace(actor, *player)) {
            return "Move into the same area as this NPC";
        }
        if (actor.GetPosition().GetDistance(player->GetPosition()) > 800.0F) {
            return "Move closer to this NPC";
        }
        if (manualRegistrations.contains(actor.GetFormID())) {
            return "Controlled by an API integration";
        }
        if (actor.IsHostileToActor(player) || actor.IsInCombat()) {
            return "Wait until this NPC is out of combat and friendly";
        }
        if (!actor.IsAIEnabled() || actor.GetCurrentScene()) {
            return "Wait until this NPC's current scene finishes";
        }
        if (const auto* reason = PackageControlRejection(
                1, false, false,
                actor.AsActorValueOwner()->GetActorValue(RE::ActorValue::kWaitingForPlayer))) {
            return reason;
        }
        if (!enabled || mode == FormationMode::kVanilla) {
            return "Enable Walk With Me and choose a travel style first";
        }
        auto reserved = managed.size();
        for (const auto& [id, flags] : packageRegistrations) {
            static_cast<void>(flags);
            if (!managed.contains(id)) {
                ++reserved;
            }
        }
        if (!managed.contains(actor.GetFormID()) && !packageRegistrations.contains(actor.GetFormID()) &&
            reserved >= static_cast<std::size_t>(Settings::GetSingleton().Get().maxFollowers)) {
            return "The party is full";
        }
        return nullptr;
    }

    void FormationController::SetPackageControl(RE::FormID id, bool take, std::uint64_t expectedGeneration)
    {
        std::lock_guard lock(stateMutex);
        if (generation != expectedGeneration) {
            return;
        }
        if (!take) {
            if (!packageRegistrations.erase(id)) {
                return;
            }
            explicitExclusions.insert(id);
            RemoveManaged(id);
            logger::info("[PackageControl] Player released {:08X}", id);
            Notify("Travel control returned to the NPC's own AI.");
        }
        else {
            if (packageRegistrations.contains(id)) {
                return;
            }
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
            if (!actor) {
                return;
            }
            if (const auto* reason = PackageControlStartRejection(*actor)) {
                Notify(reason);
                return;
            }
            const auto flags = PackageControlFlags(IsNFFFollower(*actor), actor->IsPlayerTeammate());
            packageRegistrations[id] = flags;
            dialogueRegistrations.erase(id);
            explicitExclusions.erase(id);
            logger::info("[PackageControl] Player imported {} ({:08X}), recruitment tracking={}",
                         actor->GetName(), id, flags);
            Notify("Walk With Me will direct this NPC's travel and rest.");
        }
        scanTimer = Settings::GetSingleton().Get().scanInterval;
    }

    void FormationController::UpdatePackageControl()
    {
        std::vector<RE::FormID> released;
        for (auto& [id, flags] : packageRegistrations) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
            if (!actor) {
                continue;
            }
            if (const auto* reason = PackageControlLoss(*actor, flags)) {
                logger::info("[PackageControl] Releasing {} ({:08X}): {}", actor->GetName(), id, reason);
                released.push_back(id);
            }
            else if (IsNFFFollower(*actor)) {
                flags = PackageControlFlags(true, true);
            }
            else if (flags == 1 && actor->IsPlayerTeammate()) {
                flags = PackageControlFlags(false, true);
            }
        }
        for (const auto id : released) {
            packageRegistrations.erase(id);
            explicitExclusions.insert(id);
            RemoveManaged(id);
        }
    }
} // namespace Wayfarer

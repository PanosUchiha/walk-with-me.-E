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
            return actor && !actor->IsDead() && !PackageControlSuspended(id);
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
                                 explicitExclusions.contains(id), IsDialogueFollower(actor), true);
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
        const bool directing = enabled && mode != FormationMode::kVanilla;
        if (add) {
            if (dialogueRegistrations.contains(id) || manualRegistrations.contains(id) ||
                packageRegistrations.contains(id) ||
                (managed.contains(id) && !explicitExclusions.contains(id))) {
                return;
            }
            if (!IsDialogueFollower(*actor)) {
                // Recruited through something that keeps no roster Walk With Me can read (a custom
                // follower's own framework): take them anyway, following their teammate flag.
                if (actor->IsPlayerTeammate() || IsNFFFollower(*actor)) {
                    if (const auto* reason = ImportCompanion(*actor)) {
                        Notify(reason);
                    }
                    else {
                        Notify(directing ? "Companion added to Walk With Me."
                                         : "Companion added. They join you once a travel style is chosen.");
                    }
                    scanTimer = Settings::GetSingleton().Get().scanInterval;
                    return;
                }
                Notify("This companion is not following you.");
                return;
            }
            if (!managed.contains(id) &&
                managed.size() >= static_cast<std::size_t>(Settings::GetSingleton().Get().maxFollowers)) {
                Notify("Walk With Me's companion limit has been reached.");
                return;
            }
            dialogueRegistrations.insert(id);
            explicitExclusions.erase(id);
            Notify(directing ? "Companion added to Walk With Me."
                             : "Companion added. They join you once a travel style is chosen.");
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

    // Taking control records a choice; whether the NPC can move right now (combat, a scene, a
    // wait order, Vanilla mode) is left to the ordinary movement guards, which pause and resume
    // them. So a recruited follower is accepted from anywhere and in any state.
    const char* FormationController::PackageControlStartRejection(RE::Actor& actor) const
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || actor.IsPlayerRef() || actor.IsDead() || actor.IsDisabled()) {
            return "Select a living NPC";
        }
        const bool recruited = actor.IsPlayerTeammate() || IsNFFFollower(actor);
        if (!recruited && (!actor.Is3DLoaded() || !InSameTravelSpace(actor, *player) ||
                           actor.GetPosition().GetDistance(player->GetPosition()) > 800.0F)) {
            return "Move closer to this NPC";
        }
        if (manualRegistrations.contains(actor.GetFormID())) {
            return "Controlled by an API integration";
        }
        if (actor.IsHostileToActor(player)) {
            return "This NPC is hostile";
        }
        if (const auto* reason = PackageControlRejection(
                1, false, false,
                actor.AsActorValueOwner()->GetActorValue(RE::ActorValue::kWaitingForPlayer))) {
            return reason;
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
            packageLossSince.erase(id);
            packageSeenFollowing.erase(id);
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
            if (const auto* reason = ImportCompanion(*actor)) {
                Notify(reason);
                return;
            }
            Notify(enabled && mode != FormationMode::kVanilla
                       ? "Walk With Me will direct this NPC's travel and rest."
                       : "Walk With Me will direct this NPC's travel and rest once a travel style is chosen.");
        }
        scanTimer = Settings::GetSingleton().Get().scanInterval;
    }

    const char* FormationController::ImportCompanion(RE::Actor& actor)
    {
        if (const auto* reason = PackageControlStartRejection(actor)) {
            return reason;
        }
        const auto id = actor.GetFormID();
        const auto flags = PackageControlFlags(IsNFFFollower(actor), actor.IsPlayerTeammate());
        packageRegistrations[id] = flags;
        packageLossSince.erase(id);
        packageSeenFollowing.insert(id);
        dialogueRegistrations.erase(id);
        explicitExclusions.erase(id);
        logger::info("[PackageControl] Player imported {} ({:08X}), recruitment tracking={}", actor.GetName(), id,
                     flags);
        return nullptr;
    }

    bool FormationController::PackageControlSuspended(RE::FormID id) const
    {
        const auto lost = packageLossSince.find(id);
        return lost != packageLossSince.end() && PackageControlPaused(lost->second, GetTickCount64());
    }

    // A dismissal, a framework re-slotting its roster or a quest scene that clears the teammate
    // flag pauses an import; it resumes by itself when the NPC follows again. Only the player's
    // Release (or death) ends it, so a companion once taken stays taken.
    void FormationController::UpdatePackageControl()
    {
        const auto now = GetTickCount64();
        std::vector<RE::FormID> released;
        for (auto& [id, flags] : packageRegistrations) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
            if (!actor) {
                continue;
            }
            if (actor->IsDead()) {
                logger::info("[PackageControl] Ending control of {} ({:08X}): dead", actor->GetName(), id);
                released.push_back(id);
                continue;
            }
            if (const auto* reason = PackageControlLoss(*actor, flags)) {
                auto [lost, first] =
                    packageLossSince.try_emplace(id, PackageControlLossStart(now, packageSeenFollowing.contains(id)));
                const bool paused = PackageControlPaused(lost->second, now);
                if (paused && packageSeenFollowing.erase(id) + (first ? 1 : 0) > 0) {
                    logger::info("[PackageControl] Pausing {} ({:08X}): {}; resumes when they follow again",
                                 actor->GetName(), id, reason);
                    RemoveManaged(id);
                }
                continue;
            }
            if (packageLossSince.erase(id) && !packageSeenFollowing.contains(id)) {
                logger::info("[PackageControl] Resuming {} ({:08X})", actor->GetName(), id);
            }
            packageSeenFollowing.insert(id);
            if (IsNFFFollower(*actor)) {
                flags = PackageControlFlags(true, true);
            }
            else if (flags == 1 && actor->IsPlayerTeammate()) {
                flags = PackageControlFlags(false, true);
            }
        }
        for (const auto id : released) {
            packageRegistrations.erase(id);
            packageLossSince.erase(id);
            packageSeenFollowing.erase(id);
            RemoveManaged(id);
        }
    }
} // namespace Wayfarer

#include "formation_controller.h"

namespace Wayfarer
{
    void FormationController::InitializeMovementGates()
    {
        movementGatesReady = true;
        auto* data = RE::TESDataHandler::GetSingleton();
        for (int slot = 0; slot < PARTY_CAPACITY; ++slot) {
            auto*& gate = movementGates[slot];
            gate = data ? data->LookupForm<RE::TESGlobal>(MovementGlobalLocal(slot),
                                                          Settings::GetSingleton().Get().pluginName)
                        : nullptr;
            if (gate) {
                gate->value = 0;
            }
            else {
                movementGatesReady = false;
            }
            for (const auto id : {PackageLocal(slot), SandboxPackageLocal(slot), SocialPackageLocal(slot),
                                  PosePackageLocal(slot), SeatPackageLocal(slot), GatherPackageLocal(slot)}) {
                auto* package =
                    data ? data->LookupForm<RE::TESPackage>(id, Settings::GetSingleton().Get().pluginName)
                         : nullptr;
                auto* item = package ? package->packConditions.head : nullptr;
                if (!item ||
                    item->data.functionData.function.get() !=
                        RE::FUNCTION_DATA::FunctionID::kGetGlobalValue ||
                    item->data.functionData.params[0] != gate || item->data.flags.isOR ||
                    item->data.flags.global || item->data.flags.usesAliases || item->data.flags.usePackData ||
                    item->data.flags.swapTarget ||
                    item->data.object.get() != RE::CONDITIONITEMOBJECT::kSelf ||
                    item->data.flags.opCode != RE::CONDITION_ITEM_DATA::OpCode::kEqualTo ||
                    item->data.comparisonValue.f != 1.0F) {
                    movementGatesReady = false;
                }
            }
        }
        if (!movementGatesReady) {
            logger::error(
                "[Handoff] Movement disabled: install the matching Wayfarer.esp and scripts with this DLL.");
        }
        else {
            logger::info(
                "[Handoff] Persistent aliases with conditional movement; NFF keeps catch-up; revision 1");
        }
    }

    bool FormationController::OwnsMovementPackage(const RE::Actor& actor, int slot) const
    {
        auto* current = actor.GetCurrentPackage();
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!current || !data || slot < 0 || slot >= PARTY_CAPACITY) {
            return false;
        }
        for (const auto local : {PackageLocal(slot), SandboxPackageLocal(slot), SocialPackageLocal(slot),
                                 PosePackageLocal(slot), SeatPackageLocal(slot), GatherPackageLocal(slot)}) {
            if (current ==
                data->LookupForm<RE::TESPackage>(local, Settings::GetSingleton().Get().pluginName)) {
                return true;
            }
        }
        return false;
    }

    void FormationController::SetMovementGate(ManagedFollower& f, RE::Actor* actor, bool active)
    {
        if (f.slot < 0 || f.slot >= PARTY_CAPACITY) {
            return;
        }
        auto* gate = movementGates[f.slot];
        if (!gate) {
            return;
        }
        // A recycled slot must never enable packages on the previous occupant.
        auto* alias = GetRefAlias(questCache, FollowerAlias(f.slot));
        auto* markerAlias = GetRefAlias(questCache, MarkerAlias(f.slot));
        auto* aliasActor = alias ? alias->GetReference() : nullptr;
        auto* aliasMarker = markerAlias ? markerAlias->GetReference() : nullptr;
        auto marker = markerPool[f.slot].get();
        active = CanGrantMovement(active, movementGatesReady, actor ? actor->GetFormID() : 0,
                                  aliasActor ? aliasActor->GetFormID() : 0, marker ? marker->GetFormID() : 0,
                                  aliasMarker ? aliasMarker->GetFormID() : 0);
        const float next = active ? 1.0F : 0.0F;
        if (gate->value == next) {
            return;
        }
        gate->value = next;
        TraceFollowerEvent(f, active ? "movement-permission granted"
                                     : "movement-permission released (alias retained)");
        // The engine's normal evaluation selects a package. Do not reset AI,
        // disturb another owner's current package, or evaluate during save load.
        if (!resettingGame && actor && actor->Is3DLoaded() && !actor->GetCurrentScene() &&
            !ShouldSuspendForDialogue() && (active || OwnsMovementPackage(*actor, f.slot))) {
            actor->EvaluatePackage(false, false);
        }
    }

    RE::Actor* FormationController::GetAliasFollower(std::uint32_t slot)
    {
        std::lock_guard lock(stateMutex);
        if (!movementGatesReady || loadReleaseTime > 0 || slot >= PARTY_CAPACITY) {
            return nullptr;
        }
        // Disabled/Vanilla, waiting, combat and unloaded actors keep membership;
        // their package gates are closed independently of this accessor.
        for (auto& [id, f] : managed) {
            if (static_cast<std::uint32_t>(f.slot) != slot || explicitExclusions.contains(id)) {
                continue;
            }
            auto actor = f.handle.get();
            if (actor && !actor->IsDead() && !actor->IsDisabled() &&
                (IsRegisteredFollower(id) || !CandidateRejection(*actor))) {
                return actor.get();
            }
        }
        return nullptr;
    }

    bool FormationController::ShouldSuspendAliasSync() const
    {
        std::lock_guard lock(stateMutex);
        return movementGatesReady && (resettingGame || loadReleaseTime > 0 || restoreAliasesPending);
    }

    void FormationController::RestoreAliasEnrollment()
    {
        if (!restoreAliasesPending) {
            return;
        }
        auto* quest = EnsureQuest();
        if (!quest || !quest->IsRunning()) {
            return;
        }
        for (int slot = 0; slot < Settings::GetSingleton().Get().maxFollowers; ++slot) {
            auto* alias = GetRefAlias(quest, FollowerAlias(slot));
            auto* ref = alias ? alias->GetReference() : nullptr;
            auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
            auto* markerAlias = GetRefAlias(quest, MarkerAlias(slot));
            auto* marker = markerAlias ? markerAlias->GetReference() : nullptr;
            if (!actor || actor->IsDead() || actor->IsDisabled() ||
                explicitExclusions.contains(actor->GetFormID()) ||
                (!IsRegisteredFollower(actor->GetFormID()) && CandidateRejection(*actor)) ||
                managed.contains(actor->GetFormID()) || FindAvailableSlot(slot) != slot || !marker ||
                marker->IsDeleted() || !marker->GetBaseObject() ||
                marker->GetBaseObject()->GetFormID() != 0x3B) {
                continue;
            }
            ManagedFollower follower;
            follower.handle = actor->GetHandle();
            follower.slot = slot;
            managed.emplace(actor->GetFormID(), std::move(follower));
            markerPool[slot] = marker->GetHandle();
        }
        restoreAliasesPending = false;
    }

    RE::TESObjectREFR* FormationController::GetAliasMarker(std::uint32_t slot)
    {
        std::lock_guard lock(stateMutex);
        return GetAliasFollower(slot) ? markerPool[slot].get().get() : nullptr;
    }

    bool FormationController::NFFAllowsMovement(RE::Actor& actor) const
    {
        if (!IsNFFFollower(actor)) {
            return true;
        }
        if (actor.GetCurrentScene()) {
            return false;
        }
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data) {
            return false;
        }
        // Explicit NFF errands can be requested while WWM's package is still
        // current. Observe their aliases without changing NFF quests or settings.
        // IDs verified against the local NFF ESP; missing quests are optional.
        for (const auto id : {0xC44F6u, 0xC9434u, 0xF84CDu, 0xFAC6Eu, 0x133C9Du, 0x15DE5Du, 0x257FA4u,
                              0x523EA0u, 0x537BE1u, 0x241AF5u}) {
            auto* quest = data->LookupForm<RE::TESQuest>(id, "nwsFollowerFramework.esp");
            if (!quest || !quest->IsRunning()) {
                continue;
            }
            for (auto* entry : quest->aliases) {
                auto* alias = entry ? skyrim_cast<RE::BGSRefAlias*>(entry) : nullptr;
                if (alias && alias->GetReference() == &actor) {
                    return false;
                }
            }
        }
        auto* current = actor.GetCurrentPackage();
        if (!current) {
            return false;
        }
        auto* source = current->GetFile(0);
        if (!source ||
            _stricmp(std::string_view(source->GetFilename()).data(), "nwsFollowerFramework.esp") != 0) {
            return true;
        }
        // Ordinary follow/default/automatic sandbox are shareable. Heal, loot,
        // flee, horse, home and task packages retain NFF control.
        for (const auto id : {0x47147Bu, 0x4FECA0u, 0x3A64A7u, 0x19664u}) {
            if (current == data->LookupForm<RE::TESPackage>(id, "nwsFollowerFramework.esp")) {
                return true;
            }
        }
        return false;
    }
} // namespace Wayfarer

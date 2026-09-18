#include "papyrus_bridge.h"
#include "hand_holding.h"

#include "formation_controller.h"

namespace
{
    constexpr std::string_view SCRIPT_NAME = "Wayfarer";
    bool ConsumeCompanionPush(RE::StaticFunctionTag*, RE::Actor* actor, std::int32_t ticket)
    {
        return Wayfarer::FormationController::GetSingleton().ConsumeCompanionPush(actor, ticket);
    }

    void SetDialogueManagement(RE::StaticFunctionTag*, RE::Actor* actor, bool add)
    {
        if (!actor) {
            return;
        }
        const auto handle = actor->GetHandle();
        SKSE::GetTaskInterface()->AddTask([handle, add] {
            if (auto target = handle.get()) {
                Wayfarer::FormationController::GetSingleton().SetDialogueManagement(target.get(), add);
            }
        });
    }

    bool RegisterFollower(RE::StaticFunctionTag*, RE::Actor* a_actor, std::int32_t a_preferredSlot)
    {
        return Wayfarer::FormationController::GetSingleton().RegisterFollower(a_actor, a_preferredSlot);
    }

    bool UnregisterFollower(RE::StaticFunctionTag*, RE::Actor* a_actor)
    {
        return a_actor &&
               Wayfarer::FormationController::GetSingleton().UnregisterFollower(a_actor->GetFormID());
    }

    bool ExcludeFollower(RE::StaticFunctionTag*, RE::Actor* a_actor)
    {
        return a_actor && Wayfarer::FormationController::GetSingleton().ExcludeFollower(a_actor->GetFormID());
    }

    bool IncludeFollower(RE::StaticFunctionTag*, RE::Actor* a_actor)
    {
        return a_actor && Wayfarer::FormationController::GetSingleton().IncludeFollower(a_actor->GetFormID());
    }

    bool IsManaged(RE::StaticFunctionTag*, RE::Actor* a_actor)
    {
        return a_actor && Wayfarer::FormationController::GetSingleton().IsManaged(a_actor->GetFormID());
    }

    void SetEnabled(RE::StaticFunctionTag*, bool a_enabled)
    {
        Wayfarer::FormationController::GetSingleton().SetEnabled(a_enabled);
    }

    bool GetEnabled(RE::StaticFunctionTag*)
    {
        return Wayfarer::FormationController::GetSingleton().IsEnabled();
    }

    void SetFormationMode(RE::StaticFunctionTag*, std::int32_t a_mode)
    {
        a_mode = std::clamp(a_mode, 0, 5);
        Wayfarer::FormationController::GetSingleton().SetMode(static_cast<Wayfarer::FormationMode>(a_mode));
    }

    std::int32_t GetFormationMode(RE::StaticFunctionTag*)
    {
        return static_cast<std::int32_t>(Wayfarer::FormationController::GetSingleton().GetMode());
    }

    std::int32_t GetManagedCount(RE::StaticFunctionTag*)
    {
        return static_cast<std::int32_t>(Wayfarer::FormationController::GetSingleton().GetManagedCount());
    }

    RE::Actor* GetSlotFollower(RE::StaticFunctionTag*, std::int32_t a_slot)
    {
        return a_slot < 0 ? nullptr
                          : Wayfarer::FormationController::GetSingleton().GetSlotFollower(
                                static_cast<std::uint32_t>(a_slot));
    }

    RE::TESObjectREFR* GetSlotMarker(RE::StaticFunctionTag*, std::int32_t a_slot)
    {
        return a_slot < 0 ? nullptr
                          : Wayfarer::FormationController::GetSingleton().GetSlotMarker(
                                static_cast<std::uint32_t>(a_slot));
    }
    RE::TESObjectREFR* GetRestSeat(RE::StaticFunctionTag*, std::int32_t slot)
    {
        return slot < 0 ? nullptr : Wayfarer::FormationController::GetSingleton().GetRestSeat(slot);
    }

    RE::TESObjectREFR* GetGatherMarker(RE::StaticFunctionTag*, std::int32_t slot)
    {
        return slot < 0 ? nullptr : Wayfarer::FormationController::GetSingleton().GetGatherMarker(slot);
    }
    RE::TESIdleForm* ConsumeRestIdle(RE::StaticFunctionTag*, std::int32_t slot)
    {
        return slot < 0 ? nullptr : Wayfarer::FormationController::GetSingleton().ConsumeRestIdle(slot);
    }
    void ReportRestIdle(RE::StaticFunctionTag*, std::int32_t slot, RE::Actor* actor, RE::TESIdleForm* idle,
                        bool accepted)
    {
        if (slot >= 0) {
            Wayfarer::FormationController::GetSingleton().ReportRestIdle(slot, actor, idle, accepted);
        }
    }
    RE::Actor* GetSocialTarget(RE::StaticFunctionTag*, std::int32_t slot)
    {
        return slot < 0 ? nullptr : Wayfarer::FormationController::GetSingleton().GetSocialTarget(slot);
    }
    bool HasRestActivities(RE::StaticFunctionTag*)
    {
        return Wayfarer::FormationController::GetSingleton().HasRestActivities();
    }
    bool HasSocialGroup(RE::StaticFunctionTag*)
    {
        return Wayfarer::FormationController::GetSingleton().HasSocialGroup();
    }
    bool ShouldSuspendForDialogue(RE::StaticFunctionTag*)
    {
        return Wayfarer::FormationController::GetSingleton().ShouldSuspendForDialogue();
    }
    void ReportAliasSync(RE::StaticFunctionTag*, bool busy)
    {
        Wayfarer::FormationController::GetSingleton().ReportAliasSync(busy);
    }
    void ReportSocialIdle(RE::StaticFunctionTag*, RE::Actor* actor, bool accepted)
    {
        Wayfarer::FormationController::GetSingleton().ReportSocialIdle(actor, accepted);
    }
    RE::TESIdleForm* ConsumeSocialIdle(RE::StaticFunctionTag*, std::int32_t slot)
    {
        return slot < 0 ? nullptr : Wayfarer::FormationController::GetSingleton().ConsumeSocialIdle(slot);
    }

    void ReloadSettings(RE::StaticFunctionTag*)
    {
        Wayfarer::FormationController::GetSingleton().ReloadSettings();
    }
    std::int32_t TetherStepStatus(RE::StaticFunctionTag*, std::int32_t ticket, std::int32_t operation,
                                  RE::Actor* player, RE::Actor* follower)
    {
        return Wayfarer::HandHolding::TetherStepStatus(ticket, operation, player, follower);
    }
    std::int32_t GetTetherHandoffTicket(RE::StaticFunctionTag*, std::int32_t slot)
    {
        return slot < 0 ? 0 : Wayfarer::FormationController::GetSingleton().GetTetherHandoffTicket(slot);
    }
    void ConfirmTetherHandoff(RE::StaticFunctionTag*, std::int32_t slot, std::int32_t ticket)
    {
        if (slot >= 0) {
            Wayfarer::FormationController::GetSingleton().ConfirmTetherHandoff(slot, ticket);
        }
    }
} // namespace

namespace Wayfarer::Papyrus
{
    bool Register(RE::BSScript::IVirtualMachine* a_vm)
    {
        if (!a_vm) {
            return false;
        }
        a_vm->RegisterFunction("RegisterFollower", SCRIPT_NAME, RegisterFollower);
        a_vm->RegisterFunction("ConsumePush", "WayfarerRecovery", ConsumeCompanionPush);
        a_vm->RegisterFunction("SetDialogueManagement", SCRIPT_NAME, SetDialogueManagement);
        a_vm->RegisterFunction("UnregisterFollower", SCRIPT_NAME, UnregisterFollower);
        a_vm->RegisterFunction("ExcludeFollower", SCRIPT_NAME, ExcludeFollower);
        a_vm->RegisterFunction("IncludeFollower", SCRIPT_NAME, IncludeFollower);
        a_vm->RegisterFunction("IsManaged", SCRIPT_NAME, IsManaged);
        a_vm->RegisterFunction("SetEnabled", SCRIPT_NAME, SetEnabled);
        a_vm->RegisterFunction("GetEnabled", SCRIPT_NAME, GetEnabled);
        a_vm->RegisterFunction("SetFormationMode", SCRIPT_NAME, SetFormationMode);
        a_vm->RegisterFunction("GetFormationMode", SCRIPT_NAME, GetFormationMode);
        a_vm->RegisterFunction("GetManagedCount", SCRIPT_NAME, GetManagedCount);
        a_vm->RegisterFunction("GetSlotFollower", SCRIPT_NAME, GetSlotFollower);
        a_vm->RegisterFunction("GetSlotMarker", SCRIPT_NAME, GetSlotMarker);
        a_vm->RegisterFunction("GetRestSeat", SCRIPT_NAME, GetRestSeat);
        a_vm->RegisterFunction("GetGatherMarker", SCRIPT_NAME, GetGatherMarker);
        a_vm->RegisterFunction("ConsumeRestIdle", SCRIPT_NAME, ConsumeRestIdle);
        a_vm->RegisterFunction("ReportRestIdle", SCRIPT_NAME, ReportRestIdle);
        a_vm->RegisterFunction("GetSocialTarget", SCRIPT_NAME, GetSocialTarget);
        a_vm->RegisterFunction("HasRestActivities", SCRIPT_NAME, HasRestActivities);
        a_vm->RegisterFunction("HasSocialGroup", SCRIPT_NAME, HasSocialGroup);
        a_vm->RegisterFunction("ShouldSuspendForDialogue", SCRIPT_NAME, ShouldSuspendForDialogue);
        a_vm->RegisterFunction("ReportAliasSync", SCRIPT_NAME, ReportAliasSync);
        a_vm->RegisterFunction("ReportSocialIdle", SCRIPT_NAME, ReportSocialIdle);
        a_vm->RegisterFunction("ConsumeSocialIdle", SCRIPT_NAME, ConsumeSocialIdle);
        a_vm->RegisterFunction("ReloadSettings", SCRIPT_NAME, ReloadSettings);
        a_vm->RegisterFunction("TetherStepStatus", SCRIPT_NAME, TetherStepStatus);
        a_vm->RegisterFunction("GetTetherHandoffTicket", SCRIPT_NAME, GetTetherHandoffTicket);
        a_vm->RegisterFunction("ConfirmTetherHandoff", SCRIPT_NAME, ConfirmTetherHandoff);
        logger::info("[Papyrus] Wayfarer native API registered.");
        return true;
    }
} // namespace Wayfarer::Papyrus

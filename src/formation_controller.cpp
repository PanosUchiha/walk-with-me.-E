#include "formation_controller.h"
#include "RE/C/CrosshairPickData.h"
#include "command_gesture.h"
#include "custom_followers.h"
#include "engine_compatibility.h"
#include "follower_eligibility.h"
#include "gesture_head_tracking.h"
#include "hand_hold_locomotion.h"
#include "hand_holding.h"
#include "package_control.h"
#include "travel_space.h"

#include "RE/B/BGSPackageDataBool.h"
#include "RE/B/BGSPackageDataLocation.h"
#include "RE/B/BGSScene.h"
#include "RE/B/BGSSceneAction.h"
#include "RE/B/BGSSceneActionDialogue.h"
#include "RE/E/ExtraSayToTopicInfo.h"
#include "RE/M/MenuTopicManager.h"
#include "RE/P/PackageLocation.h"
#include "RE/T/TESCustomPackageData.h"
#include "config.h"
#include "travel_speed.h"

namespace
{
    constexpr RE::FormID CURRENT_FOLLOWER_FACTION = 0x0005C84E;

    constexpr RE::FormID WAYFARER_QUEST_LOCAL_ID = 0x805;
    float Distance2D(RE::NiPoint3 a_left, RE::NiPoint3 a_right)
    {
        const float x = a_left.x - a_right.x;
        const float y = a_left.y - a_right.y;
        return std::sqrt(x * x + y * y);
    }

    Wayfarer::RestContext ContextFor(RE::Actor& actor)
    {
        using Wayfarer::RestContext;
        bool town = false, inn = false, dungeon = false;
        auto* location = actor.GetCurrentLocation();
        for (int depth = 0; location && depth < 32; ++depth, location = location->parentLoc) {
            auto has = [&](RE::FormID id) {
                auto* key = RE::TESForm::LookupByID<RE::BGSKeyword>(id);
                return key && location->HasKeyword(key);
            };
            inn |= has(0x1CB87);
            for (auto id : {0x130DBu, 0x130E2u, 0x18EF1u, 0x130EFu, 0x100819u, 0x130F2u, 0x130F0u, 0x130ECu,
                            0x130EBu, 0x130DFu}) {
                dungeon |= has(id);
            }
            for (auto id : {0x13168u, 0x13167u, 0x13166u, 0x18EF0u, 0x130E9u, 0x868E2u}) {
                town |= has(id);
            }
        }
        if (dungeon) {
            return RestContext::Dungeon;
        }
        if (inn) {
            return RestContext::Tavern;
        }
        if (!actor.GetParentCell() || actor.GetParentCell()->IsInteriorCell()) {
            return RestContext::Interior;
        }
        return town ? RestContext::Settlement : RestContext::Wilderness;
    }
    RE::FormID SocialSpace(RE::Actor& actor)
    {
        auto* cell = actor.GetParentCell();
        if (!cell) {
            return 0;
        }
        if (!cell->IsInteriorCell() && actor.GetWorldspace()) {
            return actor.GetWorldspace()->GetFormID();
        }
        return cell->GetFormID();
    }
    std::string PersonalityKey(RE::Actor& actor)
    {
        auto* base = actor.GetActorBase();
        auto* file = base ? base->GetFile(0) : nullptr;
        std::string source{file ? file->GetFilename() : "runtime"};
        std::transform(source.begin(), source.end(), source.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return fmt::format("{}|{:06X}", source,
                           base ? base->GetFormID() & (file && file->IsLight() ? 0xFFF : 0xFFFFFF) : 0);
    }
    Wayfarer::Personality PersonalityFor(RE::Actor& actor)
    {
        const auto& settings = Wayfarer::Settings::GetSingleton().Get();
        const auto key = PersonalityKey(actor);
        const auto found = settings.personalityOverrides.find(key);
        return Wayfarer::ResolvePersonality(
            key, found == settings.personalityOverrides.end() ? -1 : found->second, settings.personalities);
    }
    std::uint32_t TravelIdentity(RE::Actor& actor)
    {
        auto* file = actor.GetFile(0);
        const auto local = actor.GetFormID() & (file && file->IsLight() ? 0xFFF : 0xFFFFFF);
        std::string source{file ? file->GetFilename() : "runtime"};
        std::transform(source.begin(), source.end(), source.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return Wayfarer::IdentityHash(fmt::format("{}|{}|{:06X}", PersonalityKey(actor), source, local));
    }
    RE::BGSPackageDataBool* BoolInput(RE::TESPackage* package, std::uint8_t uid)
    {
        auto* custom = package ? skyrim_cast<RE::TESCustomPackageData*>(package->data) : nullptr;
        if (custom && custom->data.data && custom->data.uids) {
            for (std::uint16_t i = 0; i < custom->data.dataSize; ++i) {
                if (custom->data.uids[i] == uid) {
                    return skyrim_cast<RE::BGSPackageDataBool*>(custom->data.data[i]);
                }
            }
        }
        return nullptr;
    }

    bool IsSpeaking(RE::Actor& actor)
    {
        RE::TESConditionItem condition;
        condition.data.functionData.function = RE::FUNCTION_DATA::FunctionID::kIsTalking;
        condition.data.comparisonValue.f = 1;
        condition.data.flags.opCode = RE::CONDITION_ITEM_DATA::OpCode::kEqualTo;
        condition.data.object = RE::CONDITIONITEMOBJECT::kSelf;
        RE::ConditionCheckParams params{&actor, &actor};
        return condition.IsTrue(params);
    }

    std::string_view ModeName(Wayfarer::FormationMode a_mode)
    {
        switch (a_mode) {
        case Wayfarer::FormationMode::kVanilla:
            return "Vanilla";
        case Wayfarer::FormationMode::kSandbox:
            return "Relax";
        case Wayfarer::FormationMode::kLead:
            return "Lead";
        case Wayfarer::FormationMode::kRear:
            return "Rear";
        case Wayfarer::FormationMode::kCompanion:
            return "Companion";
        default:
            return "Natural";
        }
    }

    std::int32_t ProcedureOf(const RE::TESPackage* a_package)
    {
        return a_package ? static_cast<std::int32_t>(a_package->procedureType.get()) : -1;
    }

    std::string DescribePackage(const RE::TESPackage* a_package)
    {
        if (!a_package) {
            return "none";
        }
        return fmt::format("{:08X} packType={} procedure={} source={}", a_package->GetFormID(),
                           static_cast<std::int32_t>(a_package->packData.packType.get()),
                           ProcedureOf(a_package),
                           a_package->GetFile(0) ? a_package->GetFile(0)->GetFilename() : "runtime");
    }

    bool IsFollowerTravelPackage(const RE::TESPackage* package)
    {
        if (!package) {
            return false;
        }

        switch (static_cast<int>(package->procedureType.get())) {
        case 6:
        case 43:
        case 8:
        case 9:
        case 25:
        case 46:
            return true;
        default:
            return false;
        }
    }

} // namespace

namespace Wayfarer
{
    bool FormationController::IsCustomFollower(RE::Actor& actor)
    {
        auto* base = actor.GetActorBase();
        auto* file = base ? base->GetFile(0) : nullptr;
        std::string plugin{file ? file->GetFilename() : ""};
        std::transform(plugin.begin(), plugin.end(), plugin.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(CURRENT_FOLLOWER_FACTION);
        return Wayfarer::UsesCustomFollowerAI(plugin, base ? base->GetFormID() & 0xFFFFFF : 0,
                                              actor.IsPlayerTeammate(),
                                              Engine::FactionRank(actor, faction) >= 0);
    }

    const char* FormationController::RecruitmentRejectionFor(RE::Actor& actor, bool requireTeammate)
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* base = actor.GetActorBase();
        auto* file = base ? base->GetFile(0) : nullptr;
        std::string plugin{file ? file->GetFilename() : ""};
        std::transform(plugin.begin(), plugin.end(), plugin.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(CURRENT_FOLLOWER_FACTION);
        return RecruitmentRejection(
            {data && data->LookupModByName("nwsFollowerFramework.esp"), IsNFFFollower(actor),
             KnownCustomFollower(plugin, base ? base->GetFormID() & 0xFFFFFF : 0), actor.IsPlayerTeammate(),
             Engine::FactionRank(actor, faction),
             actor.AsActorValueOwner()->GetActorValue(RE::ActorValue::kWaitingForPlayer)},
            requireTeammate);
    }

    FormationController& FormationController::GetSingleton()
    {
        static FormationController singleton;
        return singleton;
    }

    void FormationController::Initialize()
    {
        std::lock_guard lock(stateMutex);
        sandbox.Reset();
        Settings::GetSingleton().Load();
        const auto& settings = Settings::GetSingleton().Get();
        enabled = settings.enabled;
        mode = settings.mode;
        initialized = true;
        companionPush.ticket = static_cast<std::int32_t>(GetTickCount64() & 0x3fffffff);
        InitializeVictoryEvents();
        logger::info("[Following] Baseline r9: September 19 r3 movement restored; "
                     "victory r5 reactions and separate command gestures retained");
        logger::info(
            "[Enrollment] NFF roster guard r1: active slots authoritative; teammate-only fallback disabled");
        logger::info("[PackageControl] Popup r1: explicit per-save travel and rest control");
        scanTimer = settings.scanInterval;
        logger::info("[Controller] Initialized in {} mode; enabled={}", ModeName(mode), enabled);
    }

    void FormationController::Update(float dt)
    {
        std::lock_guard lock(stateMutex);
        if (!initialized || !std::isfinite(dt) || dt <= 0.0F || dt > 1.0F) {
            return;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->Is3DLoaded() || !player->GetParentCell()) {
            ResetVictoryCelebration(false);
            ReleaseAll();
            sandbox.Reset();
            intent.Reset();
            return;
        }
        UpdateFollowerDialogue(dt);
        UpdateVictoryCleanup();
        UpdateCompanionPush();
        if (recallRequest.pending) {
            auto* ui = RE::UI::GetSingleton();
            const bool menuOpen =
                !ui || ui->GameIsPaused() || Menu::HasBlockingWindow() || ShouldSuspendForDialogue();
            const auto dispatch = recallRequest.Poll(generation, GetTickCount64(), menuOpen,
                                                     loadReleaseTime <= 0 && !RecallPlayerRejection(*player));
            if (dispatch == RecallDispatch::Run) {
                RecallParty(*player);
            }
            else if (dispatch == RecallDispatch::Cancel) {
                Notify("Party teleport cancelled: wait until you are ready to travel.");
            }
        }

        if (mode == FormationMode::kVanilla && loadReleaseTime <= 0) {
            ResetVictoryCelebration();
            return;
        }

        if (ShouldSuspendForDialogue()) {
            ResetVictoryCelebration();
            RestoreTravelBanter();
            social.ClearGestures();
            if (mode != FormationMode::kVanilla) {
                UpdateConversationAwareness(*player, dt);
            }
            LogGlobalDiagnostic("dialogue protected; AI updates suspended");
            return;
        }

        if (auto* ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
            return;
        }
        EndConversationAwareness();
        const auto& settings = Settings::GetSingleton().Get();
        wakeTimer += dt;
        if (wakeTimer >= 2.0F) {
            wakeTimer = 0.0F;
            WakeQuest();
        }
        if (loadReleaseTime > 0.0F) {
            loadReleaseTime -= dt;
            ResetVictoryCelebration(false);
            ReleaseAll();
            return;
        }
        const auto pos = player->GetPosition();
        const bool spaceChanged = lastCell && (lastWorld != player->GetWorldspace() ||
                                               ((lastInterior || player->GetParentCell()->IsInteriorCell()) &&
                                                lastCell != player->GetParentCell()));
        const bool jumped = havePlayerPosition && Distance2D(pos, lastPlayerPosition) > 1000.0F;
        if (spaceChanged || jumped) {
            ResetVictoryCelebration();
        }
        if ((spaceChanged || jumped) && !IsRelaxing()) {
            ReleaseAll();
            sandbox.Reset();
            intent.Reset();
        }
        lastCell = player->GetParentCell();
        lastWorld = player->GetWorldspace();
        lastInterior = lastCell && lastCell->IsInteriorCell();
        lastPlayerPosition = pos;
        havePlayerPosition = true;

        if (enabled) {
            scanTimer += dt;
            if (scanTimer >= settings.scanInterval) {
                scanTimer = 0.0F;
                ScanFollowers(player);
            }
        }
        UpdateVictoryCelebration(*player, dt);
        if (!victoryReactions.empty()) {
            ReleaseAll();
            sandbox.Reset();
            intent.Reset();
            return;
        }
        if (!enabled || (!IsRelaxing() && GlobalRejection(*player))) {
            LogGlobalDiagnostic(enabled ? GlobalRejection(*player) : "controller disabled");
            ReleaseAll();
            sandbox.Reset();
            intent.Reset();
            return;
        }
        RE::NiPoint3 velocity{};
        player->GetLinearVelocity(velocity);
        if (regroup.active) {
            intent.MaintainOrder();
        }
        intent.Update({pos.x, pos.y}, {velocity.x, velocity.y}, dt, settings.travel);
        bool gathered = !managed.empty();
        if (regroup.active) {
            for (const auto& [id, f] : managed) {
                if (auto actor = f.handle.get(); actor && UsabilityRejection(*actor, *player).empty()) {
                    const auto p = actor->GetPosition();
                    if (!f.goal.active || Length({p.x - f.goal.goal.x, p.y - f.goal.goal.y}) >
                                              settings.travel.arrivalRadius + 100) {
                        gathered = false;
                    }
                }
            }
        }
        if (regroup.Tick(dt, intent.IsMoving(), gathered)) {
            sandbox.Reset();
        }
        auto restTuning = settings.sandbox;
        if (regroup.active) {
            restTuning.automatic = false;
        }
        sandbox.Update({pos.x, pos.y}, Length({velocity.x, velocity.y}), dt,
                       order == PartyOrder::kRoam || mode == FormationMode::kSandbox,
                       settings.travel.idleRelease, restTuning);
        SetSandboxFlag(sandbox.active);
        const bool handEnabled = mode == FormationMode::kCompanion && settings.handHolding.enabled &&
                                 !settings.handHolding.partner.empty();
        if (!sandbox.active && !intent.CanTravel() && !handEnabled) {
            ReleaseAll();
            LogGlobalDiagnostic("waiting for sustained movement");
            return;
        }
        LogGlobalDiagnostic(nullptr);
        UpdateBanterMotion(*player);
        updateTimer += dt;
        traceTimer += dt;
        if (updateTimer < settings.updateInterval) {
            return;
        }
        const float elapsed = updateTimer;
        updateTimer = 0.0F;
        traceDue = traceTimer >= 1.0F;
        if (traceDue) {
            traceTimer = 0.0F;
        }
        UpdateTravelBanter(*player, elapsed);
        handsBackCooldown = std::max(0.0F, handsBackCooldown - elapsed);
        activityDispatchCooldown = std::max(0.0F, activityDispatchCooldown - elapsed);
        navigation.Collect(*player);
        UpdateFollowerCatchup(*player, elapsed);
        UpdateTravelRoles(*player, elapsed);
        UpdateLookout(*player, elapsed);

        for (int slot = 0; slot < PARTY_CAPACITY; ++slot) {
            for (auto& [id, follower] : managed) {
                if (follower.slot != slot) {
                    continue;
                }
                auto actor = follower.handle.get();
                if (!actor) {
                    ReleaseTravel(follower, nullptr);
                    continue;
                }
                if (const auto reason = UsabilityRejection(*actor, *player); !reason.empty()) {
                    LogDiagnostic(id, actor->GetName(), reason.c_str());
                    ReleaseTravel(follower, actor.get());
                    continue;
                }

                if (HandHolding::OwnsMovement(id)) {
                    if (follower.travelActive || follower.sandboxActive) {
                        ReleaseTravel(follower, actor.get(), true);
                    }
                    continue;
                }
                if (IsRelaxing() && follower.anchorSet && (!actor->Is3DLoaded() || actor->IsInCombat())) {
                    EndRestPose(follower, actor.get());
                    follower.travelActive = follower.sandboxActive = true;
                    continue;
                }
                if (sandbox.active) {
                    ApplySandbox(follower, *actor, *player, elapsed);
                }
                else if (HandHolding::TravelRequested(intent.CanTravel(), mode == FormationMode::kCompanion,
                                                      settings.handHolding, id,
                                                      HandHolding::GetApproach().actor)) {
                    ApplyTravel(follower, *actor, *player, elapsed);
                }
                else if (follower.travelActive) {
                    ReleaseTravel(follower, actor.get());
                }
            }
        }
        UpdateSocial(elapsed);
    }

    void FormationController::UpdateHandHolding(float dt)
    {
        std::lock_guard lock(stateMutex);
        if (!std::isfinite(dt) || dt <= 0 || dt > 1) {
            return;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        const bool paused = ui && ui->GameIsPaused();
        const bool permitted = initialized && enabled && mode == FormationMode::kCompanion &&
                               !sandbox.active && loadReleaseTime <= 0 && !ShouldSuspendForDialogue() &&
                               player && player->Is3DLoaded() && player->GetParentCell() &&
                               (!ui || (!ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) &&
                                        !ui->IsMenuOpen(RE::MainMenu::MENU_NAME)));
        const auto& tuning = Settings::GetSingleton().Get().handHolding;
        if (!HandHolding::BeginFrame(dt, permitted, paused, tuning)) {
            return;
        }
        RE::NiPointer<RE::Actor> selected;

        for (const auto& [id, follower] : managed) {
            auto actor = follower.handle.get();
            if (!actor || explicitExclusions.contains(id) || follower.recoveryReleasePending ||
                follower.goal.recoveryTime > 0 || follower.sandboxActive ||
                !InSameTravelSpace(*actor, *player) || !UsabilityRejection(*actor, *player).empty()) {
                continue;
            }
            if (HandHolding::ReferenceKey(*actor) == tuning.partner) {
                selected = actor;
                break;
            }
        }
        HandHolding::UpdatePartner(*player, selected.get(), dt, navigation);
        if (!selected) {
            return;
        }
        auto found = managed.find(selected->GetFormID());
        if (found == managed.end()) {
            return;
        }
        auto& follower = found->second;
        const auto ticket = HandHolding::HandoffTicket(found->first);
        // Script cleanup and the engine's package transition can finish in
        // different frames. Poll only AFTER the script's ticketed final ack;
        // an empty alias alone cannot bypass in-flight ClearKeepOffset work.
        if (HandHolding::HandoffCleanupFinished(found->first, ticket)) {
            ConfirmTetherHandoff(static_cast<std::uint32_t>(follower.slot), ticket);
        }
        if (!ticket || ticket == follower.requestedHandTicket) {
            return;
        }
        auto* quest = EnsureQuest();
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
        if (!quest || !quest->IsRunning() || !policy) {
            return;
        }
        const auto handle = policy->GetHandleForObject(RE::FormType::Quest, quest);
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result;
        // Request priority only. Alias mutation remains inside the serialized
        // script sweep, so it cannot race an older ClearKeepOffset call.
        if (vm->DispatchMethodCall(
                handle, RE::BSFixedString("WayfarerQuestScript"), RE::BSFixedString("RequestTetherHandoff"),
                RE::MakeFunctionArguments(static_cast<std::int32_t>(follower.slot), std::int32_t{ticket}),
                result)) {
            follower.requestedHandTicket = ticket;
            logger::info("[HandHolding/TETHER] Session {} priority alias handoff requested for slot {}",
                         ticket, follower.slot);
        }
    }

    std::int32_t FormationController::GetTetherHandoffTicket(std::uint32_t slot)
    {
        std::lock_guard lock(stateMutex);
        for (const auto& [id, f] : managed) {
            if (static_cast<std::uint32_t>(f.slot) == slot) {
                return HandHolding::HandoffTicket(id);
            }
        }
        return 0;
    }

    void FormationController::ConfirmTetherHandoff(std::uint32_t slot, std::int32_t ticket)
    {
        std::lock_guard lock(stateMutex);
        if (!ticket || ShouldSuspendForDialogue()) {
            return;
        }
        for (const auto& [id, f] : managed) {
            if (static_cast<std::uint32_t>(f.slot) == slot) {
                auto actor = f.handle.get();
                auto* alias = GetRefAlias(EnsureQuest(), FollowerAlias(slot));
                if (!actor || !alias) {
                    return;
                }
                auto* package = actor->GetCurrentPackage();
                auto* source = package ? package->GetFile(0) : nullptr;
                const bool own = source && std::string_view(source->GetFilename()) ==
                                               Settings::GetSingleton().Get().pluginName;
                HandHolding::ConfirmHandoff(id, ticket, !alias->GetReference(), !own);
                return;
            }
        }
    }

    void FormationController::ResetForGameLoad()
    {
        std::lock_guard lock(stateMutex);
        ResetVictoryCelebration(false);
        aliasSyncBusy = true;
        ReleaseAll();
        dialogueGuard = {};
        regroup.Reset();
        sandbox.Reset();
        ++generation;
        recallRequest = {};
        companionPush.Reset();
        order = PartyOrder::kTravel;
        mode = Settings::GetSingleton().Get().mode;
        managed.clear();
        markerPool = {};
        seatPool = {};
        gatherPool = {};
        roleTimer = 1.0F;
        roleCooldown = 0;
        handsBackCooldown = 45.0F;
        naturalPatternReady = false;
        travelPartyCount = 0;
        navigation = TravelNavigation{};
        manualRegistrations.clear();
        dialogueRosterTimer = 0.0F;
        diagnosticReasons.clear();
        lastGlobalReason.clear();
        lastScanSummary.clear();
        questCache = nullptr;
        questWarned = false;
        intent.Reset();
        lastCell = nullptr;
        lastWorld = nullptr;
        havePlayerPosition = false;
        scanTimer = Settings::GetSingleton().Get().scanInterval;
        updateTimer = traceTimer = 0.0F;
        wakeTimer = 2.0F;

        loadReleaseTime = 1.5F;
        if (restoredRelax) {
            mode = FormationMode::kSandbox;
            for (auto [id, slot] : restoredRelaxSlots) {
                auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
                auto* alias = GetRefAlias(EnsureQuest(), MarkerAlias(slot));
                auto* marker = alias ? alias->GetReference() : nullptr;
                if (!actor || !marker || !marker->GetBaseObject() ||
                    marker->GetBaseObject()->GetFormID() != 0x3B || explicitExclusions.contains(id) ||
                    slot >= Settings::GetSingleton().Get().maxFollowers) {
                    continue;
                }
                ManagedFollower follower;
                follower.handle = actor->GetHandle();
                follower.slot = slot;
                follower.anchorSet = true;
                managed.emplace(id, std::move(follower));
                markerPool[slot] = marker->GetHandle();
            }
            restoredRelax = false;
            restoredRelaxSlots.clear();
        }
        if (restoredVanilla) {
            mode = FormationMode::kVanilla;
            restoredVanilla = false;
            managed.clear();
        }
        logger::info("[Controller] Travel state reset; saved marker aliases will be reused.");
    }

    void FormationController::SetEnabled(bool a_enabled, bool a_notify)
    {
        std::lock_guard lock(stateMutex);
        enabled = a_enabled;
        regroup.Reset();
        if (!enabled) {
            ResetVictoryCelebration();
            ReleaseAll();
        }
        else {
            scanTimer = Settings::GetSingleton().Get().scanInterval;
            intent.Reset();
        }
        logger::info("[Controller] Enabled={}", enabled);
        if (a_notify) {
            Notify(enabled ? "Walk With Me enabled" : "Walk With Me disabled");
        }
    }

    bool FormationController::IsEnabled() const noexcept
    {
        std::lock_guard lock(stateMutex);
        return enabled;
    }

    void FormationController::SetMode(FormationMode a_mode, bool a_notify)
    {
        std::lock_guard lock(stateMutex);
        ResetVictoryCelebration();
        if (mode != a_mode || sandbox.active || a_mode != FormationMode::kSandbox) {
            naturalPatternReady = false;
            ReleaseAll();
            sandbox.Reset();
            roleTimer = 1.0F;
            roleCooldown = 0;
            for (auto& [id, f] : managed) {
                f.anchorSet = false;
                f.restAnchorZSet = false;
                f.role = -1;
            }
        }
        order = PartyOrder::kTravel;
        mode = static_cast<FormationMode>(std::clamp(static_cast<int>(a_mode), 0, 5));
        if (mode == FormationMode::kVanilla) {
            regroup.Reset();
            intent.Reset();
            managed.clear();
            scanTimer = Settings::GetSingleton().Get().scanInterval;
            logger::info("[Controller] Vanilla: returned travel and rest to follower AI");
            return;
        }
        if (mode == FormationMode::kSandbox) {
            regroup.Reset();
        }
        else {
            regroup.Begin();
        }
        for (auto& [id, follower] : managed) {
            follower.goal.sinceCommit = 100.0F;
        }
        if (!intent.IsMoving()) {
            if (auto* player = RE::PlayerCharacter::GetSingleton(); player && player->GetParentCell()) {
                auto pos = player->GetPosition();
                intent.BeginOrder({pos.x, pos.y}, ForwardFromYaw(player->GetAngle().z));
            }
        }
        logger::info("[Controller] Formation mode={}", ModeName(mode));
        (void)a_notify;
    }

    void FormationController::CycleMode()
    {
        std::lock_guard lock(stateMutex);
        order = PartyOrder::kTravel;
        SetMode(static_cast<FormationMode>((static_cast<int>(mode) + 1) % 6));
    }

    FormationMode FormationController::GetMode() const noexcept
    {
        std::lock_guard lock(stateMutex);
        return mode;
    }

    void FormationController::ReloadSettings(bool a_notify)
    {
        std::lock_guard lock(stateMutex);
        ReleaseAll();
        sandbox.Reset();
        Settings::GetSingleton().Load();
        naturalPatternReady = false;
        roleTimer = 1;
        roleCooldown = 0;
        const auto& settings = Settings::GetSingleton().Get();
        mode = settings.mode;
        SetEnabled(settings.enabled, false);
        intent.Reset();
        scanTimer = settings.scanInterval;
        diagnosticReasons.clear();
        std::vector<RE::FormID> excess;
        for (const auto& [id, follower] : managed) {
            if (follower.slot >= settings.maxFollowers) {
                excess.push_back(id);
            }
        }
        for (auto id : excess) {
            RemoveManaged(id);
        }
        if (a_notify) {
            Notify("Walk With Me settings reloaded");
        }
    }

    bool FormationController::RegisterFollower(RE::Actor* a_actor, int a_preferredSlot)
    {
        std::lock_guard lock(stateMutex);
        return a_actor && RegisterFollower(a_actor->GetFormID(), a_preferredSlot);
    }

    bool FormationController::RegisterFollower(RE::FormID a_formID, int a_preferredSlot)
    {
        std::lock_guard lock(stateMutex);
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_formID);
        if (!actor || actor->IsPlayerRef()) {
            return false;
        }
        manualRegistrations[a_formID] = a_preferredSlot;
        packageRegistrations.erase(a_formID);
        explicitExclusions.erase(a_formID);
        scanTimer = Settings::GetSingleton().Get().scanInterval;
        logger::info("[Controller] Manual registration {:08X}, preferred slot {}", a_formID, a_preferredSlot);
        return true;
    }

    bool FormationController::UnregisterFollower(RE::FormID a_formID)
    {
        std::lock_guard lock(stateMutex);
        const bool removed = manualRegistrations.erase(a_formID) > 0;
        RemoveManaged(a_formID);
        return removed;
    }

    bool FormationController::ExcludeFollower(RE::FormID a_formID)
    {
        std::lock_guard lock(stateMutex);
        if (!RE::TESForm::LookupByID<RE::Actor>(a_formID)) {
            return false;
        }
        explicitExclusions.insert(a_formID);
        manualRegistrations.erase(a_formID);
        dialogueRegistrations.erase(a_formID);
        packageRegistrations.erase(a_formID);
        RemoveManaged(a_formID);
        logger::info("[Controller] Explicit exclusion {:08X}", a_formID);
        return true;
    }

    bool FormationController::IncludeFollower(RE::FormID a_formID)
    {
        std::lock_guard lock(stateMutex);
        const bool removed = explicitExclusions.erase(a_formID) > 0;
        scanTimer = Settings::GetSingleton().Get().scanInterval;
        return removed;
    }

    bool FormationController::IsManaged(RE::FormID a_formID) const
    {
        std::lock_guard lock(stateMutex);
        return managed.contains(a_formID);
    }

    std::uint32_t FormationController::GetManagedCount() const noexcept
    {
        std::lock_guard lock(stateMutex);
        return static_cast<std::uint32_t>(managed.size());
    }

    void FormationController::ScanFollowers(RE::PlayerCharacter* a_player)
    {
        if (!a_player) {
            return;
        }

        const auto* followerFaction = RE::TESForm::LookupByID<RE::TESFaction>(CURRENT_FOLLOWER_FACTION);

        std::vector<RE::Actor*> candidates;
        std::size_t highCount = 0;
        std::size_t teammateCount = 0;
        std::size_t factionCount = 0;

        if (auto* lists = RE::ProcessLists::GetSingleton()) {
            Engine::ForEachHighActor(*lists, [&](RE::Actor& a_actor) {
                if (&a_actor == a_player) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                ++highCount;
                const bool teammate = a_actor.IsPlayerTeammate();
                const bool inFaction = followerFaction && a_actor.IsInFaction(followerFaction);
                if (teammate) {
                    ++teammateCount;
                }
                if (inFaction) {
                    ++factionCount;
                }

                const auto formID = a_actor.GetFormID();

                const bool plausible = teammate || inFaction || IsRegisteredFollower(formID);

                if (explicitExclusions.contains(formID)) {
                    if (plausible) {
                        LogDiagnostic(formID, a_actor.GetName(), "excluded through the Walk With Me API");
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                const char* rejection = IsRegisteredFollower(formID) ? nullptr : CandidateRejection(a_actor);
                if (!rejection) {
                    candidates.push_back(&a_actor);
                }
                else if (plausible) {
                    LogDiagnostic(formID, a_actor.GetName(), rejection);
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }

        LogScanSummary(highCount, teammateCount, factionCount);

        std::sort(candidates.begin(), candidates.end(),
                  [](const RE::Actor* a_left, const RE::Actor* a_right) {
                      return a_left->GetFormID() < a_right->GetFormID();
                  });

        std::unordered_set<RE::FormID> seen;

        if (Settings::GetSingleton().Get().teleportCatchup) {
            for (const auto& [id, f] : managed) {
                auto actor = f.handle.get();
                if (actor && !explicitExclusions.contains(id) && !actor->IsDead() && !actor->IsDisabled() &&
                    (IsRegisteredFollower(id) || !CandidateRejection(*actor))) {
                    seen.insert(id);
                }
            }
        }
        if (IsRelaxing()) {
            for (const auto& [id, f] : managed) {
                auto actor = f.handle.get();
                if (f.anchorSet && actor && !explicitExclusions.contains(id) &&
                    (IsRegisteredFollower(id) || !CandidateRejection(*actor))) {
                    seen.insert(id);
                }
            }
        }
        const auto maxFollowers = static_cast<std::size_t>(Settings::GetSingleton().Get().maxFollowers);
        for (auto* actor : candidates) {
            if (!actor) {
                continue;
            }
            if (!InSameTravelSpace(*actor, *a_player)) {
                LogDiagnostic(actor->GetFormID(), actor->GetName(),
                              "in a different cell or worldspace than the player");
                continue;
            }
            const auto formID = actor->GetFormID();
            if (managed.contains(formID)) {
                managed[formID].handle = actor->GetHandle();
                seen.insert(formID);
                continue;
            }
            if (managed.size() >= maxFollowers) {
                LogDiagnostic(formID, actor->GetName(), "iMaxFollowers already reached");
                continue;
            }
            const auto registration = manualRegistrations.find(formID);
            const int preferredSlot = registration == manualRegistrations.end() ? -1 : registration->second;
            const int slot = FindAvailableSlot(preferredSlot);
            if (slot < 0) {
                LogDiagnostic(formID, actor->GetName(), "no free formation slot");
                continue;
            }
            ManagedFollower follower;
            follower.handle = actor->GetHandle();
            follower.slot = slot;
            managed.emplace(formID, std::move(follower));
            seen.insert(formID);
            logger::info("[Controller] Managing {:08X} ({}) in slot {}", formID, actor->GetName(), slot);
        }

        std::vector<RE::FormID> stale;
        for (const auto& [formID, follower] : managed) {
            static_cast<void>(follower);
            if (!seen.contains(formID)) {
                stale.push_back(formID);
            }
        }
        for (const auto formID : stale) {
            RemoveManaged(formID);
        }
    }

    const char* FormationController::CandidateRejection(RE::Actor& a_actor) const
    {
        const auto& settings = Settings::GetSingleton().Get();
        if (!settings.autoDiscover) {
            return "bAutoDiscover is off";
        }
        if (a_actor.IsDead()) {
            return "dead";
        }
        if (a_actor.IsDisabled()) {
            return "disabled";
        }

        if (const auto* base = a_actor.GetActorBase()) {
            if (const auto* file = base->GetFile(0);
                file && Settings::GetSingleton().IsPluginExcluded(file->GetFilename())) {
                return "source plugin listed in sExcludedPlugins";
            }
        }

        if (const auto* reason = RecruitmentRejectionFor(a_actor, settings.requirePlayerTeammate)) {
            return reason;
        }
        if (IsNFFFollower(a_actor)) {
            return settings.enforceNFF ? nullptr : "NFF automatic enrollment is off";
        }
        if (IsCustomFollower(a_actor) && !settings.enforceCustomFollowers) {
            return "custom follower enforcement is off";
        }
        return nullptr;
    }

    bool FormationController::IsAutomaticCandidate(RE::Actor& a_actor) const
    {
        return CandidateRejection(a_actor) == nullptr;
    }

    std::string FormationController::UsabilityRejection(RE::Actor& a_actor, RE::PlayerCharacter& a_player,
                                                        bool inspectBanterScene, bool allowDrawnWeapon) const
    {
        const auto& settings = Settings::GetSingleton().Get();
        const auto* actorState = a_actor.AsActorState();
        const auto heldEntry = managed.find(a_actor.GetFormID());
        const bool held = IsRelaxing() && heldEntry != managed.end() && heldEntry->second.anchorSet;
        if (a_actor.IsDead()) {
            return "dead";
        }
        if (a_actor.IsDisabled()) {
            return "disabled";
        }
        if (!held && !a_actor.Is3DLoaded()) {
            return "3D not loaded";
        }
        if (!a_actor.IsAIEnabled()) {
            return "AI disabled";
        }
        if (!held && !InSameTravelSpace(a_actor, a_player)) {
            return "in a different cell or worldspace than the player";
        }
        if (!actorState) {
            return "no actor state";
        }
        if (!held && a_actor.IsInCombat()) {
            return "in combat";
        }
        if (!held && a_actor.IsSneaking()) {
            return "sneaking";
        }
        if (actorState->IsSwimming()) {
            return "swimming";
        }
        if (a_actor.IsOnMount()) {
            return "mounted";
        }
        if (actorState->IsBleedingOut()) {
            return "bleeding out";
        }
        if (actorState->IsUnconscious()) {
            return "unconscious";
        }

        const auto* sittingPackage = a_actor.GetCurrentPackage();
        const auto* sittingSource = sittingPackage ? sittingPackage->GetFile(0) : nullptr;
        const bool ourSandbox = heldEntry != managed.end() &&
                                (heldEntry->second.sandboxActive ||
                                 (heldEntry->second.travelActive && sittingSource &&
                                  std::string_view(sittingSource->GetFilename()) == settings.pluginName));
        if (!ourSandbox && !sandbox.active &&
            actorState->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) {
            return "sitting or sleeping";
        }
        if (a_actor.AsActorValueOwner()->GetActorValue(RE::ActorValue::kWaitingForPlayer) > 0.0F) {
            return "waiting for player";
        }
        if (a_actor.GetCurrentScene() && !inspectBanterScene &&
            !IsWalkingBanterScene(a_actor.GetCurrentScene())) {
            return "in a scene";
        }
        if (actorState->GetLifeState() == RE::ACTOR_LIFE_STATE::kRestrained) {
            return "restrained by another behavior";
        }
        if (!held && !allowDrawnWeapon && actorState->IsWeaponDrawn()) {
            return "weapon drawn";
        }
        if (!IsRegisteredFollower(a_actor.GetFormID()) && CandidateRejection(a_actor)) {
            return "no longer an eligible follower";
        }
        const auto* current = a_actor.GetCurrentPackage();
        const auto* source = current ? current->GetFile(0) : nullptr;
        const bool own = source && std::string_view(source->GetFilename()) == settings.pluginName;
        const bool banterPackage =
            settings.walkingBanter && current && static_cast<int>(current->packData.packType.get()) == 28 &&
            (inspectBanterScene ||
             std::any_of(travelBanterPairs.begin(), travelBanterPairs.end(),
                         [&](const auto& pair) { return pair.Contains(a_actor.GetFormID()); }));
        if (settings.requireTravelPackage && !sandbox.active && !own && !banterPackage &&
            !(a_actor.GetCurrentScene() &&
              (inspectBanterScene || IsWalkingBanterScene(a_actor.GetCurrentScene()))) &&
            !IsRegisteredFollower(a_actor.GetFormID()) &&
            !(settings.enforceCustomFollowers && IsCustomFollower(a_actor)) &&
            !(settings.enforceNFF && IsNFFFollower(a_actor))) {
            const auto* package = a_actor.GetCurrentPackage();
            if (!IsFollowerTravelPackage(package)) {
                if (!package) {
                    return "no AI package running (bRequireTravelPackage)";
                }
                return fmt::format("busy with package {} (bRequireTravelPackage)", DescribePackage(package));
            }
        }
        const float distance = Distance2D(a_actor.GetPosition(), a_player.GetPosition());
        if (!held && distance > settings.releaseDistance) {
            return fmt::format("{:.0f} units away, beyond fReleaseDistance", distance);
        }
        return {};
    }

    bool FormationController::IsActorUsable(RE::Actor& a_actor, RE::PlayerCharacter& a_player) const
    {
        return UsabilityRejection(a_actor, a_player).empty();
    }

    const char* FormationController::GlobalRejection(RE::PlayerCharacter& a_player) const
    {
        const auto& settings = Settings::GetSingleton().Get();
        const auto* playerState = a_player.AsActorState();
        if (!playerState) {
            return "no player actor state";
        }
        if (auto* ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
            return "game paused";
        }
        if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) &&
                                               !settings.sandbox.automatic && !sandbox.active) {
            return "dialogue";
        }
        if (!a_player.GetParentCell()) {
            return "player cell unavailable";
        }
        if (settings.releaseInCombat && a_player.IsInCombat()) {
            return "player in combat";
        }
        if (settings.releaseWhenSneaking && a_player.IsSneaking()) {
            return "player sneaking";
        }
        if (settings.releaseWhenWeaponDrawn && playerState->IsWeaponDrawn()) {
            return "player weapon drawn";
        }
        if (playerState->IsSwimming()) {
            return "player swimming";
        }
        if (a_player.IsOnMount()) {
            return "player mounted";
        }
        if (!sandbox.active && !settings.sandbox.automatic &&
            playerState->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) {
            return "player sitting or sleeping";
        }
        if (settings.releaseWhenControlsDisabled && !sandbox.active) {
            const auto* controls = RE::ControlMap::GetSingleton();
            if (controls && !controls->IsMovementControlsEnabled() &&
                !(settings.sandbox.automatic &&
                  RE::UI::GetSingleton()->IsMenuOpen(RE::DialogueMenu::MENU_NAME))) {
                return "player movement controls disabled";
            }
        }
        if (settings.disableIndoors) {
            const auto* cell = a_player.GetParentCell();
            if (cell && cell->IsInteriorCell()) {
                return "interior cell and bDisableIndoors is on";
            }
        }
        return nullptr;
    }

    bool FormationController::IsGlobalTravelStateSafe(RE::PlayerCharacter& a_player) const
    {
        return GlobalRejection(a_player) == nullptr;
    }

    int FormationController::FindAvailableSlot(int a_preferredSlot) const
    {
        std::set<int> occupied;
        for (const auto& [formID, follower] : managed) {
            static_cast<void>(formID);
            occupied.insert(follower.slot);
        }
        const int maximum = Settings::GetSingleton().Get().maxFollowers;
        if (a_preferredSlot >= 0 && a_preferredSlot < maximum && !occupied.contains(a_preferredSlot)) {
            return a_preferredSlot;
        }
        for (int slot = 0; slot < maximum; ++slot) {
            if (!occupied.contains(slot)) {
                return slot;
            }
        }
        return -1;
    }

    bool FormationController::IsWalkingBanterScene(const RE::BGSScene* scene) const
    {
        return scene && Settings::GetSingleton().Get().walkingBanter &&
               std::any_of(banterScenes.begin(), banterScenes.end(),
                           [&](const auto& lease) { return lease.scene == scene; });
    }

    void FormationController::RestoreTravelBanter()
    {

        for (auto& lease : banterScenes) {
            for (auto* action : lease.facingActions) {
                action->flags.set(RE::BGSSceneAction::Flag::kFaceTarget);
            }
            for (auto index : lease.exclusiveActors) {
                if (index < lease.scene->actorFlags.size()) {
                    lease.scene->actorFlags[index].set(RE::SCENE_ACTOR_FLAG::kRunOnlyScenePackages);
                }
            }
        }
        for (auto* topic : banterTopics) {
            topic->data.flags.reset(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kCanMoveWhileGreeting);
        }
        banterTopics.clear();
        banterScenes.clear();
        travelBanterPairs.clear();
    }

    void FormationController::UpdateTravelBanter(RE::PlayerCharacter& player, float dt)
    {
        const auto& settings = Settings::GetSingleton().Get();
        if (!settings.walkingBanter || sandbox.active || !intent.IsMoving()) {
            RestoreTravelBanter();
            return;
        }
        auto eligible = [&](RE::FormID id) -> RE::Actor* {
            const auto found = managed.find(id);
            if (found == managed.end()) {
                return nullptr;
            }
            auto actor = found->second.handle.get();
            return actor && !actor->IsPlayerRef() && UsabilityRejection(*actor, player, true).empty()
                       ? actor.get()
                       : nullptr;
        };
        for (auto it = banterScenes.begin(); it != banterScenes.end();) {
            auto* first = eligible(it->first);
            auto* second = eligible(it->second);
            if (first && second && first->GetCurrentScene() == it->scene &&
                second->GetCurrentScene() == it->scene) {
                ++it;
                continue;
            }
            for (auto* action : it->facingActions) {
                action->flags.set(RE::BGSSceneAction::Flag::kFaceTarget);
            }
            for (auto index : it->exclusiveActors) {
                if (index < it->scene->actorFlags.size()) {
                    it->scene->actorFlags[index].set(RE::SCENE_ACTOR_FLAG::kRunOnlyScenePackages);
                }
            }
            logger::info("[Banter] restored scene {:08X}", it->scene->GetFormID());
            it = banterScenes.erase(it);
        }
        for (auto& pair : travelBanterPairs) {
            pair.remaining -= dt;
        }
        std::erase_if(travelBanterPairs, [&](const auto& pair) {
            auto* a = eligible(pair.first);
            auto* b = eligible(pair.second);
            return pair.remaining <= 0 || !a || !b ||
                   (a->GetCurrentScene() && !IsWalkingBanterScene(a->GetCurrentScene())) ||
                   (b->GetCurrentScene() && !IsWalkingBanterScene(b->GetCurrentScene()));
        });
        auto rememberPair = [&](RE::FormID a, RE::FormID b) {
            if (a == b) {
                return;
            }
            if (a > b) {
                std::swap(a, b);
            }
            for (auto& pair : travelBanterPairs) {
                if (pair.first == a && pair.second == b) {
                    pair.remaining = 4;
                    return;
                }
                if (pair.Contains(a) || pair.Contains(b)) {
                    return;
                }
            }
            travelBanterPairs.push_back({a, b, 4});
            roleTimer = 1;
            roleCooldown = 0;
            logger::info("[Banter] walking pair {:08X} + {:08X}", a, b);
        };
        std::vector<RE::FormID> ids;
        for (const auto& [id, f] : managed) {
            ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end());
        for (auto id : ids) {
            auto* actor = eligible(id);
            if (!actor) {
                continue;
            }
            auto* scene = actor->GetCurrentScene();
            if (traceDue && settings.traceMovement && (scene || IsSpeaking(*actor))) {
                auto target = actor->GetActorRuntimeData().dialogueItemTarget.get();
                logger::info("[Banter] {} ({:08X}) scene={:08X} target={:08X} package={}", actor->GetName(),
                             id, scene ? scene->GetFormID() : 0, target ? target->GetFormID() : 0,
                             DescribePackage(actor->GetCurrentPackage()));
            }
            if (!scene) {
                if (!IsSpeaking(*actor)) {
                    continue;
                }
                auto target = actor->GetActorRuntimeData().dialogueItemTarget.get();
                auto* listener = target ? target->As<RE::Actor>() : nullptr;
                if (listener && !listener->GetCurrentScene() && eligible(listener->GetFormID())) {
                    rememberPair(id, listener->GetFormID());
                }
                continue;
            }
            if (IsWalkingBanterScene(scene)) {
                continue;
            }
            if (!scene->parentQuest || scene->actors.size() != 2 || scene->actorFlags.size() != 2) {
                continue;
            }
            std::array<RE::Actor*, 2> participants{};
            bool allManaged = true, sceneOnly = false, dialogue = false, packageAction = false,
                 playerTarget = false, looping = false;
            for (std::size_t i = 0; i < 2; ++i) {
                auto* alias = GetRefAlias(scene->parentQuest, scene->actors[i]);
                auto* ref = alias ? alias->GetReference() : nullptr;
                participants[i] = ref ? eligible(ref->GetFormID()) : nullptr;
                allManaged &= participants[i] && participants[i]->GetCurrentScene() == scene;
                sceneOnly |= scene->actorFlags[i].any(RE::SCENE_ACTOR_FLAG::kNoPlayerActivation);
            }
            if (!allManaged || participants[0] == participants[1]) {
                continue;
            }
            const auto memberAlias = [&](std::uint32_t alias) {
                return alias == scene->actors[0] || alias == scene->actors[1];
            };
            for (auto* action : scene->actions) {
                if (!action) {
                    packageAction = true;
                    break;
                }
                const auto type = action->GetType();
                packageAction |=
                    type != RE::BGSSceneAction::Type::kDialogue && type != RE::BGSSceneAction::Type::kTimer;
                looping |= action->Loops();
                playerTarget |= action->flags.any(RE::BGSSceneAction::Flag::kHeadTrackPlayer);
                if (type != RE::BGSSceneAction::Type::kDialogue) {
                    continue;
                }
                dialogue = true;
                const auto* speech = static_cast<RE::BGSSceneActionDialogue*>(action);

                playerTarget |= !memberAlias(action->actorID) ||
                                (speech->headtrackActorID >= 0 &&
                                 !memberAlias(static_cast<std::uint32_t>(speech->headtrackActorID)));
            }
            if (!CanWalkBanterScene(2, allManaged, dialogue, packageAction, sceneOnly, playerTarget,
                                    looping)) {
                continue;
            }
            BanterSceneLease lease{scene, participants[0]->GetFormID(), participants[1]->GetFormID(), {}};

            for (std::uint32_t i = 0; i < 2; ++i) {
                if (scene->actorFlags[i].any(RE::SCENE_ACTOR_FLAG::kRunOnlyScenePackages)) {
                    lease.exclusiveActors.push_back(i);
                    scene->actorFlags[i].reset(RE::SCENE_ACTOR_FLAG::kRunOnlyScenePackages);
                }
            }
            for (auto* action : scene->actions) {
                if (action->GetType() == RE::BGSSceneAction::Type::kDialogue &&
                    action->flags.any(RE::BGSSceneAction::Flag::kFaceTarget)) {
                    lease.facingActions.push_back(action);
                    action->flags.reset(RE::BGSSceneAction::Flag::kFaceTarget);
                }
            }
            logger::info("[Banter] admitted scene {:08X}: {} + {}; relaxed facing actions={}",
                         scene->GetFormID(), participants[0]->GetName(), participants[1]->GetName(),
                         lease.facingActions.size());
            banterScenes.push_back(std::move(lease));
        }

        for (const auto& lease : banterScenes) {
            rememberPair(lease.first, lease.second);
        }
        UpdateBanterMotion(player);
    }

    void FormationController::UpdateBanterMotion(RE::PlayerCharacter& player)
    {
        if (!Settings::GetSingleton().Get().walkingBanter || sandbox.active || !intent.IsMoving()) {
            return;
        }
        std::vector<RE::TESTopicInfo*> activeTopics;
        for (const auto& pair : travelBanterPairs) {
            for (auto id : {pair.first, pair.second}) {
                auto found = managed.find(id);
                auto actor = found == managed.end() ? RE::NiPointer<RE::Actor>{} : found->second.handle.get();
                if (!actor || !UsabilityRejection(*actor, player).empty()) {
                    continue;
                }
                auto& runtime = actor->GetActorRuntimeData();
                auto target = runtime.dialogueItemTarget.get();
                const bool scene = IsWalkingBanterScene(actor->GetCurrentScene());
                auto* process = runtime.currentProcess;
                auto* high = process ? process->high : nullptr;
                if (!high) {
                    continue;
                }
                using Flag = RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS;
                auto* sayTo = actor->extraList.GetByType<RE::ExtraSayToTopicInfo>();
                auto* speech = sayTo && sayTo->item ? sayTo->item : high->greetTopic.get();
                auto* topic = speech ? speech->info : nullptr;
                const bool forced = topic && topic->data.flags.any(Flag::kIsForceGreet, Flag::kPlayerAddress,
                                                                   Flag::kRequiresPlayerActivation);
                if (!CanRelaxBanterFacing(pair, id, target ? target->GetFormID() : 0, scene,
                                          scene || IsSpeaking(*actor),
                                          high->talkingToPC || high->greetingPlayer, forced)) {
                    continue;
                }

                runtime.boolFlags.reset(RE::Actor::BOOL_FLAGS::kSceneHeadTrackRotation);
                runtime.boolBits.reset(RE::Actor::BOOL_BITS::kShouldRotateToTrack);
                if (scene) {
                    high->sceneHeadTrackTimer = 0;
                }
                if (topic) {
                    activeTopics.push_back(topic);
                    if (!topic->data.flags.any(Flag::kCanMoveWhileGreeting)) {
                        topic->data.flags.set(Flag::kCanMoveWhileGreeting);
                        banterTopics.push_back(topic);
                    }
                }
            }
        }
        std::erase_if(banterTopics, [&](auto* topic) {
            if (std::find(activeTopics.begin(), activeTopics.end(), topic) != activeTopics.end()) {
                return false;
            }
            topic->data.flags.reset(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kCanMoveWhileGreeting);
            return true;
        });
    }

    const char* FormationController::RecallPlayerRejection(RE::PlayerCharacter& player) const
    {
        if (!enabled) {
            return "Walk With Me is disabled";
        }
        const auto* state = player.AsActorState();
        if (!state || !player.Is3DLoaded() || !player.GetParentCell()) {
            return "Load a game first";
        }
        if (player.IsInCombat() || player.GetCurrentScene()) {
            return "Wait until combat or the scene ends";
        }
        if (player.IsOnMount() || state->IsSwimming() ||
            state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal || state->IsBleedingOut() ||
            state->IsUnconscious()) {
            return "Stand on clear ground first";
        }
        return nullptr;
    }

    bool FormationController::RecallActorAllowed(RE::Actor& actor) const
    {
        const auto* state = actor.AsActorState();
        if (!state || actor.IsPlayerRef() || actor.IsDead() || actor.IsDisabled() || !actor.IsAIEnabled() ||
            actor.IsInCombat() || actor.IsOnMount() || actor.GetCurrentScene() || IsSpeaking(actor) ||
            state->IsSwimming() || state->IsBleedingOut() || state->IsUnconscious() ||
            state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal ||
            state->GetLifeState() == RE::ACTOR_LIFE_STATE::kRestrained ||
            actor.AsActorValueOwner()->GetActorValue(RE::ActorValue::kWaitingForPlayer) != 0) {
            return false;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto id = actor.GetFormID();
        return player && !actor.IsHostileToActor(player) && !explicitExclusions.contains(id) &&
               (IsRegisteredFollower(id) || !CandidateRejection(actor));
    }

    std::vector<RE::Actor*> FormationController::RecallCandidates() const
    {
        std::vector<RE::Actor*> result;
        auto add = [&](RE::Actor* actor) {
            if (actor && RecallActorAllowed(*actor) &&
                std::find(result.begin(), result.end(), actor) == result.end()) {
                result.push_back(actor);
            }
        };
        for (const auto& [id, follower] : managed) {
            static_cast<void>(id);
            auto actor = follower.handle.get();
            add(actor.get());
        }
        for (const auto& [id, slot] : manualRegistrations) {
            static_cast<void>(slot);
            add(RE::TESForm::LookupByID<RE::Actor>(id));
        }
        for (const auto& [id, flags] : packageRegistrations) {
            static_cast<void>(flags);
            add(RE::TESForm::LookupByID<RE::Actor>(id));
        }
        for (const auto id : dialogueRegistrations) {
            add(RE::TESForm::LookupByID<RE::Actor>(id));
        }
        // NFF uses these active slots; vanilla uses the original follower slot.
        // Eligibility still runs through the strict enrollment guard above.
        if (auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(0x750BA); quest && quest->IsRunning()) {
            for (std::uint32_t slot = 0; slot < 12; ++slot) {
                auto* alias = GetRefAlias(quest, slot);
                auto* ref = alias ? alias->GetReference() : nullptr;
                add(ref ? ref->As<RE::Actor>() : nullptr);
            }
        }
        std::stable_sort(result.begin(), result.end(), [&](auto* left, auto* right) {
            if (managed.contains(left->GetFormID()) != managed.contains(right->GetFormID())) {
                return managed.contains(left->GetFormID());
            }
            return left->GetFormID() < right->GetFormID();
        });
        result.resize(
            std::min(result.size(), static_cast<std::size_t>(Settings::GetSingleton().Get().maxFollowers)));
        return result;
    }

    std::optional<RE::NiPoint3> FormationController::RecallLanding(
        RE::PlayerCharacter& player, RE::FormID id, int slot, const std::vector<RE::NiPoint3>& reserved) const
    {
        const auto pos = player.GetPosition();
        const auto forward = ForwardFromYaw(player.GetAngleZ());
        const auto right = RightFromForward(forward);
        for (int attempt = 0; attempt < 6; ++attempt) {
            const float back = 220.0F + 90.0F * (slot / 2) + 60.0F * (attempt / 2);
            const float side = ((slot + attempt) % 2 ? 1.0F : -1.0F) * (90.0F + 45.0F * (attempt % 3));
            const RE::NiPoint3 proposed{pos.x - forward.x * back + right.x * side,
                                        pos.y - forward.y * back + right.y * side, pos.z};
            auto ground = navigation.AttachmentPoint(pos, proposed);
            if (!ground || !navigation.RestPoint(*ground)) {
                continue;
            }
            bool occupied = std::any_of(reserved.begin(), reserved.end(),
                                        [&](auto point) { return Distance2D(point, *ground) < 100; });
            if (auto* lists = RE::ProcessLists::GetSingleton()) {
                Engine::ForEachHighActor(*lists, [&](RE::Actor& other) {
                    if (other.GetFormID() != id && InSameTravelSpace(other, player) &&
                        std::abs(other.GetPosition().z - ground->z) < 120 &&
                        Distance2D(other.GetPosition(), *ground) < 100) {
                        occupied = true;
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
            if (!occupied) {
                return ground;
            }
        }
        return std::nullopt;
    }

    const char* FormationController::CompanionPushRejection(RE::Actor& actor) const
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!initialized || !player || !player->Is3DLoaded() || player->IsDead()) {
            return "Load a game first";
        }
        if (actor.IsPlayerRef() || actor.IsDead() || actor.IsDisabled() || !actor.Is3DLoaded()) {
            return "Companion is not available";
        }
        if (!InSameTravelSpace(actor, *player) ||
            actor.GetPosition().GetDistance(player->GetPosition()) > 800.0F) {
            return "Move closer to this companion";
        }
        if (player->IsInCombat() || actor.IsInCombat() || actor.IsHostileToActor(player)) {
            return "Wait until combat ends";
        }
        if (actor.GetCurrentScene() || player->GetCurrentScene() || IsSpeaking(actor) ||
            IsSpeaking(*player)) {
            return "Wait until the conversation or scene ends";
        }
        if (!actor.IsAIEnabled()) {
            return "This companion's AI is disabled";
        }
        const auto* state = actor.AsActorState();
        if (!state || actor.IsOnMount() || actor.IsInKillMove() || actor.IsInRagdollState() ||
            actor.IsInMidair() || state->IsSwimming() || state->IsFlying() || state->IsBleedingOut() ||
            state->IsUnconscious() || state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal ||
            state->GetLifeState() == RE::ACTOR_LIFE_STATE::kRestrained) {
            return "Wait until this companion is standing on the ground";
        }
        if (HandHolding::OwnsMovement(actor.GetFormID()) || HandHolding::HandoffTicket(actor.GetFormID())) {
            return "Release hands and let the connection finish first";
        }
        return nullptr;
    }

    void FormationController::RequestCompanionPush(RE::FormID id, std::uint64_t expectedGeneration)
    {
        std::lock_guard lock(stateMutex);
        if (generation != expectedGeneration) {
            return;
        }
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
        if (!actor) {
            return;
        }
        if (const auto* reason = CompanionPushRejection(*actor)) {
            Notify(reason);
            return;
        }
        if (!companionPush.Request(id, generation, GetTickCount64())) {
            Notify("Wait a moment before another push.");
        }
    }

    void FormationController::UpdateCompanionPush()
    {
        if (!companionPush.queue.pending && !companionPush.dispatched) {
            return;
        }
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(companionPush.actor);
        auto* ui = RE::UI::GetSingleton();
        const bool menuOpen =
            !ui || ui->GameIsPaused() || Menu::HasBlockingWindow() || ShouldSuspendForDialogue();
        const auto result =
            companionPush.Poll(generation, GetTickCount64(), menuOpen,
                               loadReleaseTime <= 0 && actor && !CompanionPushRejection(*actor));
        if (result == RecallDispatch::Cancel) {
            Notify("Unstick cancelled: companion is no longer ready.");
            return;
        }
        if (result != RecallDispatch::Run) {
            return;
        }
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        struct Completion final : RE::BSScript::IStackCallbackFunctor
        {
            std::uint64_t save;
            explicit Completion(std::uint64_t value) : save(value) {}
            void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
            void operator()(RE::BSScript::Variable value) override
            {
                const bool pushed = value.IsBool() && value.GetBool();
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([stamp = save, pushed] {
                        if (FormationController::GetSingleton().Snapshot().generation == stamp) {
                            Engine::Notify(pushed ? "Unstick push applied."
                                                  : "Unstick cancelled: companion is no longer ready.");
                        }
                    });
                }
            }
        };
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{new Completion(generation)};
        if (!vm || !vm->DispatchStaticCall(RE::BSFixedString("WayfarerRecovery"), RE::BSFixedString("Push"),
                                           RE::MakeFunctionArguments(static_cast<RE::Actor*>(actor),
                                                                     std::int32_t{companionPush.ticket}),
                                           callback)) {
            companionPush.Reset();
            logger::error("[CompanionRecovery] Cannot dispatch Push; install WayfarerRecovery.pex");
            Notify("Unstick unavailable: recovery script could not be started.");
        }
    }

    bool FormationController::ConsumeCompanionPush(RE::Actor* actor, std::int32_t ticket)
    {
        std::lock_guard lock(stateMutex);
        auto* ui = RE::UI::GetSingleton();
        auto* controls = RE::ControlMap::GetSingleton();
        const bool safe = actor && ui && !ui->GameIsPaused() && !Menu::HasBlockingWindow() &&
                          !ShouldSuspendForDialogue() && loadReleaseTime <= 0 && controls &&
                          controls->IsMovementControlsEnabled() && !CompanionPushRejection(*actor);
        if (!companionPush.Consume(actor ? actor->GetFormID() : 0, ticket, generation, GetTickCount64(),
                                   safe)) {
            return false;
        }
        logger::info("[CompanionRecovery] Player requested a gentle push for {} ({:08X}), ticket {}",
                     actor->GetName(), actor->GetFormID(), ticket);
        return true;
    }

    void FormationController::RequestPartyRecall(std::uint64_t expectedGeneration)
    {
        std::lock_guard lock(stateMutex);
        if (generation != expectedGeneration) {
            return;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }
        if (const auto* reason = RecallPlayerRejection(*player)) {
            Notify(reason);
            return;
        }
        if (RecallCandidates().empty()) {
            Notify("No travelling companions are available to teleport.");
            return;
        }
        recallRequest.Request(generation, GetTickCount64());
    }

    void FormationController::RecallParty(RE::PlayerCharacter& player)
    {
        if (auto* controls = RE::ControlMap::GetSingleton();
            controls && !controls->IsMovementControlsEnabled()) {
            Notify("Party teleport cancelled: player movement is locked.");
            return;
        }
        const auto candidates = RecallCandidates();
        navigation.Collect(player);
        std::vector<RE::NiPoint3> reserved;
        for (auto* actor : candidates) {
            const auto id = actor->GetFormID();
            auto landing = RecallLanding(player, id, static_cast<int>(reserved.size()), reserved);
            if (!landing) {
                continue;
            }
            if (auto entry = managed.find(id); entry != managed.end()) {
                ReleaseTravel(entry->second, actor);
            }
            actor->MoveTo(&player);
            actor->SetPosition(*landing, true);
            actor->EvaluatePackage(false, true);
            reserved.push_back(*landing);
            logger::info("[PartyRecall] Summoned {} ({:08X})", actor->GetName(), id);
        }
        if (!reserved.empty()) {
            ReleaseAll();
            sandbox.Reset();
            intent.Reset();
            for (auto& [id, follower] : managed) {
                static_cast<void>(id);
                follower.anchorSet = false;
                follower.restAnchorZSet = false;
            }
            scanTimer = Settings::GetSingleton().Get().scanInterval;
        }
        Notify(reserved.empty() ? "No clear landing space found for the party."
                                : fmt::format("Teleported {} companion(s) nearby. {} could not be placed.",
                                              reserved.size(), candidates.size() - reserved.size()));
    }

    void FormationController::ToggleTemporaryRelaxation(std::uint64_t expectedGeneration)
    {
        std::lock_guard lock(stateMutex);
        if (generation != expectedGeneration) {
            return;
        }
        auto settings = Settings::GetSingleton().Get();
        settings.sandbox.automatic = !settings.sandbox.automatic;
        if (!ApplyPreferences(settings)) {
            Notify("Could not save temporary relaxation setting.");
            return;
        }
        if (!settings.sandbox.automatic && !IsRelaxing()) {
            ReleaseAll();
            sandbox.Reset();
        }
        Notify(settings.sandbox.automatic ? "Temporary relaxation enabled."
                                          : "Temporary relaxation disabled.");
    }

    void FormationController::UpdateFollowerCatchup(RE::PlayerCharacter& player, float dt)
    {
        const auto& settings = Settings::GetSingleton().Get();
        const auto pos = player.GetPosition();
        for (int slot = 0; slot < PARTY_CAPACITY; ++slot) {
            for (auto& [id, f] : managed) {
                if (f.slot != slot) {
                    continue;
                }
                auto actor = f.handle.get();
                const auto* state = actor ? actor->AsActorState() : nullptr;
                const bool permitted =
                    settings.teleportCatchup && !sandbox.active && !IsRelaxing() && actor && state &&
                    InSameTravelSpace(*actor, player) && !actor->IsDead() && !actor->IsDisabled() &&
                    actor->IsAIEnabled() && !actor->IsInCombat() && !player.IsInCombat() &&
                    !actor->IsSneaking() && !actor->IsOnMount() && !actor->GetCurrentScene() &&
                    !IsSpeaking(*actor) && !state->IsSwimming() && !state->IsBleedingOut() &&
                    !state->IsUnconscious() && !state->IsWeaponDrawn() &&
                    state->GetSitSleepState() == RE::SIT_SLEEP_STATE::kNormal &&
                    state->GetLifeState() != RE::ACTOR_LIFE_STATE::kRestrained &&
                    !explicitExclusions.contains(id) &&
                    actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kWaitingForPlayer) == 0 &&
                    (IsRegisteredFollower(id) || !CandidateRejection(*actor));
                const auto at = actor ? actor->GetPosition() : pos;
                const float distance = std::hypot(Distance2D(at, pos), at.z - pos.z);
                if (!f.catchup.Update(dt, permitted, distance, settings.teleportDistance)) {
                    continue;
                }
                auto landing = RecallLanding(player, id, slot);
                f.catchup.Attempt(landing.has_value());
                if (!landing) {
                    continue;
                }
                ReleaseTravel(f, actor.get());
                actor->MoveTo(&player);
                actor->SetPosition(*landing, true);
                actor->EvaluatePackage(false, true);
                logger::info("[Catchup] recalled {} ({:08X}) from {:.0f} units to ({:.0f}, {:.0f}, {:.0f})",
                             actor->GetName(), id, distance, landing->x, landing->y, landing->z);
            }
        }
    }

    void FormationController::UpdateTravelRoles(RE::PlayerCharacter& player, float dt)
    {

        if (sandbox.active) {
            naturalPatternReady = false;
            return;
        }
        if (!intent.IsMoving() && !regroup.active) {
            return;
        }
        roleTimer += dt;
        roleCooldown = std::max(0.0F, roleCooldown - dt);
        if (roleTimer < .5F || roleCooldown > 0) {
            return;
        }
        roleTimer = 0;
        std::vector<std::pair<RE::FormID, ManagedFollower*>> followers;
        for (auto& [id, f] : managed) {
            auto actor = f.handle.get();
            if (actor && UsabilityRejection(*actor, player).empty()) {
                followers.emplace_back(id, &f);
            }
        }
        std::sort(followers.begin(), followers.end(), [](auto& a, auto& b) { return a.first < b.first; });
        if (followers.empty()) {
            return;
        }
        const auto& settings = Settings::GetSingleton().Get();
        const bool interior = player.GetParentCell()->IsInteriorCell();
        travelPartyCount = static_cast<int>(followers.size());
        std::uint64_t signature = 1469598103934665603ULL;

        std::vector<RE::FormID> members;
        for (const auto& [id, f] : managed) {
            members.push_back(id);
        }
        std::sort(members.begin(), members.end());
        for (auto id : members) {
            signature ^= id;
            signature *= 1099511628211ULL;
        }
        if (!naturalPatternReady || signature != naturalPartySignature) {
            naturalPartySignature = signature;
            naturalPatternReady = true;
            naturalPatternSeed = RestPose::Mix(static_cast<std::uint32_t>(GetTickCount64()) ^
                                               static_cast<std::uint32_t>(signature));
            logger::info("[Formation] party={} naturalStragglers={} setback={:.0f} spacing={:.2f}",
                         travelPartyCount,
                         mode == FormationMode::kDynamic
                             ? NaturalStragglerCount(travelPartyCount, naturalPatternSeed,
                                                     settings.travel.naturalStragglerDistance)
                             : 0,
                         settings.travel.naturalStragglerDistance, settings.travel.spacing);
        }
        std::vector<Vec2> actors, goals;
        std::vector<int> previous;
        for (std::size_t role = 0; role < followers.size(); ++role) {
            const auto planned = PartyRole(mode, static_cast<int>(role), travelPartyCount, naturalPatternSeed,
                                           settings.travel.naturalStragglerDistance);
            const auto desired =
                intent.GoalAt(mode, planned.offset, interior, settings.preferredSide, settings.travel,
                              planned.straggler ? settings.travel.naturalStragglerDistance : 0);
            const auto grounded = navigation.Resolve(player.GetPosition(), desired, intent.Direction());

            if (!grounded) {
                return;
            }
            goals.push_back({grounded->x, grounded->y});
        }
        for (auto& [id, f] : followers) {
            auto actor = f->handle.get();
            if (!actor) {
                return;
            }
            const auto p = actor->GetPosition();
            actors.push_back({p.x, p.y});
            previous.push_back(f->role);
        }
        std::vector<std::array<int, 2>> pairs;
        for (const auto& pair : travelBanterPairs) {
            int a = -1, b = -1;
            for (int i = 0; i < static_cast<int>(followers.size()); ++i) {
                if (followers[i].first == pair.first) {
                    a = i;
                }
                if (followers[i].first == pair.second) {
                    b = i;
                }
            }
            if (a >= 0 && b >= 0) {
                pairs.push_back({a, b});
            }
        }
        auto roles = AssignNearestRoles(actors, goals, previous);
        if (!pairs.empty()) {

            auto paired = PairNearbyRoles(actors, goals, previous, pairs, intent.Direction());
            if (paired.empty()) {
                paired = PairNearbyRoles(actors, goals, roles, pairs, intent.Direction());
            }
            if (!paired.empty()) {
                roles = std::move(paired);
            }
        }
        bool changed = false;
        for (std::size_t i = 0; i < roles.size(); ++i) {
            auto& f = *followers[i].second;
            if (f.role != roles[i]) {
                if (settings.traceMovement) {
                    logger::info("[Roles] {:08X} packageSlot={} role={} -> {}", followers[i].first, f.slot,
                                 f.role, roles[i]);
                }
                f.role = roles[i];
                changed = true;
            }
        }

        if (changed) {
            roleCooldown = pairs.empty() ? 1.0F : 3.0F;
        }
    }

    bool FormationController::SetHandApproachRadius(ManagedFollower& f, bool active)
    {
        if (!active && !f.handArrivalRadius.original) {
            return true;
        }
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* package = data ? data->LookupForm<RE::TESPackage>(PackageLocal(f.slot),
                                                                Settings::GetSingleton().Get().pluginName)
                             : nullptr;
        auto* custom = package ? skyrim_cast<RE::TESCustomPackageData*>(package->data) : nullptr;
        if (custom && custom->data.data && custom->data.uids) {
            for (std::uint16_t i = 0; i < custom->data.dataSize; ++i) {
                if (custom->data.uids[i] != 0) {
                    continue;
                }
                auto* input = skyrim_cast<RE::BGSPackageDataLocation*>(custom->data.data[i]);
                auto* location = input ? input->pointer : nullptr;
                if (!location || location->locType != RE::PackageLocation::Type::kAlias_Reference) {
                    break;
                }
                if (f.handArrivalRadius.Update(location->rad, active)) {
                    // An already-running route can cache the old stopping radius.
                    f.goal.hasRoute = false;
                    f.goal.sinceCommit = 100;
                    logger::info("[HandHolding/Approach] slot={} package={:08X} arrivalRadius={} active={}",
                                 f.slot, package->GetFormID(), location->rad, active);
                }
                return true;
            }
        }
        logger::warn("[HandHolding/Approach] slot={} travel location unavailable; approach refused", f.slot);
        return false;
    }

    void FormationController::ApplyTravel(ManagedFollower& f, RE::Actor& actor, RE::PlayerCharacter& player,
                                          float dt)
    {
        const auto& settings = Settings::GetSingleton().Get();
        f.anchorSet = false;
        f.restAnchorZSet = false;
        auto* quest = EnsureQuest();
        if (!quest || !quest->IsRunning()) {
            ReleaseTravel(f, &actor);
            return;
        }
        auto* package = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(PackageLocal(f.slot),
                                                                                       settings.pluginName);
        if (!package) {
            ReleaseTravel(f, &actor);
            return;
        }
        if (f.sandboxActive) {
            f.avoidance.Reset();
            EndRestPose(f, &actor);
            f.restRecovery.Reset();
            f.activitySearch.Reset();
            f.activityTarget = {};
            f.socialHold = false;
            f.socialPhase = 0;
            f.patron = {};
            f.patronGesture = false;
            SetSocialFlag(f.slot, 0);
            f.sandboxActive = false;
            f.sandboxRadius = 0;
            f.goal.Release();

            f.travelActive = false;
            f.speedScale = 1;
            TravelSpeed::Clear(static_cast<std::uint32_t>(f.slot));
        }
        const auto pos = actor.GetPosition();
        const auto playerPos = player.GetPosition();
        const auto handApproach = HandHolding::GetApproach();
        const bool handGuided = handApproach.actor == actor.GetFormID();
        if (!SetHandApproachRadius(f, handGuided)) {
            ReleaseTravel(f, &actor);
            return;
        }
        const auto travelTuning = HandHolding::ApproachTuning(settings.travel, handGuided);
        const bool interior = player.GetParentCell()->IsInteriorCell();
        const float playerDistance = Distance2D(pos, playerPos);
        f.distantCatchup = DistantCatchup(f.distantCatchup, playerDistance, settings.distantCatchupStart,
                                          settings.distantCatchupEnd);
        const bool urgent = UrgentIndividualTravel(playerDistance, settings.distantCatchupEnd, f.goal.active,
                                                   Length({f.goal.goal.x - pos.x, f.goal.goal.y - pos.y}),
                                                   intent.RoutingLead());
        if (f.individual.Initialize(TravelIdentity(actor))) {
            const auto& p = f.individual.Disposition();
            logger::info("[IndividualTravel] {} reaction={:.2f}s turnDelay={:.2f}s turnRate={:.2f} "
                         "cruise={:.3f} catchGap={:.0f} settleGap={:.0f}",
                         actor.GetName(), p.reaction, p.turnDelay, p.turnSharpness, p.cruise, p.catchGap,
                         p.settleGap);
        }
        f.individual.Update(intent.Direction(), dt, intent.IsMoving(), urgent, interior,
                            settings.travel.individuality);
        const Vec2 actorXY{pos.x, pos.y};
        const bool ownsPackage = actor.GetCurrentPackage() == package;
        f.goal.Tick(actorXY, dt, ownsPackage, travelTuning);
        if (f.goal.recoveryTime > 0.0F || f.recoveryReleasePending) {
            if (f.travelActive) {
                logger::info("[Travel] {} stalled; yielding for recovery; activePackage={}", actor.GetName(),
                             DescribePackage(actor.GetCurrentPackage()));
                f.recoveryReleasePending = true;
            }

            if (f.recoveryReleasePending) {
                auto* alias = GetRefAlias(quest, FollowerAlias(f.slot));
                if (!alias || alias->GetReference() != &actor) {
                    f.recoveryReleasePending = false;
                    f.goal.recoveryTime = std::max(f.goal.recoveryTime, .5F);
                }
            }
            ReleaseTravel(f, &actor);
            return;
        }

        if (!f.travelActive && !f.individual.Ready(urgent, settings.travel.individuality)) {
            return;
        }
        const auto planned =
            PartyRole(mode, f.role >= 0 ? f.role : f.slot, travelPartyCount, naturalPatternSeed,
                      settings.travel.naturalStragglerDistance, actor.GetFormID());
        auto offset = planned.offset;
        const float setback = planned.straggler ? settings.travel.naturalStragglerDistance : 0;
        f.roleSetback =
            f.haveRoleOffset ? BlendRoleOffset({0, f.roleSetback}, {0, setback}, dt).longitudinal : setback;
        f.roleOffset = f.haveRoleOffset ? BlendRoleOffset(f.roleOffset, offset, dt) : offset;
        f.haveRoleOffset = true;
        const float rearBlend =
            std::clamp(f.roleSetback / std::max(1.0F, settings.travel.naturalStragglerDistance), 0.0F, 1.0F);
        const auto personalIntent = intent.WithDirection(f.individual.Direction());
        Vec2 desired = personalIntent.GoalAt(mode, f.roleOffset, interior, settings.preferredSide,
                                             settings.travel, f.roleSetback, rearBlend);
        const auto direction = handGuided ? ForwardFromYaw(player.GetAngleZ()) : personalIntent.Direction();
        if (handGuided) {
            desired =
                HandHolding::ApproachPoint({playerPos.x, playerPos.y}, player.GetAngleZ(), handApproach.side);
        }
        const Vec2 ordinaryAim = handGuided ? desired : personalIntent.RouteGoal(desired, mode, interior);
        const Vec2 rearAim =
            handGuided ? desired : personalIntent.RouteGoal(desired, FormationMode::kRear, interior);
        const Vec2 aim{ordinaryAim.x + (rearAim.x - ordinaryAim.x) * rearBlend,
                       ordinaryAim.y + (rearAim.y - ordinaryAim.y) * rearBlend};
        const float lead = Length({aim.x - desired.x, aim.y - desired.y});
        ObstaclePlan corridor;
        auto target = settings.forwardCollision && !handGuided
                          ? navigation.ResolveClear(player, aim, direction, corridor)
                          : navigation.Resolve(playerPos, aim, direction);
        bool connectedRoute = false;
        if (settings.forwardCollision && !handGuided) {
            const auto searchDirection =
                mode == FormationMode::kLead
                    ? direction
                    : Normalize({aim.x - playerPos.x, aim.y - playerPos.y}, direction);
            const float advance = target ? (target->x - playerPos.x) * searchDirection.x +
                                               (target->y - playerPos.y) * searchDirection.y
                                         : 0;
            const float requestedAdvance =
                (aim.x - playerPos.x) * searchDirection.x + (aim.y - playerPos.y) * searchDirection.y;
            if (requestedAdvance >= 160 && advance < std::clamp(requestedAdvance * .7F, 110.0F, 240.0F)) {
                if (auto ahead = navigation.ResolveConnected(player, aim, searchDirection)) {
                    target = ahead;
                    connectedRoute = true;
                    if (settings.traceMovement && traceDue) {
                        logger::info("[Corridor] {} uses connected navmesh destination beyond bend "
                                     "({:.0f},{:.0f},{:.0f})",
                                     actor.GetName(), ahead->x, ahead->y, ahead->z);
                    }
                }
            }
        }
        if (settings.traceMovement && traceDue && settings.forwardCollision && !handGuided) {
            logger::info("[Corridor] {} interior={} desired=({:.0f},{:.0f}) usable={} blocker={:08X} "
                         "rays={} navRejects={} queryFailed={}",
                         actor.GetName(), interior, aim.x, aim.y, target.has_value(), corridor.blocker,
                         corridor.rays, corridor.navRejects, corridor.queryFailed);
            logger::info("[CorridorPosition] {} cell={:08X} actor=({:.0f},{:.0f},{:.0f}) "
                         "player=({:.0f},{:.0f},{:.0f}) target=({:.0f},{:.0f},{:.0f}) connected={}",
                         actor.GetName(), player.GetParentCell()->GetFormID(), pos.x, pos.y, pos.z,
                         playerPos.x, playerPos.y, playerPos.z, target ? target->x : aim.x,
                         target ? target->y : aim.y, target ? target->z : playerPos.z, connectedRoute);
        }
        if (!target) {
            LogDiagnostic(actor.GetFormID(), actor.GetName(), "no usable travel corridor; yielding");
            ReleaseTravel(f, &actor);
            f.goal.recoveryTime = 1.0F;
            return;
        }
        bool obstacleChanged = false;
        const bool localObstacles =
            settings.forwardCollision &&
            UseLocalObstacleSteering(!interior && ContextFor(player) == RestContext::Wilderness &&
                                         ContextFor(actor) == RestContext::Wilderness,
                                     handGuided, f.distantCatchup, playerDistance);
        if (localObstacles && !connectedRoute) {
            RE::NiPoint3 velocity{};
            actor.GetLinearVelocity(velocity);
            auto avoidance = navigation.AvoidObstacles(
                actor, *target, std::max(intent.Speed(), std::hypot(velocity.x, velocity.y)), dt,
                travelTuning.arrivalRadius, f.avoidance);
            obstacleChanged = avoidance.changed;
            if (avoidance.route == ObstacleRoute::Blocked) {
                logger::info(
                    "[Obstacle] {} ({:08X}): local corridor unavailable; retaining native travel route at "
                    "({:.0f}, {:.0f}, {:.0f}); blocker={:08X} rays={} navRejects={} queryFailed={}",
                    actor.GetName(), actor.GetFormID(), pos.x, pos.y, pos.z, avoidance.blocker,
                    avoidance.rays, avoidance.navRejects, avoidance.queryFailed);

                avoidance = f.avoidance.UseNative({target->x, target->y, target->z});
                obstacleChanged = true;
            }
            target = RE::NiPoint3{avoidance.target.x, avoidance.target.y, avoidance.target.z};
            if (obstacleChanged || (traceDue && settings.traceMovement && f.avoidance.active)) {
                logger::info("[Obstacle] {} ({:08X}): route={} side={} speed={:.0f} actor=({:.0f}, {:.0f}, "
                             "{:.0f}) target=({:.0f}, {:.0f}, {:.0f}) blocker={:08X} rays={} navRejects={}",
                             actor.GetName(), actor.GetFormID(),
                             avoidance.route == ObstacleRoute::Native ? "native"
                             : f.avoidance.active                     ? "detour"
                                                                      : "clear",
                             f.avoidance.side, std::hypot(velocity.x, velocity.y), pos.x, pos.y, pos.z,
                             target->x, target->y, target->z, avoidance.blocker, avoidance.rays,
                             avoidance.navRejects);
            }
        }
        else {
            obstacleChanged = f.avoidance.active;
            f.avoidance.Reset();
        }
        auto* marker = EnsureMarker(f, player);
        if (!marker) {
            ReleaseTravel(f, &actor);
            return;
        }
        if (settings.conversationAwareness) {
            PrepareAuxMarker(f, actor, pos);
        }
        const Vec2 candidate{target->x, target->y};
        const float distance = Length({candidate.x - pos.x, candidate.y - pos.y});

        const float routedLead = std::clamp(
            (target->x - desired.x) * direction.x + (target->y - desired.y) * direction.y, 0.0F, lead);
        const float paceDistance =
            f.avoidance.active ? std::min(distance, 100.0F) : std::max(0.0F, distance - routedLead);
        auto pace = ChoosePace(intent.Speed(), paceDistance, f.goal.pace, intent.IsMoving());
        if (f.avoidance.active && pace == TravelPace::kRun) {
            pace = TravelPace::kJog;
        }
        if (obstacleChanged ||
            f.goal.ShouldCommit(candidate, actorXY, pace, travelTuning, intent.Speed(), intent.IsMoving())) {

            package->packData.packFlags.set(RE::PACKAGE_DATA::GeneralFlag::kPreferredSpeed);
            package->packData.maxSpeed = static_cast<RE::PACKAGE_DATA::PreferredSpeed>(pace);
            if (marker->GetParentCell() != player.GetParentCell()) {
                marker->MoveTo(&player);
            }
            marker->SetPosition({target->x, target->y, target->z});
            f.goal.Commit(candidate, actorXY, pace);
            f.travelActive = true;
        }
        if (f.goal.ShouldRepath(actorXY, intent.Speed(), intent.IsMoving(), ownsPackage, travelTuning) ||
            (obstacleChanged && ownsPackage && f.goal.sinceRepath >= .30F)) {
            auto* followerAlias = GetRefAlias(quest, FollowerAlias(f.slot));
            if (followerAlias && followerAlias->GetReference() == &actor) {

                marker->SetPosition({target->x, target->y, target->z});
                package->packData.maxSpeed = static_cast<RE::PACKAGE_DATA::PreferredSpeed>(pace);
                f.goal.Commit(candidate, actorXY, pace);

                actor.EvaluatePackage(false, true);
                if (settings.traceMovement && f.goal.stalledTime >= 1.0F) {
                    logger::info("[TravelRetry] {} stalled={:.1f} routeAge={:.2f} ownsPackage={}",
                                 actor.GetName(), f.goal.stalledTime, f.goal.sinceRepath, ownsPackage);
                }
                f.goal.MarkRepathed();
            }
        }
        if (f.travelActive) {

            const auto facing = ForwardFromYaw(actor.GetAngle().z);
            const auto towardGoal = Normalize({candidate.x - pos.x, candidate.y - pos.y}, facing);
            const float alignment = facing.x * towardGoal.x + facing.y * towardGoal.y;
            const auto speedPace = f.goal.hasRoute ? f.goal.submittedPace : f.goal.pace;
            const float normalScale =
                f.individual.SpeedScale(intent.Speed(), paceDistance, speedPace, intent.IsMoving(),
                                        settings.travel, alignment, urgent);
            float desiredScale = TravelSpeedTarget(normalScale, settings.travel.maxSpeedScale,
                                                   f.distantCatchup, Distance2D(pos, playerPos), paceDistance,
                                                   settings.distantCatchupStart, settings.distantCatchupEnd);
            if (f.avoidance.active) {
                desiredScale = std::min(desiredScale, 1.15F);
            }
            f.speedScale = SmoothSpeedScale(f.speedScale, desiredScale, dt);

            f.speedScale = std::min(f.speedScale, desiredScale + .12F);
            TravelSpeed::Set(static_cast<std::uint32_t>(f.slot), actor.GetFormID(), f.speedScale,
                             settings.travel.maxSpeedScale, settings.distantCatchupEnd);
        }
        if (settings.traceMovement && traceDue) {
            if (handGuided) {
                logger::info("[HandHolding/Approach] {} target=({:.1f},{:.1f}) remaining={:.1f} "
                             "radius={:.0f} ownsPackage={} normalPace=true",
                             actor.GetName(), candidate.x, candidate.y, distance, travelTuning.arrivalRadius,
                             ownsPackage);
            }
            RE::NiPoint3 velocity{};
            actor.GetLinearVelocity(velocity);
            const auto forward = ForwardFromYaw(actor.GetAngle().z);
            auto* alias = GetRefAlias(quest, FollowerAlias(f.slot));
            const float forwardSpeed = velocity.x * forward.x + velocity.y * forward.y;
            logger::info(
                "[Travel] {} slot={} phase={} pace={} speed={:.0f} forwardSpeed={:.0f} goalDistance={:.0f} "
                "alias={} ownPackage={} stalled={:.1f} playerSpeed={:.0f} speedScale={:.2f} "
                "pathDistance={:.0f} targetAge={:.2f} routeAge={:.2f} steeringLead={:.0f} distantCatchup={}",
                actor.GetName(), f.slot, static_cast<int>(intent.Phase()), static_cast<int>(f.goal.pace),
                Length({velocity.x, velocity.y}), forwardSpeed, distance,
                alias && alias->GetReference() == &actor, ownsPackage, f.goal.stalledTime, intent.Speed(),
                f.speedScale, Length({f.goal.submittedGoal.x - pos.x, f.goal.submittedGoal.y - pos.y}),
                f.goal.sinceCommit, f.goal.sinceRepath, routedLead, f.distantCatchup);
            const auto right = RightFromForward(direction);
            logger::info("[IndividualPace] {} catchingUp={} heading=({:.2f},{:.2f}) strength={:.2f}",
                         actor.GetName(), f.individual.CatchingUp(), direction.x, direction.y,
                         settings.travel.individuality);
            logger::info(
                "[Spacing] {} role={} scale={:.2f} desiredSide={:.0f} groundSide={:.0f} straggler={}",
                actor.GetName(), f.role, settings.travel.spacing,
                (desired.x - playerPos.x) * right.x + (desired.y - playerPos.y) * right.y,
                (target->x - playerPos.x) * right.x + (target->y - playerPos.y) * right.y, planned.straggler);
            if (!ownsPackage) {
                logger::info("[TravelOwner] {} expected={:08X} active={}", actor.GetName(),
                             package->GetFormID(), DescribePackage(actor.GetCurrentPackage()));
            }
        }
    }

    void FormationController::SetSandboxFlag(bool active)
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* flag =
            data ? data->LookupForm<RE::TESGlobal>(SANDBOX_GLOBAL, Settings::GetSingleton().Get().pluginName)
                 : nullptr;
        if (flag) {
            flag->value = active ? 1.0F : 0.0F;
        }
    }

    void FormationController::SetPoseFlag(int slot, int kind)
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* flag = data ? data->LookupForm<RE::TESGlobal>(PoseGlobalLocal(slot),
                                                            Settings::GetSingleton().Get().pluginName)
                          : nullptr;
        if (flag) {
            flag->value = static_cast<float>(kind);
        }
    }

    void FormationController::EndRestPose(ManagedFollower& f, RE::Actor* actor)
    {
        if (f.restPose.kind == 1 && f.restPose.started && actor && actor->Is3DLoaded() && !actor->IsDead() &&
            !actor->IsInCombat() && !actor->GetCurrentScene() && !IsSpeaking(*actor) &&
            !ShouldSuspendForDialogue()) {
            actor->NotifyAnimationGraph("IdleStop");
        }
        if (f.restPose.kind) {
            f.restPose.End(actor ? actor->GetFormID() : static_cast<std::uint32_t>(f.slot));
            if (f.activity == RestActivity::Eat || f.activity == RestActivity::Drink) {
                f.restPose.cooldown += 30;
            }
        }
        SetPoseFlag(f.slot, 0);
        f.restIdleRequested = false;
    }

    RE::TESObjectREFR* FormationController::GetRestSeat(std::uint32_t slot)
    {
        std::lock_guard lock(stateMutex);
        if (slot >= PARTY_CAPACITY || !Settings::GetSingleton().Get().sandbox.extraPoses) {
            return nullptr;
        }
        return seatPool[slot].get().get();
    }

    RE::TESObjectREFR* FormationController::EnsureRestSeat(ManagedFollower& f, RE::Actor& actor,
                                                           RE::NiPoint3 point)
    {
        auto& pooled = seatPool[f.slot];
        if (!pooled.get()) {
            if (auto* alias = GetRefAlias(EnsureQuest(), SeatAlias(f.slot))) {
                auto* saved = alias->GetReference();
                if (saved && !saved->IsDeleted() && saved->GetBaseObject() &&
                    saved->GetBaseObject()->GetFormID() == 0x108D3C) {
                    pooled = saved->GetHandle();
                }
            }
        }
        if (auto ref = pooled.get()) {
            bool occupied = false;
            if (auto* lists = RE::ProcessLists::GetSingleton()) {
                Engine::ForEachHighActor(*lists, [&](RE::Actor& other) {
                    if (other.GetOccupiedFurniture().get().get() == ref.get()) {
                        occupied = true;
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
            if (occupied) {
                return nullptr;
            }
            ref->MoveTo(&actor);
            ref->SetPosition(point);
            return ref.get();
        }
        auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(0x108D3C);
        if (!base) {
            return nullptr;
        }
        pooled = RE::TESDataHandler::GetSingleton()->CreateReferenceAtLocation(
            base, point, actor.GetAngle(), actor.GetParentCell(), actor.GetWorldspace(), nullptr, nullptr,
            RE::ObjectRefHandle{}, true, false);
        return pooled.get().get();
    }

    void FormationController::UpdateRestPose(ManagedFollower& f, RE::Actor& actor, float dt)
    {
        if (f.restRecovery.walking || lookout.actor == actor.GetFormID()) {
            return;
        }
        const auto& tuning = Settings::GetSingleton().Get().sandbox;
        const auto context = ContextFor(actor);
        const bool refreshments = SettledRefreshments(context, tuning);
        if (!tuning.extraPoses && (!refreshments || (f.restPose.kind && f.activity != RestActivity::Eat &&
                                                     f.activity != RestActivity::Drink))) {
            if (f.restPose.kind) {
                EndRestPose(f, &actor);
            }
            return;
        }
        auto& pose = f.restPose;
        const auto id = actor.GetFormID();
        pose.Init(id);
        auto* state = actor.AsActorState();
        if (!state || actor.IsInCombat() || actor.GetCurrentScene() || IsSpeaking(actor) ||
            state->IsWeaponDrawn() || actor.IsSneaking() || state->IsSwimming() || actor.IsOnMount()) {
            EndRestPose(f, &actor);
            return;
        }
        auto* data = RE::TESDataHandler::GetSingleton();
        const auto& plugin = Settings::GetSingleton().Get().pluginName;
        if (pose.kind) {
            auto* package = data->LookupForm<RE::TESPackage>(
                pose.kind == 2 ? SeatPackageLocal(f.slot) : PosePackageLocal(f.slot), plugin);
            bool ready = actor.GetCurrentPackage() == package;
            if (pose.kind == 2) {
                auto seat = seatPool[f.slot].get();
                ready = ready && seat && actor.GetOccupiedFurniture().get().get() == seat.get() &&
                        state->GetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSitting;
            }
            else if (!pose.started) {
                ready = false;
            }
            const bool wasStarted = pose.started;
            if (pose.Tick(dt, ready)) {
                if (!wasStarted) {
                    logger::info("[RestPose] {} entry unavailable; returning to sandbox", actor.GetName());
                }
                EndRestPose(f, &actor);
                return;
            }
            if (pose.started && !wasStarted) {
                logger::info("[RestPose] {} kind={} holding for {:.0f}s", actor.GetName(), pose.kind,
                             pose.duration);
            }
            return;
        }

        if (f.socialHold || social.Listener(id)) {
            pose.cooldown = 12.0F + RestPose::Mix(id + pose.sequence) % 13;
            return;
        }
        pose.Tick(dt, false);
        if (pose.cooldown > 0 || social.Listener(id) || actor.GetOccupiedFurniture().get() ||
            state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) {
            return;
        }
        auto* npc = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13794);
        if (!actor.GetRace() || !npc || !actor.GetRace()->HasKeyword(npc)) {
            return;
        }
        if (actor.GetCurrentPackage() !=
            data->LookupForm<RE::TESPackage>(SandboxPackageLocal(f.slot), plugin)) {
            return;
        }
        RE::NiPoint3 velocity{};
        actor.GetLinearVelocity(velocity);
        if (Length({velocity.x, velocity.y}) > 12) {
            return;
        }
        bool driven = false;
        actor.GetGraphVariableBool("bAnimationDriven", driven);
        if (driven || f.patronTimer > 0) {
            return;
        }

        if (refreshments && std::count_if(managed.begin(), managed.end(), [](const auto& entry) {
                                return entry.second.restPose.kind &&
                                       (entry.second.activity == RestActivity::Eat ||
                                        entry.second.activity == RestActivity::Drink);
                            }) >= 2) {
            return;
        }
        int kind = 1;
        const auto roll = RestPose::Mix(id + pose.sequence * 3571);
        f.activity = !tuning.extraPoses && refreshments ? ChooseRefreshment(context, roll)
                                                        : ChooseRestActivity(context, roll, tuning);
        if (f.activity == RestActivity::Ground) {
            auto point = navigation.RestPoint(actor.GetPosition());
            float water{};
            if (point && !(actor.GetParentCell()->GetWaterHeight(*point, water) && water >= point->z - 2)) {
                bool clear = true;
                for (auto& [otherID, other] : managed) {
                    auto otherActor = other.handle.get();
                    if (otherID != id && otherActor && Distance2D(otherActor->GetPosition(), *point) < 100) {
                        clear = false;
                        break;
                    }
                }
                if (clear && EnsureRestSeat(f, actor, *point)) {
                    kind = 2;
                }
            }
        }
        if (!data->LookupForm<RE::TESGlobal>(PoseGlobalLocal(f.slot), plugin) ||
            !data->LookupForm<RE::TESPackage>(kind == 2 ? SeatPackageLocal(f.slot) : PosePackageLocal(f.slot),
                                              plugin)) {
            return;
        }
        if (kind != 2 && f.activity == RestActivity::Ground) {
            ++pose.sequence;
            pose.cooldown = 15;
            return;
        }
        if (f.activity == RestActivity::Stand) {
            const bool occupied = std::any_of(managed.begin(), managed.end(), [](const auto& entry) {
                return entry.second.restPose.kind == 1 && entry.second.activity == RestActivity::Stand;
            });
            if (occupied || handsBackCooldown > 0) {
                ++pose.sequence;
                pose.cooldown = 12.0F + RestPose::Mix(id + pose.sequence) % 13;
                return;
            }
            handsBackCooldown = 90.0F + RestPose::Mix(id + pose.sequence) % 61;
        }
        pose.Begin(kind, id);
        f.restIdleRequested = false;
        if (FiniteRestActivity(f.activity)) {
            pose.duration = 7.0F + RestPose::Mix(id + pose.sequence) % 4;
        }
        SetPoseFlag(f.slot, kind);
    }

    void FormationController::SetSocialFlag(int slot, int phase)
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* flag = data ? data->LookupForm<RE::TESGlobal>(SocialGlobalLocal(slot),
                                                            Settings::GetSingleton().Get().pluginName)
                          : nullptr;
        if (flag) {
            flag->value = static_cast<float>(phase);
        }
    }

    void FormationController::EndConversationAwareness()
    {
        if (!conversationSpeaker) {
            return;
        }
        for (auto& [id, f] : managed) {
            if (f.awarenessActive) {
                SetSocialFlag(f.slot, f.awarenessPreviousPhase);
                if (auto actor = f.handle.get(); actor && !actor->GetCurrentScene() && !IsSpeaking(*actor)) {
                    auto* stroll = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                        0x870 + f.slot, Settings::GetSingleton().Get().pluginName);
                    if (actor->GetCurrentPackage() == stroll) {
                        actor->EvaluatePackage(false, true);
                    }
                }
            }
            f.awarenessActive = f.awarenessWalking = f.awarenessTried = false;
            f.awarenessTime = 0;
        }
        conversationSpeaker = 0;
        awarenessUpdateTimer = 0;
    }

    void FormationController::UpdateConversationAwareness(RE::PlayerCharacter& player, float dt)
    {
        const auto& settings = Settings::GetSingleton().Get();
        auto* ui = RE::UI::GetSingleton();
        auto* topics = RE::MenuTopicManager::GetSingleton();
        if (!enabled || !settings.conversationAwareness || !ui || ui->GameIsPaused() ||
            !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) || !topics) {
            return;
        }
        auto speakerRef = topics->speaker.get();
        auto* speaker = speakerRef ? speakerRef->As<RE::Actor>() : nullptr;
        if (!speaker || speaker->IsPlayerRef() || !speaker->Is3DLoaded() ||
            !InSameTravelSpace(*speaker, player)) {
            return;
        }

        conversationSpeaker = speaker->GetFormID();
        awarenessUpdateTimer += dt;
        if (awarenessUpdateTimer < .2F) {
            return;
        }
        const float elapsed = awarenessUpdateTimer;
        awarenessUpdateTimer = 0;
        const auto p = player.GetPosition(), s = speaker->GetPosition();
        if (Distance2D(p, s) > 600) {
            return;
        }
        bool walking = false;
        for (auto& [id, f] : managed) {
            if (f.awarenessWalking) {
                if (id == speaker->GetFormID()) {
                    walking = true;
                    continue;
                }
                auto actor = f.handle.get();
                auto target = gatherPool[f.slot].get();
                if (!actor || !target) {
                    f.awarenessWalking = false;
                    continue;
                }
                f.awarenessTime += elapsed;
                if (Distance2D(actor->GetPosition(), target->GetPosition()) < 75) {
                    f.awarenessWalking = false;
                }
                else if (f.awarenessTime >= 15 && !actor->GetCurrentScene() && !IsSpeaking(*actor)) {
                    SetSocialFlag(f.slot, f.awarenessPreviousPhase);
                    f.awarenessActive = f.awarenessWalking = false;
                    auto* stroll = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                        0x870 + f.slot, settings.pluginName);
                    if (actor->GetCurrentPackage() == stroll) {
                        actor->EvaluatePackage(false, true);
                    }
                    logger::info("[ConversationSpace] {} could not clear the path; yielding",
                                 actor->GetName());
                }
                walking |= f.awarenessWalking;
            }
        }
        if (walking) {
            return;
        }
        navigation.Collect(player);
        for (auto& [id, f] : managed) {
            if (id == speaker->GetFormID() || f.awarenessTried || !f.travelActive || f.restPose.kind ||
                f.restRecovery.walking || social.Listener(id) || lookout.actor == id) {
                continue;
            }
            auto actor = f.handle.get();
            auto target = gatherPool[f.slot].get();
            if (!actor || !target || !actor->Is3DLoaded() || !InSameTravelSpace(*actor, player) ||
                actor->IsDead() || actor->IsInCombat() || actor->GetCurrentScene() ||
                actor->GetOccupiedFurniture().get() || IsSpeaking(*actor)) {
                continue;
            }
            auto* state = actor->AsActorState();
            bool driven = false;
            actor->GetGraphVariableBool("bAnimationDriven", driven);
            if (!state || driven || state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal ||
                state->IsSwimming() || state->IsWeaponDrawn() || actor->IsOnMount()) {
                continue;
            }
            const auto pos = actor->GetPosition();
            if (std::abs(pos.z - p.z) > 150 || !IntrudesConversation({pos.x, pos.y}, {p.x, p.y}, {s.x, s.y},
                                                                     settings.conversationClearance)) {
                continue;
            }

            auto* quest = questCache;
            if (!quest || !quest->IsRunning()) {
                continue;
            }
            auto* followerAlias = GetRefAlias(quest, FollowerAlias(f.slot));
            auto* destinationAlias = GetRefAlias(quest, 30 + f.slot);
            if (!followerAlias || followerAlias->GetReference() != actor.get() || !destinationAlias ||
                destinationAlias->GetReference() != target.get()) {
                continue;
            }
            auto* data = RE::TESDataHandler::GetSingleton();
            auto* current = actor->GetCurrentPackage();
            auto* travel = data->LookupForm<RE::TESPackage>(PackageLocal(f.slot), settings.pluginName);
            auto* rest = data->LookupForm<RE::TESPackage>(SandboxPackageLocal(f.slot), settings.pluginName);
            auto* stroll = data->LookupForm<RE::TESPackage>(0x870 + f.slot, settings.pluginName);
            if ((current != travel && current != rest) || !stroll) {
                continue;
            }
            f.awarenessTried = true;
            const auto desired =
                ConversationExit({pos.x, pos.y}, {p.x, p.y}, {s.x, s.y}, settings.conversationClearance, id);
            auto point = navigation.Resolve(pos, desired, Normalize({desired.x - pos.x, desired.y - pos.y}));
            if (!point || IntrudesConversation({point->x, point->y}, {p.x, p.y}, {s.x, s.y},
                                               settings.conversationClearance + 40)) {
                continue;
            }
            float water{};
            if (actor->GetParentCell()->GetWaterHeight(*point, water) && water >= point->z - 2) {
                continue;
            }
            bool clear = true;
            for (const auto& [otherID, other] : managed) {
                if (otherID != id) {
                    if (auto otherActor = other.handle.get();
                        otherActor && Distance2D(otherActor->GetPosition(), *point) < 100) {
                        clear = false;
                        break;
                    }
                }
            }
            if (!clear) {
                continue;
            }
            target->MoveTo(actor.get());
            target->SetPosition(*point);
            stroll->packData.packFlags.set(RE::PACKAGE_DATA::GeneralFlag::kPreferredSpeed);
            stroll->packData.maxSpeed = RE::PACKAGE_DATA::PreferredSpeed::kWalk;
            f.awarenessPreviousPhase = f.socialPhase;
            f.awarenessActive = f.awarenessWalking = true;
            f.awarenessTime = 0;
            SetSocialFlag(f.slot, 2);
            actor->EvaluatePackage(false, true);
            logger::info("[ConversationSpace] {} steps aside; protected speaker={:08X}", actor->GetName(),
                         speaker->GetFormID());
            break;
        }
    }

    void FormationController::UpdateLookout(RE::PlayerCharacter& player, float dt)
    {
        const auto& settings = Settings::GetSingleton().Get();
        const auto context = ContextFor(player);
        int present = 0;
        std::vector<WatchCandidate> candidates;
        for (const auto& [id, f] : managed) {
            if (auto actor = f.handle.get(); actor && f.sandboxActive && IsActorUsable(*actor, player)) {
                auto* npc = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13794);
                if (!actor->GetRace() || !npc || !actor->GetRace()->HasKeyword(npc)) {
                    continue;
                }
                ++present;
                auto* state = actor->AsActorState();
                RE::NiPoint3 motion{};
                actor->GetLinearVelocity(motion);
                bool driven = false;
                actor->GetGraphVariableBool("bAnimationDriven", driven);
                if (!state || actor->IsInCombat() || actor->IsDead() || state->IsWeaponDrawn() ||
                    actor->IsSneaking() || actor->IsOnMount() || state->IsSwimming() ||
                    actor->GetOccupiedFurniture().get() ||
                    state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal || actor->GetCurrentScene() ||
                    IsSpeaking(*actor) || f.restPose.kind || f.restRecovery.walking || social.Listener(id) ||
                    f.patronTimer > 0 || Length({motion.x, motion.y}) > 12 || driven) {
                    continue;
                }
                auto* current = actor->GetCurrentPackage();
                auto* expected = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                    id == lookout.actor ? PosePackageLocal(f.slot) : SandboxPackageLocal(f.slot),
                    settings.pluginName);
                auto* native = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                    SandboxPackageLocal(f.slot), settings.pluginName);
                if (current != expected && !(id == lookout.actor && current == native)) {
                    continue;
                }
                candidates.push_back({id, WatchWeight(PersonalityFor(*actor))});
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](auto a, auto b) { return a.id < b.id; });
        const auto previous = lookout.actor;
        lookout.Update(dt,
                       LookoutPermitted(settings.lookouts, sandbox.active, sandbox.expanded,
                                        context == RestContext::Wilderness || context == RestContext::Dungeon,
                                        present, player.IsInCombat()),
                       sandbox.activeTime, settings.lookoutAfter, candidates);
        if (previous && previous != lookout.actor) {
            if (auto found = managed.find(previous); found != managed.end()) {
                SetPoseFlag(found->second.slot, found->second.restPose.kind);
                logger::info("[Lookout] {:08X} finished watching", previous);
            }
        }
        if (lookout.actor && previous != lookout.actor) {
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(lookout.actor)) {
                const auto pos = actor->GetPosition();
                const auto p = player.GetPosition();
                const auto facing =
                    Normalize({pos.x - p.x, pos.y - p.y}, ForwardFromYaw(actor->GetAngle().z));
                Engine::SetHeading(*actor, std::atan2(facing.x, facing.y));
                logger::info("[Lookout] {} keeping watch for {:.0f}s", actor->GetName(), lookout.duration);
            }
        }
    }

    RE::TESObjectREFR* FormationController::PrepareAuxMarker(ManagedFollower& f, RE::Actor& actor,
                                                             RE::NiPoint3 point, bool move)
    {
        auto& pooled = gatherPool[f.slot];
        if (!pooled.get()) {
            if (auto* alias = GetRefAlias(EnsureQuest(), 30 + f.slot)) {
                auto* saved = alias->GetReference();
                if (saved && !saved->IsDeleted() && saved->GetBaseObject() &&
                    saved->GetBaseObject()->GetFormID() == 0x3B) {
                    pooled = saved->GetHandle();
                }
            }
        }
        if (!pooled.get()) {
            auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(0x3B);
            if (!base) {
                return nullptr;
            }
            pooled = RE::TESDataHandler::GetSingleton()->CreateReferenceAtLocation(
                base, point, RE::NiPoint3{}, actor.GetParentCell(), actor.GetWorldspace(), nullptr, nullptr,
                RE::ObjectRefHandle{}, true, false);
        }
        auto marker = pooled.get();
        if (!marker) {
            return nullptr;
        }
        if (move) {
            marker->MoveTo(&actor);
            marker->SetPosition(point);
        }
        return marker.get();
    }

    std::optional<RE::NiPoint3> FormationController::FindRestActivity(ManagedFollower& f, RE::Actor& actor,
                                                                      RE::TESObjectREFR& anchor)
    {
        auto* cell = actor.GetParentCell();
        if (!cell) {
            return std::nullopt;
        }
        const auto origin = actor.GetPosition();
        auto center = anchor.GetPosition();
        if (f.restAnchorZSet) {
            center.z = f.restAnchorZ;
        }
        const float height = Settings::GetSingleton().Get().sandbox.activityHeight;
        std::unordered_set<RE::FormID> occupied;
        std::vector<RE::NiPoint3> people;
        struct Opportunity
        {
            RE::ObjectRefHandle handle;
            float score;
            bool patron;
        };
        std::vector<Opportunity> choices;
        auto claimed = [&](RE::FormID id) {
            return std::any_of(managed.begin(), managed.end(), [&](const auto& entry) {
                auto ref = entry.second.activityTarget.get();
                return entry.second.restRecovery.walking && ref && ref->GetFormID() == id;
            });
        };
        const bool settledSocial =
            SettledRest(ContextFor(actor)) && Settings::GetSingleton().Get().sandbox.social;
        if (auto* lists = RE::ProcessLists::GetSingleton()) {
            Engine::ForEachHighActor(*lists, [&](RE::Actor& other) {
                if (SocialSpace(other) != SocialSpace(actor) || !other.Is3DLoaded()) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                if (auto furniture = other.GetOccupiedFurniture().get()) {
                    occupied.insert(furniture->GetFormID());
                }
                if (other.GetFormID() != actor.GetFormID()) {
                    people.push_back(other.GetPosition());
                }
                if (!settledSocial || other.IsPlayerRef() || managed.contains(other.GetFormID()) ||
                    other.IsDead() || other.IsInCombat() || other.GetCurrentScene() || IsSpeaking(other) ||
                    other.IsHostileToActor(&actor) || claimed(other.GetFormID())) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                auto* npc = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13794);
                if (!npc || !other.GetRace() || !other.GetRace()->HasKeyword(npc)) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                RE::NiPoint3 velocity{};
                other.GetLinearVelocity(velocity);
                const auto pos = other.GetPosition();
                const float distance = std::hypot(Distance2D(origin, pos), pos.z - origin.z);
                if (Length({velocity.x, velocity.y}) < 12 && distance > 280 &&
                    Distance2D(center, pos) < f.sandboxRadius - 180 && std::abs(pos.z - center.z) <= height) {
                    choices.push_back({other.GetHandle(), distance + 250, true});
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }
        std::unordered_set<RE::TESObjectCELL*> cells{cell};
        if (!cell->IsInteriorCell()) {
            if (auto* world = RE::TES::GetSingleton()) {
                for (float x : {-f.sandboxRadius, 0.0F, f.sandboxRadius}) {
                    for (float y : {-f.sandboxRadius, 0.0F, f.sandboxRadius}) {
                        auto* neighbor = world->GetCell({center.x + x, center.y + y, center.z});
                        if (neighbor && neighbor->IsAttached()) {
                            cells.insert(neighbor);
                        }
                    }
                }
            }
        }
        for (auto* searchCell : cells) {
            Engine::ForEachReferenceInRange(
                *searchCell, center, std::hypot(f.sandboxRadius, height), [&](RE::TESObjectREFR& ref) {
                    if (ref.IsDisabled() || ref.IsDeleted() || ref.GetIgnoredBySandbox() ||
                        occupied.contains(ref.GetFormID()) || claimed(ref.GetFormID())) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    auto* base = ref.GetBaseObject();
                    if (!base) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    auto* furniture = base->As<RE::TESFurniture>();
                    if (!furniture && base->GetFormType() != RE::FormType::IdleMarker) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }

                    if (furniture &&
                        (furniture->furnFlags.any(RE::TESFurniture::ActiveMarker::kCanSleep,
                                                  RE::TESFurniture::ActiveMarker::kDisablesActivation,
                                                  RE::TESFurniture::ActiveMarker::kIsPerch) ||
                         furniture->workBenchData.benchType !=
                             RE::TESFurniture::WorkBenchData::BenchType::kNone)) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    const auto pos = ref.GetPosition();
                    const float distance = std::hypot(Distance2D(origin, pos), pos.z - origin.z);
                    if (distance < 160 || std::abs(pos.z - center.z) > height ||
                        Distance2D(center, pos) > f.sandboxRadius - 40) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    const float variation = static_cast<float>(
                        RestPose::Mix(ref.GetFormID() + actor.GetFormID() + f.activitySearch.sequence) % 160);
                    choices.push_back({ref.GetHandle(), distance + variation, false});
                    return RE::BSContainer::ForEachResult::kContinue;
                });
        }
        std::sort(choices.begin(), choices.end(),
                  [](const auto& a, const auto& b) { return a.score < b.score; });
        const auto area = choices.empty() ? RestReachability{}
                                          : navigation.RestArea(origin, center, f.sandboxRadius, height);
        for (std::size_t i = 0; i < std::min<std::size_t>(choices.size(), 8); ++i) {
            auto ref = choices[i].handle.get();
            if (!ref) {
                continue;
            }
            const auto pos = ref->GetPosition();
            const float angle = std::atan2(origin.y - pos.y, origin.x - pos.x);
            const float standOff = choices[i].patron ? 175.0F : 95.0F;
            for (float turn : {0.0F, .7F, -.7F}) {
                const RE::NiPoint3 desired{pos.x + std::cos(angle + turn) * standOff,
                                           pos.y + std::sin(angle + turn) * standOff, pos.z};
                const auto route = navigation.FindRestDestination(area, desired);
                if (!route) {
                    continue;
                }
                const RE::NiPoint3 point{route->point.x, route->point.y, route->point.z};
                if (std::hypot(Distance2D(origin, point), point.z - origin.z) < 120 ||
                    Distance2D(point, pos) > standOff + 45 ||
                    Distance2D(center, point) > f.sandboxRadius - 30 ||
                    std::abs(point.z - center.z) > height) {
                    continue;
                }
                float water{};
                auto* targetCell = ref->GetParentCell();
                if (!targetCell || (targetCell->GetWaterHeight(point, water) && water >= point.z - 2)) {
                    continue;
                }
                if (std::any_of(people.begin(), people.end(), [&](auto person) {
                        return std::abs(person.z - point.z) < 120 && Distance2D(person, point) < 100;
                    })) {
                    continue;
                }
                f.activityTarget = ref->GetHandle();
                f.activityWalkTime = RestWalkBudget(route->routeLength);
                logger::info("[RestActivity] {} approaching {} {:08X}; distance={:.0f} range={:.0f} "
                             "heightDelta={:.0f} route={:.0f} timeout={:.0f}",
                             actor.GetName(), choices[i].patron ? "patron" : "marker", ref->GetFormID(),
                             Distance2D(origin, point), f.sandboxRadius, point.z - origin.z,
                             route->routeLength, f.activityWalkTime);
                return point;
            }
        }
        logger::info("[RestActivity] {} no reachable unoccupied activity destination; candidates={} "
                     "reachable={} limited={}",
                     actor.GetName(), choices.size(), area.distance.size(), area.limited);
        return std::nullopt;
    }

    void FormationController::UpdateRestRecovery(ManagedFollower& f, RE::Actor& actor,
                                                 RE::TESObjectREFR& anchor, float dt)
    {
        auto& recovery = f.restRecovery;
        auto* state = actor.AsActorState();
        RE::NiPoint3 motion{};
        actor.GetLinearVelocity(motion);
        bool animationDriven = false;
        actor.GetGraphVariableBool("bAnimationDriven", animationDriven);
        const bool busy = !state || state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal ||
                          actor.GetOccupiedFurniture().get() || actor.GetCurrentScene() ||
                          actor.IsInCombat() || f.restPose.kind || lookout.actor == actor.GetFormID() ||
                          social.Listener(actor.GetFormID()) || f.patronTimer > 0 || animationDriven ||
                          IsSpeaking(actor);
        const auto& settings = Settings::GetSingleton().Get();
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* native = data->LookupForm<RE::TESPackage>(SandboxPackageLocal(f.slot), settings.pluginName);
        auto* stroll = data->LookupForm<RE::TESPackage>(0x870 + f.slot, settings.pluginName);
        if (recovery.walking) {
            auto target = gatherPool[f.slot].get();
            const auto actorPos = actor.GetPosition();
            const auto targetPos = target ? target->GetPosition() : RE::NiPoint3{};
            const bool arrived = target && RestArrived({actorPos.x, actorPos.y, actorPos.z},
                                                       {targetPos.x, targetPos.y, targetPos.z});
            if (recovery.Done(dt, arrived,
                              busy || !target ||
                                  (!f.activityTarget.get() && settings.sandbox.idleRecoveryAfter <= 0))) {

                if (arrived && !busy && f.activityTarget.get()) {
                    auto position = anchor.GetPosition();
                    if (std::abs(position.z - actorPos.z) >= 64) {
                        position.z = actorPos.z;
                        anchor.SetPosition(position);
                    }
                }
                logger::info("[RestRecovery] {} returning to native sandbox; elapsed={:.1f}", actor.GetName(),
                             recovery.elapsed);
                recovery.Finish();
                f.activityTarget = {};
            }
            return;
        }
        const bool standing =
            !busy && actor.GetCurrentPackage() == native && Length({motion.x, motion.y}) < 12;
        const bool activityReady = f.activitySearch.Ready(dt, standing, actor.GetFormID());
        const bool recoveryReady =
            recovery.Ready(dt, standing, sandbox.expanded,
                           settings.sandbox.idleRecoveryAfter * RecoveryPreference(PersonalityFor(actor)),
                           actor.GetFormID());
        if (!activityReady && !recoveryReady) {
            return;
        }
        for (const auto& [id, other] : managed) {
            if (id != actor.GetFormID() && other.restRecovery.walking) {
                return;
            }
        }
        if (!stroll) {
            recovery.Finish();
            return;
        }
        if (activityReady && activityDispatchCooldown <= 0) {
            f.activitySearch.Attempt(actor.GetFormID());
            activityDispatchCooldown = 2.5F;
            if (auto point = FindRestActivity(f, actor, anchor)) {
                if (PrepareAuxMarker(f, actor, *point, true)) {
                    stroll->packData.packFlags.set(RE::PACKAGE_DATA::GeneralFlag::kPreferredSpeed);
                    stroll->packData.maxSpeed = RE::PACKAGE_DATA::PreferredSpeed::kWalk;
                    recovery.Begin(f.activityWalkTime);
                    return;
                }
                f.activityTarget = {};
            }

            actor.EvaluatePackage(false, true);
        }
        if (!recoveryReady) {
            return;
        }
        const auto origin = actor.GetPosition(), center = anchor.GetPosition();
        const float radius = std::min(360.0F, f.sandboxRadius * .65F);
        for (int attempt = 0; attempt < 8; ++attempt) {
            const auto roll = RestPose::Mix(actor.GetFormID() + recovery.sequence * 7919 + attempt * 3571);
            const float angle = static_cast<float>(roll % 6283) * .001F;
            const Vec2 desired{center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
            const auto direction = Normalize({desired.x - origin.x, desired.y - origin.y});
            const auto point = navigation.Resolve(origin, desired, direction);
            if (!point || Distance2D(origin, *point) < 140 ||
                Distance2D(center, *point) > f.sandboxRadius - 35 ||
                (f.restAnchorZSet && std::abs(point->z - f.restAnchorZ) > settings.sandbox.activityHeight)) {
                continue;
            }
            float water{};
            if (actor.GetParentCell()->GetWaterHeight(*point, water) && water >= point->z - 2) {
                continue;
            }
            bool clear = true;
            for (const auto& [id, other] : managed) {
                if (id != actor.GetFormID()) {
                    if (auto companion = other.handle.get();
                        companion && Distance2D(companion->GetPosition(), *point) < 120) {
                        clear = false;
                        break;
                    }
                }
            }
            if (!clear) {
                continue;
            }
            auto* marker = PrepareAuxMarker(f, actor, *point, true);
            if (!marker) {
                break;
            }
            stroll->packData.packFlags.set(RE::PACKAGE_DATA::GeneralFlag::kPreferredSpeed);
            stroll->packData.maxSpeed = RE::PACKAGE_DATA::PreferredSpeed::kWalk;
            recovery.Begin();
            logger::info("[RestRecovery] {} taking a short walk; distance={:.0f} anchorDistance={:.0f}",
                         actor.GetName(), Distance2D(origin, *point), Distance2D(center, *point));
            return;
        }
        recovery.Finish();
    }

    void FormationController::ApplySandbox(ManagedFollower& f, RE::Actor& actor, RE::PlayerCharacter& player,
                                           float dt)
    {
        f.individual.ResetMotion();
        static_cast<void>(player);
        f.recoveryReleasePending = false;
        const auto& settings = Settings::GetSingleton().Get();
        auto* quest = EnsureQuest();
        auto* package = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
            SandboxPackageLocal(f.slot), settings.pluginName);
        if (!quest || !quest->IsRunning() || !package) {
            ReleaseTravel(f, &actor);
            return;
        }
        auto* custom = skyrim_cast<RE::TESCustomPackageData*>(package->data);
        RE::PackageLocation* location = nullptr;
        if (custom && custom->data.data && custom->data.uids) {
            for (std::uint16_t i = 0; i < custom->data.dataSize; ++i) {
                if (custom->data.uids[i] != 0) {
                    continue;
                }
                auto* input = skyrim_cast<RE::BGSPackageDataLocation*>(custom->data.data[i]);
                if (input) {
                    location = input->pointer;
                }
                break;
            }
        }
        if (!location || location->locType != RE::PackageLocation::Type::kAlias_Reference) {
            LogDiagnostic(actor.GetFormID(), actor.GetName(),
                          "Sandbox location input unavailable; verify matching ESP");
            ReleaseTravel(f, &actor);
            return;
        }
        auto* marker = EnsureMarker(f, actor);
        if (!marker) {
            ReleaseTravel(f, &actor);
            return;
        }
        if (settings.conversationAwareness) {
            PrepareAuxMarker(f, actor, actor.GetPosition());
        }
        const bool entering = !f.sandboxActive;

        const bool moved = !f.anchorSet;

        if (moved) {
            marker->MoveTo(&actor);
            marker->SetPosition(actor.GetPosition());
            f.anchorSet = true;
        }
        if (!f.restAnchorZSet) {
            f.restAnchorZ = marker->GetPosition().z;
            f.restAnchorZSet = true;
        }
        const auto context = ContextFor(actor);
        const float activityRadius = ActivityRadius(context, sandbox.radius, settings.sandbox);
        if (entering || f.sandboxRadius != activityRadius) {
            logger::info("[Sandbox] {} activity radius {:.0f} -> {:.0f}; context={}; phase={}; current "
                         "action retained",
                         actor.GetName(), f.sandboxRadius, activityRadius, static_cast<int>(context),
                         sandbox.expanded ? "free roam" : "nearby");
        }

        location->rad = static_cast<std::uint32_t>(activityRadius);
        f.distantCatchup = false;
        package->packData.packFlags.set(RE::PACKAGE_DATA::GeneralFlag::kPreferredSpeed);
        package->packData.maxSpeed = RE::PACKAGE_DATA::PreferredSpeed::kWalk;
        f.sandboxActive = f.travelActive = true;

        f.sandboxRadius = activityRadius;
        const int meals = MealsAllowed(ContextFor(actor), settings.sandbox) ? 1 : 0;
        if (f.mealsState != meals) {
            auto* source = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                meals ? SocialPackageLocal(f.slot) : PosePackageLocal(f.slot), settings.pluginName);
            auto* destination = BoolInput(package, 1);
            auto* value = BoolInput(source, meals ? 4 : 1);
            if (destination && value) {
                destination->Assign(value);
                f.mealsState = meals;
            }
        }
        const int previousPose = f.restPose.kind;
        const bool previouslyWalking = f.restRecovery.walking;
        UpdateRestRecovery(f, actor, *marker, dt);
        UpdateRestPose(f, actor, dt);
        if (lookout.actor == actor.GetFormID()) {
            SetPoseFlag(f.slot, 1);
        }

        f.socialHold = social.Listener(actor.GetFormID()) != 0;
        f.socialPhase = f.restRecovery.walking ? 2 : f.socialHold ? 1 : 0;
        SetSocialFlag(f.slot, f.socialPhase);
        auto* activePackage = package;
        if (f.restRecovery.walking) {
            activePackage = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                0x870 + f.slot, settings.pluginName);
        }
        else if (f.socialHold) {
            activePackage = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                SocialPackageLocal(f.slot), settings.pluginName);
        }
        if (f.restPose.kind) {
            activePackage = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                f.restPose.kind == 2 ? SeatPackageLocal(f.slot) : PosePackageLocal(f.slot),
                settings.pluginName);
        }
        else if (lookout.actor == actor.GetFormID()) {
            activePackage = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                PosePackageLocal(f.slot), settings.pluginName);
        }
        f.sandboxRefresh += dt;
        if (entering) {
            f.goal.Release();
        }
        const bool owns = actor.GetCurrentPackage() == activePackage;
        auto* alias = GetRefAlias(quest, FollowerAlias(f.slot));
        bool animationDriven = false;
        actor.GetGraphVariableBool("bAnimationDriven", animationDriven);
        auto* actorState = actor.AsActorState();
        const bool nativeBusy =
            actor.GetOccupiedFurniture().get() || animationDriven ||
            (actorState && actorState->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal);
        const bool ownPoseTransition =
            previousPose != f.restPose.kind && previousPose != 0 &&
            actor.GetCurrentPackage() ==
                RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                    previousPose == 2 ? SeatPackageLocal(f.slot) : PosePackageLocal(f.slot),
                    settings.pluginName);
        if (alias && alias->GetReference() == &actor && !IsSpeaking(actor) && !actor.GetCurrentScene() &&
            (!nativeBusy || ownPoseTransition) &&
            (entering || previousPose != f.restPose.kind || previouslyWalking != f.restRecovery.walking ||
             moved || (!owns && f.sandboxRefresh >= 1.0F))) {
            actor.EvaluatePackage(false, true);
            f.sandboxRefresh = 0;
        }
        f.speedScale = SmoothSpeedScale(f.speedScale, 1.0F, dt);
        TravelSpeed::Set(static_cast<std::uint32_t>(f.slot), actor.GetFormID(), f.speedScale);
        if (settings.traceMovement && traceDue) {
            RE::NiPoint3 motion{};
            actor.GetLinearVelocity(motion);
            logger::info(
                "[Sandbox] {} radius={:.0f} ownPackage={} alias={} social={} speedScale={:.2f} phase={} "
                "restAge={:.1f} speed={:.0f} anchorDistance={:.0f} furniture={} scene={} pose={}",
                actor.GetName(), f.sandboxRadius, owns, alias && alias->GetReference() == &actor,
                social.Listener(actor.GetFormID()) != 0, f.speedScale,
                sandbox.expanded ? "free roam" : "nearby", sandbox.activeTime, Length({motion.x, motion.y}),
                Distance2D(actor.GetPosition(), marker->GetPosition()),
                bool(actor.GetOccupiedFurniture().get()), actor.GetCurrentScene() != nullptr,
                f.restPose.kind);
            if (!owns) {
                logger::info("[SandboxOwner] {} active={}", actor.GetName(),
                             DescribePackage(actor.GetCurrentPackage()));
            }
        }
    }

    RE::Actor* FormationController::GetSlotFollower(std::uint32_t slot)
    {
        std::lock_guard lock(stateMutex);
        if (!enabled || mode == FormationMode::kVanilla || loadReleaseTime > 0.0F) {
            return nullptr;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || (!IsRelaxing() && GlobalRejection(*player))) {
            return nullptr;
        }
        for (auto& [id, follower] : managed) {
            if (static_cast<std::uint32_t>(follower.slot) != slot || !follower.travelActive) {
                continue;
            }
            if (HandHolding::OwnsMovement(id)) {
                return nullptr;
            }
            if (!sandbox.active &&
                !HandHolding::TravelRequested(intent.CanTravel(), mode == FormationMode::kCompanion,
                                              Settings::GetSingleton().Get().handHolding, id,
                                              HandHolding::GetApproach().actor)) {
                return nullptr;
            }
            auto actor = follower.handle.get();
            if (!actor || !UsabilityRejection(*actor, *player).empty()) {
                return nullptr;
            }
            return actor.get();
        }
        return nullptr;
    }

    RE::TESObjectREFR* FormationController::GetSlotMarker(std::uint32_t slot)
    {
        std::lock_guard lock(stateMutex);
        if (!GetSlotFollower(slot)) {
            return nullptr;
        }
        return slot < markerPool.size() ? markerPool[slot].get().get() : nullptr;
    }

    RE::TESQuest* FormationController::EnsureQuest()
    {
        if (!questCache) {
            auto* data = RE::TESDataHandler::GetSingleton();
            if (data) {
                questCache = data->LookupForm<RE::TESQuest>(WAYFARER_QUEST_LOCAL_ID,
                                                            Settings::GetSingleton().Get().pluginName);
            }
        }
        if (!questCache) {
            if (!questWarned) {
                logger::error("[Quest] Wayfarer.esp is missing or disabled");
                questWarned = true;
            }
            return nullptr;
        }

        const auto& settings = Settings::GetSingleton().Get();
        const auto priority =
            (settings.enforceNFF || settings.enforceCustomFollowers || !manualRegistrations.empty() ||
             !dialogueRegistrations.empty() || !packageRegistrations.empty() || sandbox.active)
                ? 100
                : 96;
        if (questCache->data.priority != priority) {
            logger::info("[Quest] Runtime priority {} -> {}", static_cast<int>(questCache->data.priority),
                         priority);
            questCache->data.priority = static_cast<std::int8_t>(priority);
        }
        if (!questCache->IsRunning() && !questCache->IsStarting() && !questCache->IsStopping()) {
            questCache->Start();
        }
        return questCache;
    }

    void FormationController::WakeQuest()
    {
        auto* quest = EnsureQuest();
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
        if (!quest || !quest->IsRunning() || !policy) {
            return;
        }
        const auto handle = policy->GetHandleForObject(RE::FormType::Quest, quest);
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result;
        const bool dispatched =
            vm->DispatchMethodCall(handle, RE::BSFixedString("WayfarerQuestScript"),
                                   RE::BSFixedString("Wake"), RE::MakeFunctionArguments(), result);
        if (!dispatched && !questWarned) {
            questWarned = true;
            logger::error("[Quest] Script watchdog could not dispatch. Verify the matching ESP and "
                          "WayfarerQuestScript.pex are installed.");
        }
    }

    RE::BGSRefAlias* FormationController::GetRefAlias(RE::TESQuest* a_quest, std::uint32_t a_aliasID)
    {
        if (!a_quest) {
            return nullptr;
        }
        for (auto* alias : a_quest->aliases) {
            if (alias && alias->aliasID == a_aliasID) {
                return skyrim_cast<RE::BGSRefAlias*>(alias);
            }
        }
        return nullptr;
    }

    RE::TESObjectREFR* FormationController::EnsureMarker(ManagedFollower& a_follower, RE::Actor& a_player)
    {
        auto& pooled = markerPool[static_cast<std::size_t>(a_follower.slot)];
        if (auto existing = pooled.get()) {
            return existing.get();
        }
        if (auto* alias = GetRefAlias(EnsureQuest(), MarkerAlias(a_follower.slot))) {
            if (auto* saved = alias->GetReference(); saved && saved->GetBaseObject() &&
                                                     saved->GetBaseObject()->GetFormID() == 0x3B &&
                                                     !saved->IsDeleted()) {
                pooled = saved->GetHandle();
                return saved;
            }
        }

        auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(0x3B);
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!base || !dataHandler) {
            if (!a_follower.markerLogged) {
                a_follower.markerLogged = true;
                logger::error("[Marker] Base form {:08X} could not be resolved; check uMarkerBaseFormID.",
                              0x3B);
            }
            return nullptr;
        }

        pooled = dataHandler->CreateReferenceAtLocation(base, a_player.GetPosition(), RE::NiPoint3{},
                                                        a_player.GetParentCell(), a_player.GetWorldspace(),
                                                        nullptr, nullptr, RE::ObjectRefHandle{}, true, false);

        auto* created = pooled.get().get();
        if (created) {
            logger::info("[Marker] Created {:08X} for slot {}", created->GetFormID(), a_follower.slot);
        }
        else if (!a_follower.markerLogged) {
            a_follower.markerLogged = true;
            logger::error("[Marker] CreateReferenceAtLocation failed for slot {}.", a_follower.slot);
        }
        return created;
    }

    void FormationController::ReleaseTravel(ManagedFollower& f, RE::Actor* actor, bool tetherHandoff)
    {
        SetHandApproachRadius(f, false);
        f.avoidance.Reset();
        if (actor && !tetherHandoff) {
            HandHolding::ReleaseActor(actor->GetFormID());
        }
        f.individual.ResetMotion();
        EndRestPose(f, actor);
        f.haveRoleOffset = false;
        f.restRecovery.Reset();
        f.activitySearch.Reset();
        f.activityTarget = {};
        f.awarenessActive = f.awarenessWalking = f.awarenessTried = false;
        f.awarenessTime = 0;

        f.travelActive = false;
        f.socialHold = false;
        f.socialPhase = 0;
        f.patron = {};
        f.patronGesture = false;
        SetSocialFlag(f.slot, 0);
        f.sandboxActive = f.distantCatchup = false;
        f.sandboxRadius = f.sandboxRefresh = 0;
        if (!IsRelaxing()) {
            f.anchorSet = false;
            f.restAnchorZSet = false;
        }
        TravelSpeed::Clear(static_cast<std::uint32_t>(f.slot));
        f.speedScale = 1.0F;
        f.goal.Release();
    }

    void FormationController::ReleaseAll()
    {
        HandHolding::Reset();
        RestoreTravelBanter();
        lookout.Reset();
        conversationSpeaker = 0;
        activityDispatchCooldown = 0;
        social.Reset();
        SetSandboxFlag(false);
        for (int slot = 0; slot < PARTY_CAPACITY; ++slot) {
            SetSocialFlag(slot, false);
            SetPoseFlag(slot, 0);
        }
        for (auto& [formID, follower] : managed) {
            static_cast<void>(formID);
            follower.catchup.distantTime = 0;
            auto actorPointer = follower.handle.get();
            ReleaseTravel(follower, actorPointer.get());
        }
    }

    bool FormationController::ShouldSuspendForDialogue()
    {
        std::lock_guard lock(stateMutex);
        auto* ui = RE::UI::GetSingleton();
        const auto* topics = RE::MenuTopicManager::GetSingleton();
        const bool active = (ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) ||
                            (topics && Engine::IsDialogueTransition(*topics));
        return dialogueGuard.Check(active, GetTickCount64());
    }

    bool FormationController::SocialEligible(RE::Actor& actor) const
    {
        if (!sandbox.AllowsSocial(Settings::GetSingleton().Get().sandbox) || !actor.Is3DLoaded() ||
            actor.IsInCombat() || actor.IsDead() || actor.IsSneaking() || actor.IsOnMount() ||
            actor.GetCurrentScene()) {
            return false;
        }
        auto* state = actor.AsActorState();
        if (!state || state->IsWeaponDrawn() || state->IsSwimming() ||
            state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal || actor.GetOccupiedFurniture().get()) {
            return false;
        }
        auto it = managed.find(actor.GetFormID());
        if (it == managed.end() || !it->second.sandboxActive || it->second.restPose.kind ||
            it->second.restRecovery.walking || lookout.actor == actor.GetFormID()) {
            return false;
        }
        auto* package = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
            SandboxPackageLocal(it->second.slot), Settings::GetSingleton().Get().pluginName);
        auto* conversation = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
            SocialPackageLocal(it->second.slot), Settings::GetSingleton().Get().pluginName);
        if (actor.GetCurrentPackage() != package &&
            !(social.Listener(actor.GetFormID()) && actor.GetCurrentPackage() == conversation)) {
            return false;
        }
        if (!social.Listener(actor.GetFormID()) && IsSpeaking(actor)) {
            return false;
        }
        auto* race = actor.GetRace();
        auto* npc = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13794);
        if (!race || !npc || !race->HasKeyword(npc)) {
            return false;
        }
        RE::NiPoint3 velocity{};
        actor.GetLinearVelocity(velocity);
        return Length({velocity.x, velocity.y}) < 140;
    }

    RE::TESIdleForm* FormationController::ConsumeRestIdle(std::uint32_t slot)
    {
        std::lock_guard lock(stateMutex);
        if (ShouldSuspendForDialogue()) {
            return nullptr;
        }
        auto* actor = GetSlotFollower(slot);
        if (!actor) {
            return nullptr;
        }
        auto& f = managed.at(actor->GetFormID());
        auto* package = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
            PosePackageLocal(slot), Settings::GetSingleton().Get().pluginName);
        if (f.restPose.kind != 1 || f.restPose.started || f.restIdleRequested ||
            actor->GetCurrentPackage() != package) {
            return nullptr;
        }
        f.restIdleRequested = true;
        return RE::TESForm::LookupByID<RE::TESIdleForm>(RestIdleID(f.activity));
    }
    void FormationController::ReportRestIdle(std::uint32_t slot, RE::Actor* actor, RE::TESIdleForm* idle,
                                             bool accepted)
    {
        std::lock_guard lock(stateMutex);
        if (ShouldSuspendForDialogue() || !actor || !idle) {
            return;
        }
        auto it = managed.find(actor->GetFormID());
        if (it == managed.end()) {
            return;
        }
        auto& f = it->second;
        if (f.slot != static_cast<int>(slot) || f.restPose.kind != 1 || !f.restIdleRequested ||
            RestIdleID(f.activity) != idle->GetFormID()) {
            return;
        }
        auto* package = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
            PosePackageLocal(slot), Settings::GetSingleton().Get().pluginName);
        if (actor->GetCurrentPackage() != package || actor->IsInCombat() || actor->GetCurrentScene()) {
            return;
        }
        logger::info("[RestPose] {} activity={} idle={:08X} accepted={} duration={:.0f}s", actor->GetName(),
                     static_cast<int>(f.activity), idle->GetFormID(), accepted, f.restPose.duration);
        if (accepted) {
            f.restPose.started = true;
            f.restPose.elapsed = 0;
        }
        else {
            EndRestPose(f, actor);
        }
    }
    RE::TESObjectREFR* FormationController::GetGatherMarker(std::uint32_t slot)
    {
        std::lock_guard lock(stateMutex);
        if (!enabled || slot >= PARTY_CAPACITY) {
            return nullptr;
        }
        for (const auto& [id, f] : managed) {
            if (f.slot == static_cast<int>(slot) && f.travelActive &&
                (Settings::GetSingleton().Get().conversationAwareness || f.restRecovery.walking ||
                 f.awarenessActive)) {
                return gatherPool[slot].get().get();
            }
        }
        return nullptr;
    }
    void FormationController::UpdatePatron(ManagedFollower& f, RE::Actor& actor, float dt)
    {
        f.patronCooldown -= dt;
        const bool eligible =
            SocialEligible(actor) && !social.Listener(actor.GetFormID()) && SettledRest(ContextFor(actor));
        if (!eligible) {
            f.patron = {};
            f.patronTimer = 0;
            f.patronGesture = false;
            return;
        }
        if (f.patronTimer > 0) {
            f.patronTimer -= dt;
            if (f.patronTimer <= 0) {
                f.patron = {};
                f.patronGesture = false;
            }
            return;
        }
        if (f.patronCooldown > 0) {
            return;
        }
        f.patronCooldown = 25.0F + RestPose::Mix(actor.GetFormID() + social.opportunities) % 21;
        RE::NiPoint3 velocity{};
        actor.GetLinearVelocity(velocity);
        if (Length({velocity.x, velocity.y}) > 20) {
            return;
        }
        auto* lists = RE::ProcessLists::GetSingleton();
        if (!lists) {
            return;
        }
        auto* npc = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13794);
        float nearest = 240;
        Engine::ForEachHighActor(*lists, [&](RE::Actor& other) {
            if (other.IsPlayerRef() || managed.contains(other.GetFormID()) || other.IsDead() ||
                other.IsInCombat() || other.GetCurrentScene() || IsSpeaking(other) ||
                SocialSpace(actor) != SocialSpace(other) || other.IsHostileToActor(&actor) ||
                !other.GetRace() || !npc || !other.GetRace()->HasKeyword(npc)) {
                return RE::BSContainer::ForEachResult::kContinue;
            }
            RE::NiPoint3 motion{};
            other.GetLinearVelocity(motion);
            const float distance = Distance2D(actor.GetPosition(), other.GetPosition());
            bool sightDetail = false;
            if (distance < nearest && Length({motion.x, motion.y}) < 20 &&
                actor.HasLineOfSight(&other, sightDetail)) {
                nearest = distance;
                f.patron = other.GetHandle();
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        if (f.patron.get()) {
            f.patronTimer = 6;
            f.patronGesture = true;
            logger::info("[Social] {} greets a nearby NPC", actor.GetName());
        }
    }

    void FormationController::UpdateSocial(float dt)
    {
        if (!sandbox.AllowsSocial(Settings::GetSingleton().Get().sandbox)) {
            social.Reset();
            for (auto& [id, f] : managed) {
                f.patron = {};
                f.patronTimer = 0;
                f.patronGesture = false;
            }
            return;
        }
        std::vector<SocialCandidate> candidates;
        for (auto& [id, f] : managed) {
            if (auto actor = f.handle.get(); actor && SocialEligible(*actor)) {
                const auto p = actor->GetPosition();
                RE::NiPoint3 velocity{};
                actor->GetLinearVelocity(velocity);
                candidates.push_back({id,
                                      {p.x, p.y},
                                      Length({velocity.x, velocity.y}) < 20,
                                      SocialSpace(*actor),
                                      SocialWeight(PersonalityFor(*actor))});
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](auto& a, auto& b) { return a.id < b.id; });
        const int before = social.count;
        social.Update(dt, candidates, [](RE::FormID a, RE::FormID b) {
            auto* first = RE::TESForm::LookupByID<RE::Actor>(a);
            auto* second = RE::TESForm::LookupByID<RE::Actor>(b);
            bool detail = false;
            return first && second && first->HasLineOfSight(second, detail);
        });
        if (!before && social.count) {
            auto* first = RE::TESForm::LookupByID<RE::Actor>(social.members[0]);
            auto* second = RE::TESForm::LookupByID<RE::Actor>(social.members[1]);
            if (first && second) {
                logger::info("[Social] conversation: {} and {}; brief pair focus", first->GetName(),
                             second->GetName());
            }
        }
        for (auto& [id, f] : managed) {
            if (auto actor = f.handle.get()) {
                UpdatePatron(f, *actor, dt);
            }
        }
        if (Settings::GetSingleton().Get().traceMovement && traceDue && !social.count) {
            logger::info(
                "[Social] eligible={} settled={} cooldown={:.1f}", candidates.size(),
                std::count_if(candidates.begin(), candidates.end(), [](auto& c) { return c.settled; }),
                social.cooldown);
        }
    }

    RE::Actor* FormationController::GetSocialTarget(std::uint32_t slot)
    {
        std::lock_guard lock(stateMutex);
        if (ShouldSuspendForDialogue()) {
            social.ClearGestures();
            return nullptr;
        }
        auto* actor = GetSlotFollower(slot);
        if (!actor || !SocialEligible(*actor)) {
            return nullptr;
        }
        const auto found = managed.find(actor->GetFormID());
        if (found != managed.end() && found->second.patronTimer > 0 && !social.Listener(actor->GetFormID())) {
            auto patron = found->second.patron.get();
            if (patron && !patron->IsDead() && !patron->IsInCombat() && !patron->GetCurrentScene() &&
                SocialSpace(*actor) == SocialSpace(*patron) &&
                Distance2D(actor->GetPosition(), patron->GetPosition()) <= 260) {
                RE::NiPoint3 motion{};
                actor->GetLinearVelocity(motion);
                if (Length({motion.x, motion.y}) < 20) {
                    return patron.get();
                }
            }
        }
        auto* package = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
            SandboxPackageLocal(slot), Settings::GetSingleton().Get().pluginName);
        auto* conversation = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
            SocialPackageLocal(slot), Settings::GetSingleton().Get().pluginName);
        if (found == managed.end() ||
            (actor->GetCurrentPackage() != package && actor->GetCurrentPackage() != conversation)) {
            return nullptr;
        }
        RE::NiPoint3 velocity{};
        actor->GetLinearVelocity(velocity);
        if (Length({velocity.x, velocity.y}) > 20) {
            return nullptr;
        }
        auto* target = RE::TESForm::LookupByID<RE::Actor>(social.Listener(actor->GetFormID()));
        return target && SocialEligible(*target) && SocialSpace(*actor) == SocialSpace(*target) &&
                       Distance2D(actor->GetPosition(), target->GetPosition()) <= SocialPlanner::leaveDistance
                   ? target
                   : nullptr;
    }

    RE::TESIdleForm* FormationController::ConsumeSocialIdle(std::uint32_t slot)
    {
        std::lock_guard lock(stateMutex);
        auto* actor = GetSlotFollower(slot);
        if (!actor || !GetSocialTarget(slot)) {
            return nullptr;
        }
        auto& follower = managed.at(actor->GetFormID());
        if (follower.patronTimer > 0 && follower.patronGesture) {
            follower.patronGesture = false;
        }
        else if (social.TakeGesture(actor->GetFormID())) {
        }
        else {
            return nullptr;
        }

        return RE::TESForm::LookupByID<RE::TESIdleForm>((social.turn + social.sequence) % 3 == 0 ? 0x58B46
                                                                                                 : 0x58B45);
    }

    void FormationController::ReportSocialIdle(RE::Actor* actor, bool accepted)
    {
        if (actor) {
            logger::info("[Social] gesture {} ({:08X}) accepted={}", actor->GetName(), actor->GetFormID(),
                         accepted);
        }
    }

    void FormationController::RemoveManaged(RE::FormID a_formID)
    {
        const auto found = managed.find(a_formID);
        if (found == managed.end()) {
            return;
        }
        auto actorPointer = found->second.handle.get();
        ReleaseTravel(found->second, actorPointer.get());
        managed.erase(found);
        diagnosticReasons.erase(a_formID);
        logger::info("[Controller] Released {:08X}", a_formID);
    }

    void FormationController::LogDiagnostic(RE::FormID a_formID, const char* a_name, const char* a_reason)
    {
        if (!Settings::GetSingleton().Get().logDiagnostics) {
            return;
        }
        std::string reason = a_reason ? a_reason : "eligible";
        const auto found = diagnosticReasons.find(a_formID);
        if (found != diagnosticReasons.end() && found->second == reason) {
            return;
        }
        logger::info("[Diag] {:08X} ({}): {}", a_formID, a_name && *a_name ? a_name : "unnamed", reason);
        diagnosticReasons[a_formID] = std::move(reason);
    }

    void FormationController::LogGlobalDiagnostic(const char* a_reason)
    {
        if (!Settings::GetSingleton().Get().logDiagnostics) {
            return;
        }
        std::string reason = a_reason ? a_reason : "travelling";
        if (lastGlobalReason == reason) {
            return;
        }
        logger::info("[Diag] global: {}", reason);
        lastGlobalReason = std::move(reason);
    }

    void FormationController::LogScanSummary(std::size_t a_high, std::size_t a_teammates,
                                             std::size_t a_inFaction)
    {
        if (!Settings::GetSingleton().Get().logDiagnostics) {
            return;
        }
        auto summary = fmt::format("high actors={} teammates={} in CurrentFollowerFaction={} managed={}",
                                   a_high, a_teammates, a_inFaction, managed.size());
        if (lastScanSummary == summary) {
            return;
        }
        logger::info("[Diag] scan: {}", summary);
        lastScanSummary = std::move(summary);
    }

    bool FormationController::IsNFFFollower(RE::Actor& actor)
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data || !data->LookupModByName("nwsFollowerFramework.esp")) {
            return false;
        }
        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(0x750BA);
        if (!quest || !quest->IsRunning()) {
            return false;
        }

        for (std::uint32_t slot = 0; slot < 12; ++slot) {
            auto* alias = GetRefAlias(quest, slot);
            if (alias && alias->GetReference() == &actor) {
                return true;
            }
        }
        return false;
    }

    void FormationController::GiveOrder(PartyOrder value)
    {
        std::lock_guard lock(stateMutex);
        if (!enabled) {
            Notify("Enable Walk With Me before issuing party orders");
            return;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->Is3DLoaded() || !player->GetParentCell()) {
            return;
        }
        order = value;
        sandbox.Reset();
        for (auto& [id, f] : managed) {
            f.anchorSet = false;
            f.restAnchorZSet = false;
        }
        SetMode(value == PartyOrder::kRoam    ? FormationMode::kSandbox
                : value == PartyOrder::kScout ? FormationMode::kLead
                : value == PartyOrder::kRear  ? FormationMode::kRear
                                              : Settings::GetSingleton().Get().mode,
                false);
        order = value;

        if (!intent.IsMoving()) {
            const auto pos = player->GetPosition();
            intent.BeginOrder({pos.x, pos.y}, ForwardFromYaw(player->GetAngle().z));
        }
        scanTimer = Settings::GetSingleton().Get().scanInterval;
    }

    bool FormationController::ApplyPreferences(const SettingsData& value)
    {
        std::lock_guard lock(stateMutex);
        const auto previous = Settings::GetSingleton().Get();
        if (!Settings::GetSingleton().Save(value)) {
            return false;
        }
        const auto& settings = Settings::GetSingleton().Get();
        if (!settings.walkingBanter) {
            RestoreTravelBanter();
        }

        if (settings.enabled != enabled) {
            SetEnabled(settings.enabled, false);
        }
        if (settings.mode != previous.mode) {
            SetMode(settings.mode, false);
        }
        if (settings.maxFollowers != previous.maxFollowers) {
            std::vector<RE::FormID> excess;
            for (const auto& [id, follower] : managed) {
                if (follower.slot >= settings.maxFollowers) {
                    excess.push_back(id);
                }
            }
            for (auto id : excess) {
                RemoveManaged(id);
            }
        }
        if (settings.maxFollowers != previous.maxFollowers || settings.enforceNFF != previous.enforceNFF ||
            settings.enforceCustomFollowers != previous.enforceCustomFollowers ||
            settings.autoDiscover != previous.autoDiscover ||
            settings.disableIndoors != previous.disableIndoors) {
            scanTimer = settings.scanInterval;
            diagnosticReasons.clear();
        }
        if (settings.travel.spacing != previous.travel.spacing ||
            settings.travel.naturalStragglerDistance != previous.travel.naturalStragglerDistance ||
            settings.preferredSide != previous.preferredSide) {
            roleTimer = 1.0F;
            roleCooldown = 0;
        }
        return true;
    }

    std::vector<CommandGesture::FollowerBearing> FormationController::GestureFollowerBearings(
        RE::PlayerCharacter& player)
    {
        std::lock_guard lock(stateMutex);
        std::vector<CommandGesture::FollowerBearing> result;
        if (!enabled || mode == FormationMode::kVanilla) {
            return result;
        }
        const auto forward = ForwardFromYaw(player.GetAngleZ());
        const auto right = RightFromForward(forward);
        const auto origin = player.GetPosition();
        for (const auto& [id, follower] : managed) {
            auto actor = follower.handle.get();
            if (!actor || !actor->Is3DLoaded() || actor->IsDead() || actor->IsDisabled() ||
                actor->IsInCombat() || explicitExclusions.contains(id) ||
                !InSameTravelSpace(*actor, player)) {
                continue;
            }
            const auto delta = actor->GetPosition() - origin;
            if (std::abs(delta.z) > 200) {
                continue;
            }
            result.push_back(
                {delta.x * right.x + delta.y * right.y, delta.x * forward.x + delta.y * forward.y});
        }
        return result;
    }

    PartyView FormationController::Snapshot()
    {
        std::lock_guard lock(stateMutex);
        PartyView view;
        view.settings = Settings::GetSingleton().Get();
        view.enabled = enabled;
        view.order = order;
        view.mode = mode;
        view.generation = generation;
        view.sandboxActive = sandbox.active;
        view.sandboxRadius = sandbox.radius;
        auto* data = RE::TESDataHandler::GetSingleton();
        view.nffInstalled = data && data->LookupModByName("nwsFollowerFramework.esp");
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        view.inGame = initialized && player && player->Is3DLoaded() && player->GetParentCell() && ui &&
                      !ui->IsMenuOpen(RE::MainMenu::MENU_NAME) && !ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
        if (!view.inGame) {
            return view;
        }
        view.recallCount = static_cast<int>(RecallCandidates().size());
        view.recallPending = recallRequest.pending;
        if (const auto* reason = RecallPlayerRejection(*player)) {
            view.recallReason = reason;
        }
        else if (!view.recallCount) {
            view.recallReason = "No travelling companions available";
        }
        view.gameplay = !ui->GameIsPaused();
        for (const auto* menuName :
             {"Dialogue Menu", "Console", "TweenMenu", "InventoryMenu", "MagicMenu", "ContainerMenu",
              "BarterMenu", "MapMenu", "Journal Menu", "Crafting Menu", "StatsMenu", "FavoritesMenu",
              "RaceSex Menu", "Book Menu", "Sleep/Wait Menu", "MessageBoxMenu"}) {
            if (ui->IsMenuOpen(menuName)) {
                view.gameplay = false;
            }
        }
        auto reason = GlobalRejection(*player);
        view.state = !enabled                          ? "Disabled"
                     : mode == FormationMode::kVanilla ? "Vanilla follower AI"
                     : reason                          ? reason
                     : sandbox.active                  ? "Sandboxing"
                     : intent.CanTravel()              ? "Travelling"
                                                       : "Ready - move to travel";
        if (auto* pick = RE::CrosshairPickData::GetSingleton()) {
#ifdef WAYFARER_MODERN_COMMONLIB
            auto target = pick->GetActiveTarget().get();
#else
            auto target = pick->target.get();
#endif
            if (auto* actor = target ? target->As<RE::Actor>() : nullptr) {
                view.crosshairActor = actor->GetFormID();
            }
        }
        auto addPackageTarget = [&](RE::FormID id, RE::Actor* actor) {
            PackageControlView row;
            row.id = id;
            row.name = actor ? actor->GetName() : fmt::format("Unavailable NPC ({:08X})", id);
            const auto registration = packageRegistrations.find(id);
            row.controlled = registration != packageRegistrations.end();
            row.manualReleaseRequired = row.controlled
                                            ? registration->second == 1
                                            : actor && !IsNFFFollower(*actor) && !actor->IsPlayerTeammate();
            const auto* why = actor ? PackageControlStartRejection(*actor) : "NPC is not loaded";
            row.canTake = !row.controlled && !why;
            const auto* pushWhy = actor ? CompanionPushRejection(*actor) : "NPC is not loaded";
            row.pushReason = pushWhy                                ? pushWhy
                             : companionPush.Busy(GetTickCount64()) ? "Wait a moment before another push"
                                                                    : "";
            row.canPush = row.pushReason.empty();
            if (row.controlled) {
                const auto status = actor ? UsabilityRejection(*actor, *player) : "NPC is not loaded";
                row.state = !enabled                          ? "Control paused: Walk With Me is disabled"
                            : mode == FormationMode::kVanilla ? "Control paused: Vanilla mode"
                            : !status.empty()                 ? "Control paused: " + status
                            : !managed.contains(id)           ? "Waiting for a free party slot"
                                                              : "Walk With Me controls travel and rest";
            }
            else {
                row.state = why ? why : "Ready for explicit travel control";
            }
            view.packageTargets.push_back(std::move(row));
        };
        for (const auto& [id, flags] : packageRegistrations) {
            static_cast<void>(flags);
            addPackageTarget(id, RE::TESForm::LookupByID<RE::Actor>(id));
        }
        // A focused popup keeps its target even if the crosshair moves. No nearby-NPC picker.
        for (const auto id : {view.crosshairActor, Menu::CompanionTarget()}) {
            if (id && std::none_of(view.packageTargets.begin(), view.packageTargets.end(),
                                   [id](const auto& row) { return row.id == id; })) {
                if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(id)) {
                    addPackageTarget(id, actor);
                }
            }
        }
        if (auto* lists = RE::ProcessLists::GetSingleton()) {
            Engine::ForEachHighActor(*lists, [&](RE::Actor& actor) {
                if (actor.IsPlayerRef() || actor.IsDead() || !InSameTravelSpace(actor, *player)) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                const bool nff = IsNFFFollower(actor);
                const auto id = actor.GetFormID();
                const bool eligible = IsDialogueFollower(actor);
                if (!eligible && !dialogueRegistrations.contains(id) && !manualRegistrations.contains(id) &&
                    !packageRegistrations.contains(id)) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                CompanionView row;
                row.id = id;
                row.name = actor.GetName();
                row.nff = nff;
                row.handHoldKey = HandHolding::ReferenceKey(actor);
                row.personalityKey = PersonalityKey(actor);
                row.personality = PersonalityName(PersonalityFor(actor));
                if (auto pref = view.settings.personalityOverrides.find(row.personalityKey);
                    pref != view.settings.personalityOverrides.end()) {
                    row.personalityOverride = pref->second;
                }
                if (lookout.actor == id) {
                    row.personality += " - keeping watch";
                }
                row.managed = managed.contains(id);
                row.excluded = explicitExclusions.contains(id);
                row.manuallyAdded = dialogueRegistrations.contains(id);
                row.apiRegistered = manualRegistrations.contains(id);
                row.packageControlled = packageRegistrations.contains(id);
                row.eligible = eligible;
                if (row.excluded || mode == FormationMode::kVanilla) {
                    row.state = "Left to follower AI";
                }
                else if (row.manuallyAdded && !eligible) {
                    row.state = "Added - not currently following";
                }
                else if (!row.manuallyAdded && !row.apiRegistered && !row.packageControlled &&
                         !view.settings.autoDiscover) {
                    row.state = "Detected - not added";
                }
                else if (auto why = CandidateRejection(actor); why && !IsRegisteredFollower(id)) {
                    row.state = why;
                }
                else if (!row.managed) {
                    row.state = "Waiting for a free party slot";
                }
                else if (auto unavailable = UsabilityRejection(actor, *player); !unavailable.empty()) {
                    row.state = unavailable;
                }
                else if (managed.at(id).travelActive) {
                    row.state = managed.at(id).sandboxActive ? "Sandboxing" : "Travelling with you";
                    ++view.activeCount;
                }
                else {
                    row.state = "Ready";
                }
                view.party.push_back(std::move(row));
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }
        std::sort(view.party.begin(), view.party.end(),
                  [](const auto& a, const auto& b) { return a.id < b.id; });
        std::sort(view.packageTargets.begin(), view.packageTargets.end(), [](const auto& a, const auto& b) {
            if (a.controlled != b.controlled) {
                return a.controlled;
            }
            return a.id < b.id;
        });
        return view;
    }

    void FormationController::RegisterSerialization()
    {
        auto* serial = SKSE::GetSerializationInterface();
        if (!serial) {
            return;
        }
        serial->SetUniqueID(0x57415946);
        serial->SetRevertCallback([](SKSE::SerializationInterface*) {
            CommandGesture::RestoreSelector(false);
            CommandGesture::HeadTracking::RestoreOwnership(0);
            auto& c = GetSingleton();
            std::lock_guard lock(c.stateMutex);
            c.ResetVictoryCelebration(false);
            c.explicitExclusions.clear();
            c.dialogueRegistrations.clear();
            c.packageRegistrations.clear();
            c.restoredRelax = false;
            c.restoredRelaxSlots.clear();
            c.restoredVanilla = false;
        });
        serial->SetSaveCallback([](SKSE::SerializationInterface* api) {
            const std::uint32_t gestureOwned = CommandGesture::SavedOwnership();
            const auto headOwned = CommandGesture::HeadTracking::SavedOwnership();
            if (headOwned && !api->WriteRecord(0x57484544, 1, &headOwned, sizeof(headOwned))) {
                logger::error("[Save] Could not write gesture head cleanup ownership");
            }
            if (gestureOwned && !api->WriteRecord(0x57475354, 3, &gestureOwned, sizeof(gestureOwned))) {
                logger::error("[Save] Could not write gesture cleanup ownership");
            }
            auto& c = GetSingleton();
            std::lock_guard lock(c.stateMutex);
            if (c.mode == FormationMode::kVanilla) {
                const std::uint32_t vanilla = 1;
                if (!api->WriteRecord(0x56414E4C, 1, &vanilla, sizeof(vanilla))) {
                    logger::error("[Save] Could not write Vanilla handoff state");
                }
            }
            if (c.IsRelaxing()) {
                std::vector<std::uint32_t> record{0};
                for (const auto& [id, f] : c.managed) {
                    if (f.anchorSet) {
                        ++record[0];
                        record.push_back(id);
                        record.push_back(f.slot);
                    }
                }
                if (!api->WriteRecord(0x52454C58, 1, record.data(),
                                      static_cast<std::uint32_t>(record.size() * sizeof(std::uint32_t)))) {
                    logger::error("[Save] Could not write Relax anchors");
                }
            }
            for (const auto id : c.dialogueRegistrations) {
                if (!api->WriteRecord(0x444C474D, 1, &id, sizeof(id))) {
                    logger::error("[Save] Could not write manual dialogue follower");
                }
            }
            for (const auto& [id, flags] : c.packageRegistrations) {
                const std::array<std::uint32_t, 2> record{id, flags};
                if (!api->WriteRecord(0x5043544C, 1, record.data(), sizeof(record))) {
                    logger::error("[Save] Could not write explicit package control");
                }
            }
            for (const auto id : c.explicitExclusions) {

                const std::array<std::uint32_t, 6> record{id,         0xFFFFFFFF, 0xFFFFFFFF,
                                                          0xFFFFFFFF, 0xFFFFFFFF, 1};
                if (!api->WriteRecord(0x5052464C, 1, record.data(), sizeof(record))) {
                    logger::error("[Save] Could not write companion exclusion");
                }
            }
        });
        serial->SetLoadCallback([](SKSE::SerializationInterface* api) {
            CommandGesture::RestoreSelector(false);
            CommandGesture::HeadTracking::RestoreOwnership(0);
            auto& c = GetSingleton();
            std::lock_guard lock(c.stateMutex);
            c.explicitExclusions.clear();
            c.dialogueRegistrations.clear();
            c.packageRegistrations.clear();
            c.restoredRelax = false;
            c.restoredRelaxSlots.clear();
            c.restoredVanilla = false;
            std::uint32_t type{}, version{}, length{};
            while (api->GetNextRecordInfo(type, version, length)) {
                if (type == 0x5043544C && version == 1 && length == 8) {
                    std::array<std::uint32_t, 2> record{};
                    RE::FormID resolved{};
                    if (api->ReadRecordData(record.data(), sizeof(record)) == sizeof(record) &&
                        ValidPackageControlFlags(record[1]) && api->ResolveFormID(record[0], resolved)) {
                        c.packageRegistrations[resolved] = record[1];
                    }
                    continue;
                }
                if (type == 0x444C474D && version == 1 && length == 4) {
                    RE::FormID saved{}, resolved{};
                    if (api->ReadRecordData(&saved, 4) == 4 && api->ResolveFormID(saved, resolved)) {
                        c.dialogueRegistrations.insert(resolved);
                    }
                    continue;
                }
                if (type == 0x56414E4C && version == 1 && length == 4) {
                    std::uint32_t saved{};
                    if (api->ReadRecordData(&saved, 4) == 4) {
                        c.restoredVanilla = saved == 1;
                    }
                    continue;
                }
                if (type == 0x57484544 && version == 1 && length == 4) {
                    std::uint32_t saved{};
                    if (api->ReadRecordData(&saved, 4) == 4) {
                        CommandGesture::HeadTracking::RestoreOwnership(saved);
                    }
                    continue;
                }
                if (type == 0x57475354 && (version >= 1 && version <= 3) && length == 4) {
                    std::uint32_t saved{};
                    if (api->ReadRecordData(&saved, 4) == 4) {
                        CommandGesture::RestoreSelector(version == 1 ? (saved == 1 ? 1U : 0U) : saved);
                    }
                    continue;
                }
                if (type == 0x52454C58 && version == 1 && length >= 4 && length <= PARTY_CAPACITY * 8 + 4 &&
                    (length - 4) % 8 == 0) {
                    std::vector<std::uint32_t> record(length / 4);
                    if (api->ReadRecordData(record.data(), length) != length ||
                        record[0] != (length - 4) / 8) {
                        continue;
                    }
                    c.restoredRelax = true;
                    std::set<std::uint32_t> slots;
                    for (std::size_t i = 1; i < record.size(); i += 2) {
                        RE::FormID id{};
                        if (record[i + 1] < PARTY_CAPACITY && slots.insert(record[i + 1]).second &&
                            api->ResolveFormID(record[i], id)) {
                            c.restoredRelaxSlots[id] = static_cast<int>(record[i + 1]);
                        }
                    }
                    continue;
                }
                if (type != 0x5052464C || version != 1 || length != 24) {
                    continue;
                }
                std::array<std::uint32_t, 6> record{};
                RE::FormID id{};
                if (api->ReadRecordData(record.data(), sizeof(record)) != sizeof(record) ||
                    !api->ResolveFormID(record[0], id)) {
                    continue;
                }
                if (record[5] == 1) {
                    c.explicitExclusions.insert(id);
                }
            }
            logger::info("[Save] Restored {} companion exclusions; legacy manual placements ignored",
                         c.explicitExclusions.size());
        });
    }

    void FormationController::Notify(std::string_view a_message)
    {
        Engine::Notify(std::string(a_message).c_str());
    }
} // namespace Wayfarer

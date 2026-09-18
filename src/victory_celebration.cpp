#include "RE/C/CombatGroup.h"
#include "RE/M/MiddleHighProcessData.h"
#include "RE/T/TESCombatEvent.h"
#include "RE/T/TESDeathEvent.h"
#include "formation_controller.h"
#include "victory_animation.h"

namespace
{
    struct BattleEvent
    {
        RE::ActorHandle actor, target;
        RE::FormID dying{};
    };

    class BattleEvents final : public RE::BSTEventSink<RE::TESCombatEvent>,
                               public RE::BSTEventSink<RE::TESDeathEvent>
    {
      public:
        static BattleEvents& Get()
        {
            static BattleEvents instance;
            return instance;
        }
        void Register()
        {
            if (registered) {
                return;
            }
            if (auto* source = RE::ScriptEventSourceHolder::GetSingleton()) {
                source->AddEventSink<RE::TESCombatEvent>(this);
                source->AddEventSink<RE::TESDeathEvent>(this);
                registered = true;
            }
        }
        RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent* event,
                                              RE::BSTEventSource<RE::TESCombatEvent>*) override
        {
            if (event && event->newState != RE::ACTOR_COMBAT_STATE::kNone) {
                auto* actor = event->actor ? event->actor->As<RE::Actor>() : nullptr;
                auto* target = event->targetActor ? event->targetActor->As<RE::Actor>() : nullptr;
                if (actor && target) {
                    Push({actor->GetHandle(), target->GetHandle(), 0});
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
        RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* event,
                                              RE::BSTEventSource<RE::TESDeathEvent>*) override
        {
            if (event && event->dead && event->actorDying) {
                Push({{}, {}, event->actorDying->GetFormID()});
            }
            return RE::BSEventNotifyControl::kContinue;
        }
        std::vector<BattleEvent> Drain(bool& lost)
        {
            std::lock_guard lock(mutex);
            lost = std::exchange(overflow, false);
            return std::exchange(events, {});
        }
        void Clear()
        {
            bool ignored;
            (void)Drain(ignored);
        }

      private:
        void Push(BattleEvent event)
        {
            std::lock_guard lock(mutex);
            if (events.size() < 512) {
                events.push_back(event);
            }
            else {
                overflow = true;
            }
        }
        std::mutex mutex;
        std::vector<BattleEvent> events;
        bool registered{}, overflow{};
    };

    bool SameSpace(RE::Actor& actor, RE::PlayerCharacter& player)
    {
        auto* a = actor.GetParentCell();
        auto* b = player.GetParentCell();
        return a && b &&
               (a == b || (!a->IsInteriorCell() && !b->IsInteriorCell() &&
                           actor.GetWorldspace() == player.GetWorldspace()));
    }

    bool Speaking(RE::Actor& actor)
    {
        RE::TESConditionItem condition;
        condition.data.functionData.function = RE::FUNCTION_DATA::FunctionID::kIsTalking;
        condition.data.comparisonValue.f = 1;
        condition.data.flags.opCode = RE::CONDITION_ITEM_DATA::OpCode::kEqualTo;
        condition.data.object = RE::CONDITIONITEMOBJECT::kSelf;
        RE::ConditionCheckParams params{&actor, &actor};
        return condition.IsTrue(params);
    }

    bool OwnsIdle(RE::Actor& actor, RE::TESIdleForm* idle)
    {
        auto* process = actor.GetActorRuntimeData().currentProcess;
        return idle && process && process->middleHigh && process->middleHigh->lastIdlePlayed == idle;
    }

    bool InBattle(RE::Actor& actor)
    {
        return actor.IsInCombat() ||
               actor.GetActorRuntimeData().boolBits.all(RE::Actor::BOOL_BITS::kSearchingInCombat);
    }

    bool PlayCelebration(RE::Actor& actor, RE::TESIdleForm* idle)
    {
        auto* process = actor.GetActorRuntimeData().currentProcess;
        if (!process || !idle) {
            return false;
        }
#ifdef WAYFARER_MODERN_COMMONLIB
        return process->PlayIdle(&actor, idle, nullptr);
#else

        using Function = bool (*)(RE::AIProcess*, RE::Actor*, RE::DEFAULT_OBJECT, RE::TESIdleForm*, bool,
                                  bool, RE::TESObjectREFR*);
        static REL::Relocation<Function> play{RELOCATION_ID(38290, 39256)};
        return play(process, &actor, RE::DEFAULT_OBJECT::kActionIdle, idle, true, false, nullptr);
#endif
    }

    bool StopCelebration(RE::Actor& actor, RE::TESIdleForm* idle)
    {
        Wayfarer::Victory::Animation::Select(actor.GetFormID(), false);

        if (!actor.Is3DLoaded() || actor.IsDead() || !OwnsIdle(actor, idle)) {
            return true;
        }
        if (InBattle(actor) || actor.GetCurrentScene() || actor.GetOccupiedFurniture().get() ||
            Speaking(actor)) {
            return false;
        }
#ifdef WAYFARER_MODERN_COMMONLIB
        actor.GetActorRuntimeData().currentProcess->StopCurrentIdle(&actor, true);
#else
        using Function = void (*)(RE::AIProcess*, RE::Actor*, bool);
        static REL::Relocation<Function> stop{RELOCATION_ID(38291, 39257)};
        stop(actor.GetActorRuntimeData().currentProcess, &actor, true);
#endif
        return true;
    }

    Wayfarer::Victory::Equipment CelebrationEquipment(RE::Actor& actor)
    {
        using Equipment = Wayfarer::Victory::Equipment;
        auto* state = actor.AsActorState();
        if (!state) {
            return Equipment::Unsupported;
        }
        const auto weaponState = state->GetWeaponState();
        if (weaponState == RE::WEAPON_STATE::kSheathed) {
            return Equipment::Sheathed;
        }
        if (weaponState != RE::WEAPON_STATE::kDrawn) {
            return Equipment::Transition;
        }
        auto* right = actor.GetEquippedObject(false);
        auto* left = actor.GetEquippedObject(true);
        if (!right && !left) {
            return Equipment::EmptyHands;
        }
        // A free hand or shield is fine; don't cheer with a second blade/spell.
        if (left && left != right && (left->As<RE::TESObjectWEAP>() || left->As<RE::SpellItem>())) {
            return Equipment::Unsupported;
        }
        auto* weapon = right ? right->As<RE::TESObjectWEAP>() : nullptr;
        if (!weapon) {
            return Equipment::Unsupported;
        }
        switch (weapon->GetWeaponType()) {
        case RE::WEAPON_TYPE::kOneHandSword:
            return Equipment::Sword;
        case RE::WEAPON_TYPE::kTwoHandSword:
            return Equipment::Greatsword;
        default:
            return Equipment::Unsupported;
        }
    }
} // namespace

namespace Wayfarer
{
    void FormationController::InitializeVictoryEvents()
    {
        BattleEvents::Get().Register();
        logger::info("[Victory] Revision 4: minimum={} enemies; randomized sheathed shoulder recovery={}; "
                     "start window={:.1f}s",
                     Victory::Encounter::minimumEnemies, Victory::Animation::Available(),
                     Victory::startWindowSeconds);
    }

    void FormationController::ReportAliasSync(bool busy)
    {
        std::lock_guard lock(stateMutex);
        if (aliasSyncBusy && !busy) {
            ++completedAliasSyncs;
        }
        aliasSyncBusy = busy;
    }

    void FormationController::UpdateVictoryCleanup()
    {
        if (auto* ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
            return;
        }
        std::erase_if(victoryCleanup, [](const VictoryReaction& reaction) {
            auto actor = reaction.actor.get();
            return !actor || StopCelebration(*actor, reaction.idle);
        });
    }

    void FormationController::ResetVictoryCelebration(bool stopAnimations)
    {
        Victory::Animation::Clear();
        if (stopAnimations) {
            for (const auto& reaction : victoryReactions) {
                if (auto actor = reaction.actor.get();
                    actor && reaction.started && !StopCelebration(*actor, reaction.idle)) {
                    victoryCleanup.push_back(reaction);
                }
            }
        }
        else {
            victoryCleanup.clear();
        }
        victoryReactions.clear();
        victoryEncounter.Reset();
        victoryEnemies.clear();
        victoryScanTimer = 0;
        BattleEvents::Get().Clear();
    }

    std::string FormationController::VictoryActorRejection(RE::Actor& actor, RE::PlayerCharacter& player,
                                                           bool starting) const
    {
        const auto id = actor.GetFormID();
        if (!managed.contains(id) || explicitExclusions.contains(id)) {
            return "not a managed eligible companion";
        }
        // Only this caller permits a drawn weapon; all follower/framework checks
        // still run, including those after the ordinary travel weapon gate.
        if (auto reason = UsabilityRejection(actor, player, false, true); !reason.empty()) {
            return reason;
        }
        if (!SameSpace(actor, player) || !actor.Is3DLoaded()) {
            return "not loaded in the player's space";
        }
        if (InBattle(actor)) {
            return "still fighting or searching";
        }
        if (actor.GetCurrentScene() || Speaking(actor)) {
            return "scene or speech in progress";
        }
        if (actor.IsInKillMove() || actor.IsInMidair() || actor.IsInRagdollState() ||
            actor.GetOccupiedFurniture().get() || actor.IsSneaking()) {
            return "unsafe movement or furniture";
        }
        auto* state = actor.AsActorState();
        if (!state || state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal || state->IsFlying() ||
            state->IsBleedingOut() || state->IsUnconscious()) {
            return "unsafe actor state";
        }
        auto* npc = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13794);
        if (!npc || !actor.GetRace() || !actor.GetRace()->HasKeyword(npc)) {
            return "not a humanoid animation rig";
        }
        const auto offset = actor.GetPosition() - player.GetPosition();
        if (offset.Length() > 900.0F || std::abs(offset.z) > 180.0F) {
            return "too far from player";
        }
        if (starting) {

            const auto* alias = GetRefAlias(questCache, FollowerAlias(managed.at(id).slot));
            if (alias && alias->GetReference() == &actor) {
                return "movement alias still assigned";
            }
            for (const auto& cleanup : victoryCleanup) {
                if (cleanup.actor == actor.GetHandle()) {
                    return "previous reaction cleanup pending";
                }
            }
            bool driven = false;
            actor.GetGraphVariableBool("bAnimationDriven", driven);
            RE::NiPoint3 velocity{};
            actor.GetLinearVelocity(velocity);
            if (driven) {
                return "another animation owns the actor";
            }
            if (velocity.Length() > 30.0F) {
                return "companion still moving";
            }
        }
        const auto* process = actor.GetActorRuntimeData().currentProcess;
        return process && process->middleHigh ? "" : "animation process unavailable";
    }

    void FormationController::UpdateVictoryCelebration(RE::PlayerCharacter& player, float dt)
    {
        const auto& settings = Settings::GetSingleton().Get();
        auto* ui = RE::UI::GetSingleton();
        auto* state = player.AsActorState();
        auto* controls = RE::ControlMap::GetSingleton();
        if (!enabled || !settings.victoryCelebrations || player.IsDead() || !state ||
            (ui && (ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || ui->IsMenuOpen(RE::MainMenu::MENU_NAME)))) {
            ResetVictoryCelebration();
            return;
        }
        std::vector<RE::NiPointer<RE::Actor>> followers;
        std::unordered_set<RE::FormID> party{player.GetFormID()};
        bool fighting = InBattle(player);
        for (const auto& [id, follower] : managed) {
            auto actor = follower.handle.get();
            if (!actor || actor->IsDead() || actor->IsDisabled() || !actor->Is3DLoaded() ||
                explicitExclusions.contains(id) || !SameSpace(*actor, player) ||
                (!IsRegisteredFollower(id) && CandidateRejection(*actor))) {
                continue;
            }
            party.insert(id);
            followers.push_back(actor);
            if (InBattle(*actor)) {
                fighting = true;
                victoryEncounter.ObserveParticipant(id);
            }
        }
        auto observeEnemy = [&](RE::Actor& actor) {
            if (party.contains(actor.GetFormID()) || actor.IsPlayerTeammate()) {
                return;
            }
            victoryEncounter.ObserveEnemy(actor.GetFormID());
            victoryEnemies[actor.GetFormID()] = actor.GetHandle();
            if (actor.IsDead()) {
                victoryEncounter.MarkDefeated(actor.GetFormID());
            }
        };
        bool lostEvents = false;
        const auto events = BattleEvents::Get().Drain(lostEvents);
        if (lostEvents) {
            logger::warn("[Victory] Encounter discarded: combat event queue overflow");
            ResetVictoryCelebration();
            return;
        }
        for (const auto& event : events) {
            if (event.dying) {
                victoryEncounter.MarkDefeated(event.dying);
                continue;
            }
            auto actor = event.actor.get();
            auto target = event.target.get();
            if (!actor || !target) {
                continue;
            }
            const bool actorParty = party.contains(actor->GetFormID());
            const bool targetParty = party.contains(target->GetFormID());
            if (actorParty == targetParty) {
                continue;
            }
            auto& member = actorParty ? *actor : *target;
            auto& enemy = actorParty ? *target : *actor;
            if (enemy.IsPlayerTeammate()) {
                continue;
            }

            fighting = true;
            observeEnemy(enemy);
            if (!member.IsPlayerRef()) {
                victoryEncounter.ObserveParticipant(member.GetFormID());
            }
        }

        victoryScanTimer += dt;
        if (victoryScanTimer >= .2F) {
            victoryScanTimer = 0;
            auto collectTargets = [&](RE::Actor& member) {
                auto* group = member.GetCombatGroup();
                if (!group || !InBattle(member)) {
                    return;
                }
                fighting = true;
                if (!member.IsPlayerRef()) {
                    victoryEncounter.ObserveParticipant(member.GetFormID());
                }
                std::vector<RE::ActorHandle> targets;
                {
                    RE::BSReadLockGuard guard(group->lock);
                    for (const auto& target : group->targets) {
                        targets.push_back(target.targetHandle);
                    }
                }
                for (auto handle : targets) {
                    if (auto actor = handle.get(); actor && !actor->IsDead()) {
                        observeEnemy(*actor);
                    }
                }
            };
            collectTargets(player);
            for (const auto& actor : followers) {
                collectTargets(*actor);
            }
        }
        for (const auto& [id, handle] : victoryEnemies) {
            if (auto enemy = handle.get()) {
                if (enemy->IsDead()) {
                    victoryEncounter.MarkDefeated(id);
                }
                else {
                    victoryEncounter.defeated.erase(id);
                }
            }
        }

        RE::NiPoint3 velocity{};
        player.GetLinearVelocity(velocity);
        const bool canCelebrate =
            !player.IsSneaking() && !player.IsOnMount() && !state->IsSwimming() && !state->IsBleedingOut() &&
            !player.IsInKillMove() && !player.IsInMidair() && !player.GetCurrentScene() &&
            (!controls || controls->IsMovementControlsEnabled()) && velocity.Length() < 180.0F;
        if (fighting || !canCelebrate) {
            Victory::Animation::Clear();
            if (!victoryReactions.empty()) {
                logger::info("[Victory] Reactions cancelled: partyFighting={} playerReady={}", fighting,
                             canCelebrate);
            }
            for (const auto& reaction : victoryReactions) {
                if (auto actor = reaction.actor.get();
                    actor && reaction.started && !StopCelebration(*actor, reaction.idle)) {
                    victoryCleanup.push_back(reaction);
                }
            }
            victoryReactions.clear();
        }

        const auto outcome = victoryEncounter.Tick(dt, fighting);
        if (outcome != Victory::Result::None) {
            if (outcome == Victory::Result::Victory && canCelebrate) {
                ++victorySequence;
                std::sort(followers.begin(), followers.end(),
                          [](const auto& a, const auto& b) { return a->GetFormID() < b->GetFormID(); });
                for (const auto& actor : followers) {
                    if (!victoryEncounter.participants.contains(actor->GetFormID())) {
                        continue;
                    }
                    const auto variation = RestPose::Mix(actor->GetFormID() + victorySequence * 7919);

                    VictoryReaction reaction;
                    reaction.actor = actor->GetHandle();
                    reaction.actorID = actor->GetFormID();
                    reaction.delay = .4F + (variation % 7) * .1F;
                    reaction.variation = variation;
                    reaction.queuedAtAliasSync = completedAliasSyncs;
                    victoryReactions.push_back(std::move(reaction));
                }
                logger::info("[Victory] {} distinct enemies defeated; {} participating companions queued",
                             victoryEncounter.enemies.size(), victoryReactions.size());
            }
            else {
                logger::info(
                    "[Victory] No reaction: tracked={} defeated={} participants={} minimum={} playerReady={}",
                    victoryEncounter.enemies.size(), victoryEncounter.defeated.size(),
                    victoryEncounter.participants.size(), Victory::Encounter::minimumEnemies, canCelebrate);
            }
            victoryEncounter.Reset();
            victoryEnemies.clear();
        }

        std::erase_if(victoryReactions, [&](VictoryReaction& reaction) {
            auto finish = [&] {
                Victory::Animation::Select(reaction.actorID, false);
                return true;
            };
            auto actor = reaction.actor.get();
            if (!actor) {
                return finish();
            }
            reaction.elapsed += dt;
            if (!reaction.started) {
                if (reaction.elapsed > Victory::startWindowSeconds) {
                    logger::info("[Victory] {} expired before playback: {}", actor->GetName(),
                                 reaction.lastReason);
                    return finish();
                }
                if (reaction.elapsed < reaction.delay || reaction.elapsed < reaction.nextAttempt) {
                    return false;
                }
                auto wait = [&](std::string reason) {
                    if (reason != reaction.lastReason) {
                        logger::info("[Victory] {} waiting: {}", actor->GetName(), reason);
                        reaction.lastReason = std::move(reason);
                    }
                    return false;
                };
                if (auto reason = VictoryActorRejection(*actor, player, true); !reason.empty()) {
                    return wait(std::move(reason));
                }
                if (!Victory::HandoffComplete(reaction.queuedAtAliasSync, completedAliasSyncs)) {
                    return wait("waiting for first completed movement cleanup pass");
                }
                reaction.equipment = CelebrationEquipment(*actor);
                const bool shoulderAvailable = Victory::Animation::Available();
                const auto choice =
                    Victory::ChooseIdle(reaction.equipment, reaction.variation, shoulderAvailable);
                if (!choice.form) {
                    return wait(Victory::RequestsShoulder(reaction.variation, shoulderAvailable)
                                    ? "selected shoulder recovery is waiting for fully sheathed weapons"
                                : reaction.equipment == Victory::Equipment::Transition
                                    ? "drawing or sheathing"
                                    : "drawn equipment has no compatible victory idle; waiting for sheathe");
                }
                reaction.idle = RE::TESForm::LookupByID<RE::TESIdleForm>(choice.form);
                if (!reaction.idle) {
                    logger::warn("[Victory] {} missing vanilla idle {:08X}", actor->GetName(), choice.form);
                    return finish();
                }
                reaction.nextAttempt = reaction.elapsed + Victory::retrySeconds;
                Victory::Animation::Select(reaction.actorID, choice.shoulder);
                if (!PlayCelebration(*actor, reaction.idle)) {
                    Victory::Animation::Select(reaction.actorID, false);
                    return wait("engine rejected idle start; retrying within window");
                }
                reaction.duration = choice.seconds;
                reaction.started = true;
                reaction.elapsed = 0;
                logger::info("[Victory] {} engine accepted {} ({:08X}) for at most {:.1f}s; armed={}",
                             actor->GetName(), choice.name, reaction.idle->GetFormID(), reaction.duration,
                             choice.armed);
                return false;
            }
            if (!OwnsIdle(*actor, reaction.idle)) {
                logger::info("[Victory] {} idle ownership ended after {:.2f}s; engine acceptance does not "
                             "confirm visible playback",
                             actor->GetName(), reaction.elapsed);
                return finish();
            }
            const auto rejection = VictoryActorRejection(*actor, player, false);
            const bool equipmentChanged = CelebrationEquipment(*actor) != reaction.equipment;
            if (reaction.elapsed >= reaction.duration || !rejection.empty() || equipmentChanged) {
                logger::info("[Victory] {} reaction ended after {:.2f}s: {}", actor->GetName(),
                             reaction.elapsed,
                             !rejection.empty() ? rejection
                             : equipmentChanged ? "equipment changed or drawing/sheathing"
                                                : "duration complete");
                if (!StopCelebration(*actor, reaction.idle)) {
                    victoryCleanup.push_back(reaction);
                }
                return finish();
            }
            return false;
        });
    }
} // namespace Wayfarer

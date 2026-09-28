// TETHER physics sequence and elastic speed approach adapted for Walk With Me.
// Copyright (c) 2026 KEI997. MIT; see ThirdParty/TETHER-LICENSE.txt.
// Upstream: https://github.com/Kei-BWV997/TETHER
// Revision: 19e04c23a3dac77c5345b8101d6a94df9895d35f
#include "RE/M/MenuTopicManager.h"
#include "command_gesture.h"
#include "engine_compatibility.h"
#include "formation_math.h"
#include "hand_holding_backends.h"
#include "tether_clasp.h"
#include "tether_movement.h"
#include "tether_session.h"

namespace Wayfarer::HandHolding::Tether
{
    namespace
    {
        std::recursive_mutex mutex;
        Tuning tuning;
        State state;
        Approach approach;
        TetherSession session;
        TetherHandoff handoff;
        TetherSeparation separation;
        TetherArrival arrival;
        TetherMovement movement;
        RE::ActorHandle playerHandle, followerHandle;
        RE::NiPointer<RE::NiAVObject> playerRoot, followerRoot;
        std::int32_t token = static_cast<std::int32_t>(GetTickCount64() & 0x3fffffff);
        RE::FormID playerCell{}, followerCell{}, world{};
        bool playerInterior{}, followerInterior{}, gripLeft{}, gripReady{}, bodiesReady{}, facingReady{};
        RE::NiPoint3 lastPlayer{}, lastFollower{};
        float pendingTime{}, traceTime{}, retryTime{};
        float acquireTime{}, approachTime{}, handoffTime{}, cooldown{};
        RE::NiAVObject* Hand(RE::Actor& actor, bool left)
        {
            auto* root = actor.Get3D(false);
            return root ? root->GetObjectByName(left ? "NPC L Hand [LHnd]" : "NPC R Hand [RHnd]") : nullptr;
        }

        bool Current(RE::Actor* actor, const RE::NiPointer<RE::NiAVObject>& root)
        {
            return actor && root && actor->Get3D(false) == root.get() && root->parent;
        }
        bool DialogueActive()
        {
            auto* ui = RE::UI::GetSingleton();
            const auto* topics = RE::MenuTopicManager::GetSingleton();
            return (ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) ||
                   (topics && Engine::IsDialogueTransition(*topics));
        }
        float WristGap(RE::Actor& player, RE::Actor& follower, bool left)
        {
            const auto* p = Hand(player, left);
            const auto* f = Hand(follower, !left);
            return p && f ? (f->world.translate - p->world.translate).Length()
                          : std::numeric_limits<float>::infinity();
        }
        const char* Rejection(RE::Actor& actor)
        {
            auto* s = actor.AsActorState();
            if (!s || !actor.Is3DLoaded() || actor.IsDisabled() || actor.IsDead() || actor.IsInCombat() ||
                actor.IsInKillMove() || actor.IsInRagdollState() || actor.IsInMidair() ||
                actor.IsSneaking() || actor.IsOnMount() || actor.GetCurrentScene() || s->IsSwimming() ||
                s->IsFlying() || s->IsSprinting() || s->IsWeaponDrawn() ||
                s->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) {
                return "actor busy";
            }
            bool driven{}, offset{};
            actor.GetGraphVariableBool("bAnimationDriven", driven);
            actor.GetGraphVariableBool("bOffsetGPMA", offset);
            return driven || offset ? "another arm animation" : nullptr;
        }
        void Pump();
        void Release(const char* reason, bool failed = false)
        {
            Clasp::Clear();
            if (failed) {
                arrival.Fail();
            }
            arrival.elapsed = 0;
            if (session.started && !session.cancelled) {
                logger::info("[HandHolding/TETHER] Session {} release {:08X}: {}", token, approach.actor,
                             reason);
                cooldown = failed ? 4 : 1.5F;
            }
            session.Cancel();
            state.Reset();
            approach = {};
            gripReady = bodiesReady = facingReady = false;
            Pump();
        }
        struct Completion final : RE::BSScript::IStackCallbackFunctor
        {
            std::int32_t ticket;
            TetherOp operation;
            Completion(std::int32_t value, TetherOp op) : ticket(value), operation(op) {}
            void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
            void operator()(RE::BSScript::Variable result) override
            {
                // TryStep: 0 failed, 1 completed, 2 deferred without mutation.
                const auto status = result.IsInt() ? result.GetSInt() : 0;
                const bool success = status == 1;
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([value = ticket, op = operation, success, status] {
                        std::lock_guard lock(mutex);
                        if (value == token && status == 2 && session.Defer(op)) {
                            logger::info("[HandHolding/TETHER] Session {} operation {} deferred after "
                                         "{:.3f}s; retrying fresh pose",
                                         value, static_cast<int>(op), pendingTime);
                            pendingTime = 0;
                            retryTime = .05F;
                            return;
                        }
                        if (value != token || !session.Complete(op, success)) {
                            return;
                        }
                        logger::info("[HandHolding/TETHER] Session {} operation {} completed={} "
                                     "callback={:.3f}s setup={:.3f}s",
                                     value, static_cast<int>(op), success, pendingTime, acquireTime);
                        pendingTime = 0;
                        if (!success) {
                            Clasp::Clear();
                            state.Reset();
                            approach = {};
                            arrival.Fail();
                            cooldown = 4;
                            if (IsCleanup(op)) {
                                retryTime = 1;
                            }
                        }
                        if (op == TetherOp::Connect && session.connected) {
                            // The hands are already joined. Alias cleanup only
                            // gates the movement controller, never the grip.
                            handoff.Start(approach.actor, token);
                            handoffTime = 0;
                            logger::info(
                                "[HandHolding/TETHER] Physics grip connected; approach={:.3f}s "
                                "setup={:.3f}s total={:.3f}s; movement handoff follows; KEI997 / MIT",
                                approachTime, acquireTime, approachTime + acquireTime);
                        }
                        Pump();
                    });
                }
            }
        };
        void Pump()
        {
            if (session.Done()) {
                Clasp::Clear();
                logger::info("[HandHolding/TETHER] Session {} cleanup finished", token);
                session = {};
                handoff = {};
                movement = {};
                playerHandle = {};
                followerHandle = {};
                playerRoot.reset();
                followerRoot.reset();
                state.Reset();
                approach = {};
                return;
            }
            const auto op = session.Next();
            if (!op || retryTime > 0) {
                return;
            }
            // A connected pair may already be walking when cleanup finishes.
            // Only the old package's completed cleanup gates offset movement.
            if ((NeedsHandoff(*op) && !handoff.ready) || (NeedsHeading(*op) && !bodiesReady) ||
                (NeedsGrip(*op) && !gripReady)) {
                return;
            }
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) {
                Clasp::Clear();
                session.Cancel();
                return;
            }
            auto player = playerHandle.get(), follower = followerHandle.get();
            session.Begin(*op);
            if (*op == TetherOp::Offset && follower) {
                const auto* package = follower->GetCurrentPackage();
                movement.Requested(package ? package->GetFormID() : 0);
            }
            pendingTime = 0;
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{new Completion(token, *op)};
            // The authored clasp turns the palm surfaces toward one another.
            // Match its palm depth rather than joining the centers of the wrists.
            auto* args = RE::MakeFunctionArguments(
                std::int32_t{token}, static_cast<std::int32_t>(*op), static_cast<RE::Actor*>(player.get()),
                static_cast<RE::Actor*>(follower.get()), bool{gripLeft}, float{tuning.palmX},
                float{tuning.palmY}, float{tuning.palmZ}, float{tuning.palmX}, float{tuning.palmY},
                float{tuning.palmZ});
            if (!vm->DispatchStaticCall("WayfarerTether"sv, "TryStep"sv, args, callback)) {
                logger::error(
                    "[HandHolding/TETHER] Cannot dispatch operation {}; install matching WayfarerTether.pex",
                    static_cast<int>(*op));
                if (IsCleanup(*op)) {
                    // A dispatch failure is not evidence that physics was removed.
                    // Retain ownership and retry; never start a second grip over it.
                    session.pending.reset();
                    retryTime = 1;
                    Clasp::Clear();
                    session.Cancel();
                    arrival.Fail();
                    return;
                }
                session.Complete(*op, false);
                Clasp::Clear();
                state.Reset();
                approach = {};
                arrival.Fail();
                cooldown = 4;
                Pump();
            }
        }
        struct EquipSink final : RE::BSTEventSink<RE::TESEquipEvent>
        {
            RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* event,
                                                  RE::BSTEventSource<RE::TESEquipEvent>*) override
            {
                if (event && event->actor) {
                    std::lock_guard lock(mutex);
                    if (session.started && (event->actor->GetHandle() == playerHandle ||
                                            event->actor->GetHandle() == followerHandle)) {
                        Release("equipment changed");
                    }
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        } equipSink;
    } // namespace

    std::string ReferenceKey(RE::Actor& actor)
    {
        if ((actor.GetFormID() >> 24) == 0xFF) {
            return {};
        }
        auto* file = actor.GetFile(0);
        if (!file) {
            return {};
        }
        auto key = fmt::format("{}|{:06X}", file->GetFilename(),
                               actor.GetFormID() & (file->IsLight() ? 0xFFF : 0xFFFFFF));
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return key;
    }
    void Install()
    {
        if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
            events->AddEventSink<RE::TESEquipEvent>(&equipSink);
        }
        logger::info("[HandHolding] Revision 18: authored clasp and completed-cleanup handoff; TETHER by "
                     "KEI997 (MIT)");
        logger::info(
            "[HandHolding/TETHER] Responsive-facing r1: reference rotation before offset; fresh-pose gates");
        logger::info("[HandHolding/TETHER] Latency r2: priority cleanup and retryable pose validation");
        logger::info(
            "[HandHolding/TETHER] Direct grip r4: connect at arrival before movement handoff; no dwell");
        logger::info(
            "[HandHolding/TETHER] Movement r5: held facing, package renewal and stalled-follow recovery");
        logger::info(
            "[HandHolding/TETHER] Handoff r6: keep the existing travel route moving until alias cleanup");
        logger::info("[HandHolding/Clasp] Required OAR pose assets: {}",
                     Clasp::Available() ? "available" : "missing; TETHER grip disabled (Classic available)");
    }
    void Reset()
    {
        std::lock_guard lock(mutex);
        Release("controller reset");
    }
    bool CleanupPending()
    {
        std::lock_guard lock(mutex);
        return session.started || handoff.actor != 0;
    }
    void OnGameLoaded()
    {
        std::lock_guard lock(mutex);
        Clasp::Clear();
        // The engine has loaded a different physics world. Never replay teardown
        // from the old VM stacks against this world's actors, even if IDs match.
        token = token == 0x7fffffff ? 1 : token + 1;
        session = {};
        handoff = {};
        separation = {};
        arrival = {};
        movement = {};
        state.Reset();
        approach = {};
        playerHandle = {};
        followerHandle = {};
        playerRoot.reset();
        followerRoot.reset();
        pendingTime = retryTime = traceTime = acquireTime = approachTime = handoffTime = 0;
        cooldown = 3;
        gripReady = bodiesReady = facingReady = false;
        logger::info("[HandHolding/TETHER] New game/load: invalidated previous physics session");
    }
    void ReleaseActor(RE::FormID actor)
    {
        std::lock_guard lock(mutex);
        auto follower = followerHandle.get();
        if (actor && (approach.actor == actor || (follower && follower->GetFormID() == actor))) {
            Release("follower movement released");
        }
    }
    bool OwnsMovement(RE::FormID actor)
    {
        std::lock_guard lock(mutex);
        return actor && handoff.actor == actor;
    }
    std::int32_t HandoffTicket(RE::FormID actor)
    {
        std::lock_guard lock(mutex);
        return actor && handoff.actor == actor && !session.cancelled ? handoff.ticket : 0;
    }
    void ConfirmHandoff(RE::FormID actor, std::int32_t ticket, bool aliasEmpty, bool packageReleased)
    {
        std::lock_guard lock(mutex);
        if (session.cancelled) {
            return;
        }
        const bool alreadyComplete = handoff.cleanupComplete;
        if (handoff.RecordCleanup(actor, ticket, aliasEmpty) && !alreadyComplete) {
            logger::info("[HandHolding/TETHER] Session {} script cleanup complete; packageReleased={} "
                         "afterGrip={:.3f}s",
                         ticket, packageReleased, handoffTime);
        }
        if (!handoff.ready && !session.cancelled &&
            handoff.Confirm(actor, ticket, aliasEmpty, packageReleased)) {
            logger::info("[HandHolding/TETHER] Session {} WWM alias cleanup acknowledged", token);
        }
        // Next validated frame starts offset; never start it on the Papyrus thread.
    }
    bool HandoffCleanupFinished(RE::FormID actor, std::int32_t ticket)
    {
        std::lock_guard lock(mutex);
        return actor && handoff.actor == actor && handoff.ticket == ticket && handoff.cleanupComplete &&
               !handoff.ready && !session.cancelled;
    }
    bool BeginFrame(float dt, bool permitted, bool paused, const Tuning& value)
    {
        std::lock_guard lock(mutex);
        // A live constraint was built from the old palm anchors, so an ini reload
        // that moves them has to release and rebuild rather than take effect only
        // on the next grip.
        const bool changed = value.partner != tuning.partner || value.enabled != tuning.enabled ||
                             value.delay != tuning.delay || value.palmX != tuning.palmX ||
                             value.palmY != tuning.palmY || value.palmZ != tuning.palmZ ||
                             value.gripGap != tuning.gripGap;
        tuning = value;
        if (changed) {
            arrival = {};
        }
        if (!paused) {
            retryTime = std::max(0.0F, retryTime - dt);
            cooldown = std::max(0.0F, cooldown - dt);
        }
        else {
            movement.Pause();
        }
        if (changed || !permitted || !value.enabled || value.partner.empty() || !Clasp::Available()) {
            Release("settings or context changed");
        }
        if (session.cancelled) {
            Pump();
        }
        if (session.pending && !paused) {
            pendingTime += dt;
            if (pendingTime > 5 && !session.cancelled) {
                Release("physics callback timed out; waiting for cleanup", true);
            }
        }
        if (session.cancelled && session.started) {
            return false;
        }
        if (!permitted || paused || cooldown > 0 || !Clasp::Available()) {
            return false;
        }
        state.Tick(dt, permitted, paused, tuning);
        return state.Searching() || session.started;
    }

    std::int32_t TetherStepStatus(std::int32_t ticket, std::int32_t operation, RE::Actor* player,
                                  RE::Actor* follower)
    {
        std::lock_guard lock(mutex);
        if (operation < 0 || operation > 7 || !TetherStepCurrent(session, token, ticket, operation)) {
            return 0;
        }
        const bool p = player && player->GetHandle() == playerHandle && Current(player, playerRoot);
        const bool f = follower && follower->GetHandle() == followerHandle && Current(follower, followerRoot);
        // Status 2 retires only resources belonging to vanished/replaced skeletons.
        if (operation == 3) {
            return p && f ? 1 : 2;
        }
        if (operation == 4) {
            return f ? 1 : 2;
        }
        if (operation == 5) {
            return p ? 1 : 2;
        }
        if (operation == 7) {
            return follower && follower->GetHandle() == followerHandle ? 1 : 2;
        }
        if (!p || !f || (NeedsHandoff(static_cast<TetherOp>(operation)) && !handoff.ready) ||
            DialogueActive() || Rejection(*player) || Rejection(*follower)) {
            return 0;
        }
        auto* camera = RE::PlayerCamera::GetSingleton();
        auto* controls = RE::ControlMap::GetSingleton();
        auto* pc = player->GetParentCell();
        auto* fc = follower->GetParentCell();
        if (!camera || camera->IsInFirstPerson() || !controls || !controls->IsMovementControlsEnabled() ||
            !pc || !fc || player->GetWorldspace() != follower->GetWorldspace() ||
            ((pc->IsInteriorCell() || fc->IsInteriorCell()) && pc != fc)) {
            return 0;
        }
        const auto delta = follower->GetPosition() - player->GetPosition();
        bool unknown{};
        const bool visible = player->HasLineOfSight(follower, unknown);
        RE::NiPoint3 velocity{}, followerVelocity{};
        player->GetLinearVelocity(velocity);
        follower->GetLinearVelocity(followerVelocity);
        const auto forward = ForwardFromYaw(player->GetAngleZ());
        const float lateral = delta.x * forward.y - delta.y * forward.x;
        const float heading = std::remainder(follower->GetAngleZ() - player->GetAngleZ(), 6.283185307F);
        const int side = gripLeft ? -1 : 1;
        if (operation == 6) {
            return session.connected && TetherNear(delta.Length(), delta.z, visible, tuning.releaseDistance)
                       ? 1
                       : 3;
        }
        // The ragdoll bodies are created once the follower has turned into place.
        // Only the constraint needs the palms already together, so the last-resort
        // check here matches the native gate for the operation being dispatched.
        if (operation != 2) {
            return TetherNear(delta.Length(), delta.z, visible, tuning.connectDistance) &&
                           std::isfinite(velocity.Length()) && velocity.Length() <= 15 &&
                           TetherHeadingSettled(heading, lateral, side, followerVelocity.Length())
                       ? 1
                       : 3;
        }
        return TetherAlignedGrip(delta.Length(), delta.z, WristGap(*player, *follower, gripLeft), visible,
                                 tuning.connectDistance, lateral, side, heading, velocity.Length(),
                                 followerVelocity.Length(), tuning.gripGap)
                   ? 1
                   : 3;
    }

    void UpdatePartner(RE::PlayerCharacter& player, RE::Actor* partner, float dt, const TravelNavigation&)
    {
        std::lock_guard lock(mutex);
        auto* camera = RE::PlayerCamera::GetSingleton();
        auto* controls = RE::ControlMap::GetSingleton();
        if (!partner) {
            Release("selected companion is not eligible");
            return;
        }
        if (const auto* reason = Rejection(player)) {
            Release(reason);
            return;
        }
        if (const auto* reason = Rejection(*partner)) {
            Release(reason);
            return;
        }
        if (!camera || camera->IsInFirstPerson()) {
            Release("third-person camera unavailable");
            return;
        }
        if (!controls || !controls->IsMovementControlsEnabled()) {
            Release("movement controls unavailable");
            return;
        }
        if (DialogueActive()) {
            Release("dialogue opened");
            return;
        }
        if (CommandGesture::SavedOwnership()) {
            Release("command gesture owns animation");
            return;
        }
        auto* pCell = player.GetParentCell();
        auto* fCell = partner->GetParentCell();
        if (!pCell || !fCell || player.GetWorldspace() != partner->GetWorldspace() ||
            ((pCell->IsInteriorCell() || fCell->IsInteriorCell()) && pCell != fCell)) {
            Release("travel space changed");
            return;
        }
        const auto pos = player.GetPosition(), other = partner->GetPosition(), delta = other - pos;
        const float distance = delta.Length();
        if (!std::isfinite(distance)) {
            Release("invalid separation");
            return;
        }
        auto* pWorld = player.GetWorldspace();
        const auto worldID = pWorld ? pWorld->GetFormID() : 0;
        RE::NiPoint3 velocity{};
        player.GetLinearVelocity(velocity);
        bool unknown{};
        const bool visible = player.HasLineOfSight(partner, unknown);
        traceTime += dt;
        if (session.started) {
            const auto expected = followerHandle.get();
            const bool boundary =
                (playerCell != pCell->GetFormID() && (playerInterior || pCell->IsInteriorCell())) ||
                (followerCell != fCell->GetFormID() && (followerInterior || fCell->IsInteriorCell()));
            if (!expected || expected.get() != partner || !Current(&player, playerRoot) ||
                !Current(partner, followerRoot) || world != worldID || boundary ||
                (pos - lastPlayer).Length() > 200 || (other - lastFollower).Length() > 200) {
                Release("skeleton or travel transition");
                return;
            }
            if (session.connected ? separation.Release(distance, tuning.releaseDistance, dt)
                                  : distance > 100) {
                Release("separation limit");
                return;
            }
            if (!session.connected && velocity.Length() > 15) {
                Release("player resumed travel before grip");
                return;
            }
            const auto playerStep = pos - lastPlayer, followerStep = other - lastFollower;
            const float playerMotion = std::hypot(playerStep.x, playerStep.y) / dt;
            const float followerMotion = std::hypot(followerStep.x, followerStep.y) / dt;
            playerCell = pCell->GetFormID();
            followerCell = fCell->GetFormID();
            lastPlayer = pos;
            lastFollower = other;
            approach.velocity = {velocity.x, velocity.y, 0};
            approach.heading = player.GetAngleZ();
            Clasp::Select(player.GetFormID(), partner->GetFormID(), gripLeft);
            if (session.connected) {
                const float heading = std::remainder(partner->GetAngleZ() - player.GetAngleZ(), 6.283185307F);
                // KeepOffset supplies translation, but does not reliably maintain
                // facing. Upstream TETHER also corrects held yaw every frame.
                if (const auto yaw = TetherHeldHeading(partner->GetAngleZ(), player.GetAngleZ(), dt)) {
                    Engine::SetFacingImmediate(*partner, *yaw);
                }
                const auto forward = ForwardFromYaw(player.GetAngleZ());
                const float lateral = delta.x * forward.y - delta.y * forward.x;
                const float along = delta.x * forward.x + delta.y * forward.y;
                const float targetError = std::hypot(lateral - (gripLeft ? -40.0F : 40.0F), along + 10);
                const auto* package = partner->GetCurrentPackage();
                const auto packageID = package ? package->GetFormID() : 0;
                if (session.offset && !session.pending) {
                    const auto action =
                        movement.Observe(packageID, targetError, playerMotion, followerMotion, dt);
                    if (action == TetherMovementAction::Release) {
                        Release("follower stalled after offset renewal", true);
                        return;
                    }
                    if (action != TetherMovementAction::None && session.RefreshOffset()) {
                        logger::info("[HandHolding/TETHER] Session {} renewing offset: {} package={:08X} "
                                     "targetError={:.1f} playerMotion={:.1f} followerMotion={:.1f}",
                                     token,
                                     action == TetherMovementAction::RefreshPackage ? "package changed"
                                                                                    : "follower stalled",
                                     packageID, targetError, playerMotion, followerMotion);
                    }
                }
                if (!session.offset) {
                    handoffTime += dt;
                    if (handoffTime > 8) {
                        Release("movement handoff timed out after grip", true);
                        return;
                    }
                }
                if (traceTime >= 1) {
                    logger::info("[HandHolding/TETHER] Held {:08X}: gap={:.1f} wrist={:.1f} "
                                 "playerSpeed={:.1f} nativeGait=true offsetMovement={} handoff={:.3f}s "
                                 "heading={:.2f} playerMotion={:.1f} followerMotion={:.1f} "
                                 "targetError={:.1f} package={:08X} renewing={}",
                                 partner->GetFormID(), distance, WristGap(player, *partner, gripLeft),
                                 velocity.Length(), session.offset, handoffTime, heading, playerMotion,
                                 followerMotion, targetError, packageID, session.refreshOffset);
                }
                Pump();
            }
            else {
                acquireTime += dt;
                RE::NiPoint3 followerVelocity{};
                partner->GetLinearVelocity(followerVelocity);
                const auto direction = ForwardFromYaw(player.GetAngleZ());
                const float lateral = delta.x * direction.y - delta.y * direction.x;
                const float heading = std::remainder(partner->GetAngleZ() - player.GetAngleZ(), 6.283185307F);
                const int side = gripLeft ? -1 : 1;
                const bool settled = TetherHeadingSettled(heading, lateral, side, followerVelocity.Length());
                bool corrected = false;
                // The travel destination now holds the arrival position. Align
                // before physics exists; never rotate a live/pending body.
                if (CanCorrectTetherFacing(session) &&
                    TetherNear(distance, delta.z, visible, tuning.connectDistance) &&
                    std::isfinite(velocity.Length()) && velocity.Length() <= 15 &&
                    std::isfinite(player.GetAngleZ()) && std::isfinite(heading) &&
                    std::isfinite(followerVelocity.Length()) && followerVelocity.Length() <= 130 &&
                    lateral * side >= 15 && std::abs(heading) > .2F) {
                    Engine::SetFacingImmediate(*partner, player.GetAngleZ());
                    corrected = true;
                    logger::info("[HandHolding/TETHER] Session {} facing corrected: before={:.2f} "
                                 "after={:.2f} elapsed={:.2f}; awaiting fresh pose",
                                 token, heading,
                                 std::remainder(partner->GetAngleZ() - player.GetAngleZ(), 6.283185307F),
                                 acquireTime);
                }
                // Bone world transforms may still describe the old pose in the
                // correction frame. Never synthesize a zero error to skip this wait.
                facingReady =
                    !corrected && TetherFacingReady(heading, lateral, side, followerVelocity.Length());
                bodiesReady = !corrected && settled;
                const auto gap = WristGap(player, *partner, gripLeft);
                const auto limit = tuning.gripGap;
                const bool aligned =
                    !corrected &&
                    TetherAlignedGrip(distance, delta.z, gap, visible, tuning.connectDistance, lateral, side,
                                      heading, velocity.Length(), followerVelocity.Length(), limit);
                gripReady = aligned;
                if (acquireTime > 8) {
                    Release("direct grip timed out; leave contact area before retry", true);
                    return;
                }
                if (traceTime >= 1) {
                    const auto* package = partner->GetCurrentPackage();
                    logger::info("[HandHolding/TETHER] Session {} gate={} root={:.1f} wrist={:.1f}/{:.1f} "
                                 "height={:.1f} LOS={} package={:08X} heading={:.2f} followerSpeed={:.1f}",
                                 token,
                                 !facingReady                           ? "facing correction"
                                 : !bodiesReady                         ? "follower turn"
                                 : !session.player || !session.follower ? "ragdoll bodies"
                                 : !gripReady                           ? "grip proximity"
                                                                        : "constraint callback",
                                 distance, gap, limit, delta.z, visible, package ? package->GetFormID() : 0,
                                 heading, followerVelocity.Length());
                }
                Pump();
            }
            if (traceTime >= 1) {
                traceTime = 0;
            }
            return;
        }
        if (!state.Searching()) {
            return;
        }
        const auto direction = ForwardFromYaw(player.GetAngleZ());
        const RE::NiPoint3 right{direction.y, -direction.x, 0};
        if (approach.actor != partner->GetFormID()) {
            approachTime = 0;
            approach = {partner->GetFormID(), delta.Dot(right) < 0 ? -1 : 1,
                        SeekSpacing(tuning.connectDistance)};
            logger::info("[HandHolding/TETHER] {} close approach requested through WWM travel; side={}",
                         partner->GetName(), approach.side);
        }
        approachTime += dt;
        // Keep the chosen side for this approach; crossing the centerline must not
        // move the target to the other side and send the follower back across us.
        approach.spacing = SeekSpacing(tuning.connectDistance);
        approach.heading = player.GetAngleZ();
        approach.velocity = {velocity.x, velocity.y, 0};
        RE::NiPoint3 followerVelocity{};
        partner->GetLinearVelocity(followerVelocity);
        const auto gap = WristGap(player, *partner, approach.side < 0);
        const float heading = std::remainder(partner->GetAngleZ() - player.GetAngleZ(), 6.283185307F);
        if (!arrival.Observe(
                delta.Dot(right) * approach.side >= 20 &&
                    TetherNear(distance, delta.z, visible, std::min(90.0F, tuning.connectDistance)),
                distance, velocity.Length(), followerVelocity.Length(), delta.Dot(right),
                delta.x * direction.x + delta.y * direction.y, heading, dt)) {
            if (traceTime >= 1) {
                const auto* package = partner->GetCurrentPackage();
                logger::info("[HandHolding/TETHER] Arrival gate={} root={:.1f} wrist={:.1f} "
                             "playerSpeed={:.1f} followerSpeed={:.1f} heading={:.2f} package={:08X}",
                             arrival.failed ? "failed attempt; step away"
                                            : "WWM close approach; pause to connect",
                             distance, gap, velocity.Length(), followerVelocity.Length(), heading,
                             package ? package->GetFormID() : 0);
                traceTime = 0;
            }
            return;
        }
        auto* pRoot = player.Get3D(false);
        auto* fRoot = partner->Get3D(false);
        gripLeft = approach.side < 0;
        token = token == 0x7fffffff ? 1 : token + 1;
        playerHandle = player.GetHandle();
        followerHandle = partner->GetHandle();
        playerRoot.reset(pRoot);
        followerRoot.reset(fRoot);
        playerCell = pCell->GetFormID();
        followerCell = fCell->GetFormID();
        world = worldID;
        playerInterior = pCell->IsInteriorCell();
        followerInterior = fCell->IsInteriorCell();
        lastPlayer = pos;
        lastFollower = other;
        traceTime = acquireTime = handoffTime = 0;
        gripReady = bodiesReady = facingReady = false;
        separation = {};
        movement = {};
        arrival.elapsed = 0;
        approach.holdPosition = true;
        approach.position = {other.x, other.y, other.z};
        Clasp::Select(player.GetFormID(), partner->GetFormID(), gripLeft);
        session.Start();
        logger::info(
            "[HandHolding/TETHER] Session {} direct grip for {:08X}; root={:.1f}; holding arrival position",
            token, partner->GetFormID(), distance);
    }
    // Hold the arrival destination during setup, then follow the player's side
    // with the existing route while the serialized script hands movement over.
    Approach GetApproach()
    {
        std::lock_guard lock(mutex);
        if (!state.Searching() || !TetherRetainsApproach(session, handoff) || arrival.failed ||
            cooldown > 0) {
            return {};
        }
        auto result = approach;
        result.handoffTravel = session.connected && handoff.actor != 0;
        if (result.handoffTravel) {
            result.holdPosition = false;
        }
        return result;
    }
    bool IsAttached(RE::FormID actor)
    {
        std::lock_guard lock(mutex);
        auto f = followerHandle.get();
        return actor && session.connected && f && f->GetFormID() == actor;
    }
    float SpeedMultiplier(RE::Actor*)
    {
        return TetherFollowerSpeed(0, false);
    }
} // namespace Wayfarer::HandHolding::Tether

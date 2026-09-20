// TETHER physics sequence and elastic speed approach adapted for Walk With Me.
// Copyright (c) 2026 KEI997. MIT; see ThirdParty/TETHER-LICENSE.txt.
// Upstream: https://github.com/Kei-BWV997/TETHER
// Revision: 19e04c23a3dac77c5345b8101d6a94df9895d35f
#include "RE/M/MenuTopicManager.h"
#include "command_gesture.h"
#include "engine_compatibility.h"
#include "formation_math.h"
#include "hand_holding.h"
#include "tether_clasp.h"
#include "tether_session.h"

namespace Wayfarer::HandHolding
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
        RE::ActorHandle playerHandle, followerHandle;
        RE::NiPointer<RE::NiAVObject> playerRoot, followerRoot;
        std::int32_t token = static_cast<std::int32_t>(GetTickCount64() & 0x3fffffff);
        RE::FormID playerCell{}, followerCell{}, world{};
        bool playerInterior{}, followerInterior{}, gripLeft{}, gripReady{}, bodiesReady{};
        RE::NiPoint3 lastPlayer{}, lastFollower{};
        float pendingTime{}, traceTime{}, retryTime{};
        float acquireTime{}, contactTime{}, cooldown{};
        float GripGap()
        {
            return TetherGripGap(acquireTime, tuning.gripGap, tetherGripGapRelaxed, tetherGripGapPatience);
        }
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
                logger::info("[HandHolding/TETHER] Session {} release {:08X}: {}", token, handoff.actor,
                             reason);
                cooldown = failed ? 4 : 1.5F;
            }
            session.Cancel();
            state.Reset();
            approach = {};
            gripReady = bodiesReady = false;
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
                const bool success = result.IsBool() && result.GetBool();
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([value = ticket, op = operation, success] {
                        std::lock_guard lock(mutex);
                        if (value != token || !session.Complete(op, success)) {
                            return;
                        }
                        pendingTime = 0;
                        logger::info("[HandHolding/TETHER] Session {} operation {} completed={}", value,
                                     static_cast<int>(op), success);
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
                        if (session.connected) {
                            logger::info("[HandHolding/TETHER] Physics grip connected; KEI997 / MIT");
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
            // Each operation waits only on what it actually depends on. The ragdoll
            // bodies are added while the pair is still turning into place, so the
            // constraint is the single Papyrus round trip left after contact.
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
            pendingTime = 0;
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{new Completion(token, *op)};
            // The authored clasp turns the palm surfaces toward one another.
            // Match its palm depth rather than joining the centers of the wrists.
            auto* args = RE::MakeFunctionArguments(
                std::int32_t{token}, static_cast<std::int32_t>(*op), static_cast<RE::Actor*>(player.get()),
                static_cast<RE::Actor*>(follower.get()), bool{gripLeft}, float{tuning.palmX},
                float{tuning.palmY}, float{tuning.palmZ}, float{tuning.palmX}, float{tuning.palmY},
                float{tuning.palmZ});
            if (!vm->DispatchStaticCall("WayfarerTether"sv, "Step"sv, args, callback)) {
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
        logger::info("[HandHolding/Clasp] Required OAR pose assets: {}",
                     Clasp::Available() ? "available" : "missing; grip disabled");
    }
    void Reset()
    {
        std::lock_guard lock(mutex);
        Release("controller reset");
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
        state.Reset();
        approach = {};
        playerHandle = {};
        followerHandle = {};
        playerRoot.reset();
        followerRoot.reset();
        pendingTime = retryTime = traceTime = acquireTime = contactTime = 0;
        cooldown = 3;
        gripReady = bodiesReady = false;
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
            logger::info("[HandHolding/TETHER] Session {} script cleanup complete; packageReleased={}",
                         ticket, packageReleased);
        }
        if (!handoff.ready && !session.cancelled &&
            handoff.Confirm(actor, ticket, aliasEmpty, packageReleased)) {
            logger::info("[HandHolding/TETHER] Session {} WWM alias cleanup acknowledged", token);
        }
        // Next validated frame starts setup; never start it on the Papyrus thread.
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
        if (!p || !f || !handoff.ready || DialogueActive() || Rejection(*player) || Rejection(*follower)) {
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
            return TetherNear(delta.Length(), delta.z, visible, 100) && std::isfinite(velocity.Length()) &&
                           velocity.Length() <= 15 && lateral * side >= 15
                       ? 1
                       : 0;
        }
        // The ragdoll bodies are created once the follower has turned into place.
        // Only the constraint needs the palms already together, so the last-resort
        // check here matches the native gate for the operation being dispatched.
        if (operation != 2) {
            return TetherNear(delta.Length(), delta.z, visible, tuning.connectDistance) &&
                           std::isfinite(velocity.Length()) && velocity.Length() <= 15 &&
                           TetherHeadingSettled(heading, lateral, side, followerVelocity.Length())
                       ? 1
                       : 0;
        }
        return TetherAlignedGrip(delta.Length(), delta.z, WristGap(*player, *follower, gripLeft), visible,
                                 tuning.connectDistance, lateral, side, heading, velocity.Length(),
                                 followerVelocity.Length(), GripGap())
                   ? 1
                   : 0;
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
            playerCell = pCell->GetFormID();
            followerCell = fCell->GetFormID();
            lastPlayer = pos;
            lastFollower = other;
            approach.velocity = {velocity.x, velocity.y, 0};
            approach.heading = player.GetAngleZ();
            if (session.offset) {
                Clasp::Select(player.GetFormID(), partner->GetFormID(), gripLeft);
            }
            if (session.connected) {
                if (traceTime >= 1) {
                    logger::info("[HandHolding/TETHER] Held {:08X}: gap={:.1f} wrist={:.1f} "
                                 "playerSpeed={:.1f} nativeGait=true offsetMovement=true",
                                 partner->GetFormID(), distance, WristGap(player, *partner, gripLeft),
                                 velocity.Length());
                }
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
                // Only turn the stationary follower after WWM's package is gone,
                // and before adding any physics bodies. This cannot steer normal travel.
                if (session.offset && !session.player && !session.pending && !settled &&
                    followerVelocity.Length() <= 20 && lateral * side >= 15 && std::abs(heading) > .2F) {
                    Engine::SetHeading(*partner,
                                       SeekHeading(partner->GetAngleZ(), player.GetAngleZ(), dt * .24F));
                }
                // The turn is finished, so the bodies can go in while the palms are
                // still closing. Once the player body exists the follower is no
                // longer being turned, and a momentary wobble must not stall setup.
                bodiesReady = session.offset && (settled || session.player);
                const auto gap = WristGap(player, *partner, gripLeft);
                const auto limit = GripGap();
                const bool aligned =
                    session.offset &&
                    TetherAlignedGrip(distance, delta.z, gap, visible, tuning.connectDistance, lateral, side,
                                      heading, velocity.Length(), followerVelocity.Length(), limit);
                contactTime = aligned ? contactTime + dt : 0;
                gripReady = contactTime >= .15F;
                if (acquireTime > 8) {
                    Release("close approach handoff timed out; leave contact area before retry", true);
                    return;
                }
                if (traceTime >= 1) {
                    const auto* package = partner->GetCurrentPackage();
                    logger::info("[HandHolding/TETHER] Session {} gate={} root={:.1f} wrist={:.1f}/{:.1f} "
                                 "height={:.1f} LOS={} package={:08X} heading={:.2f} followerSpeed={:.1f}",
                                 token,
                                 !handoff.ready                         ? "alias cleanup"
                                 : !session.offset                      ? "offset dispatch"
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
            approach = {partner->GetFormID(), delta.Dot(right) < 0 ? -1 : 1,
                        SeekSpacing(tuning.connectDistance)};
            logger::info("[HandHolding/TETHER] {} close approach requested through WWM travel; side={}",
                         partner->GetName(), approach.side);
        }
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
        traceTime = acquireTime = contactTime = 0;
        gripReady = bodiesReady = false;
        separation = {};
        arrival.elapsed = 0;
        handoff.Start(partner->GetFormID(), token);
        session.Start();
        logger::info("[HandHolding/TETHER] Session {} requesting package handoff for {:08X}; root={:.1f}",
                     token, handoff.actor, distance);
    }
    // Approach changes only the WWM destination/arrival tolerance. Once handoff
    // starts, TETHER owns movement and the formation route is explicitly released.
    Approach GetApproach()
    {
        std::lock_guard lock(mutex);
        return state.Searching() && !session.started && !arrival.failed && cooldown <= 0 ? approach
                                                                                         : Approach{};
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
} // namespace Wayfarer::HandHolding

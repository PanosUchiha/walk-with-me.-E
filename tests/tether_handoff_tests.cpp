#include "dialogue_guard.h"
#include "hand_hold_state.h"
#include "tether_approach.h"
#include "tether_clasp.h"
#include "tether_contact.h"
#include "tether_movement.h"
#include "tether_session.h"
#include <array>
#include <iostream>
#include <limits>

int main()
{
    using namespace Wayfarer::HandHolding;
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::cerr << what << '\n';
            ++failures;
        }
    };
    Clasp::Selection clasp;
    clasp.Set(20, 42, false);
    check(clasp.Matches(20, false) && clasp.Matches(42, true),
          "right-side companion uses opposite selected hands");
    check(!clasp.Matches(43, true) && !clasp.Matches(20, true),
          "unselected actors and opposite arm keep normal animation");
    clasp.Set(20, 42, true);
    check(clasp.Matches(20, true) && clasp.Matches(42, false) && !clasp.Matches(42, true),
          "switching sides atomically replaces pose roles");
    clasp.Clear();
    check(!clasp.Matches(20, true) && !clasp.Matches(42, false),
          "release/load clears both animation conditions");
    clasp.Set(20, 20, true);
    check(!clasp.Matches(20, true), "invalid pair never takes animation ownership");
    auto finish = [&](TetherSession& s, TetherOp op, bool ok = true) {
        check(s.Begin(op), "expected operation");
        check(!s.Next(), "no concurrent operation");
        check(s.Complete(op, ok), "matching callback");
    };
    constexpr std::array setup{TetherOp::AddPlayer, TetherOp::AddFollower, TetherOp::Connect,
                               TetherOp::Offset};
    constexpr std::array teardown{TetherOp::Disconnect, TetherOp::RemoveFollower, TetherOp::RemovePlayer,
                                  TetherOp::ClearOffset};
    TetherSession s;
    s.Start();
    check(!s.Begin(TetherOp::Offset), "movement offset cannot precede the grip");
    for (auto op : setup) {
        finish(s, op);
    }
    check(s.connected && !s.Next(), "successful grip holds without repeated operations");
    s.Cancel();
    check(!s.connected, "dialogue/cancellation removes speed immediately");
    for (auto op : teardown) {
        finish(s, op);
    }
    check(s.Done(), "formation resumes only after offset removal");

    for (std::size_t at = 0; at < setup.size(); ++at) {
        s.Start();
        for (std::size_t i = 0; i < at; ++i) {
            finish(s, setup[i]);
        }
        s.Begin(setup[at]);
        s.Cancel();
        check(!s.Next(), "cancel waits for in-flight mutation");
        check(!TetherStepCurrent(s, 7, 7, static_cast<int>(setup[at])),
              "cancelled queued acquisition denied");
        s.Complete(setup[at], true);
        check(!s.connected, "late successful connect never reactivates grip");
        while (const auto op = s.Next()) {
            check(IsCleanup(*op), "cancellation only tears down acquired resources");
            finish(s, *op);
        }
        check(s.Done(), "partial acquisition rolls back completely");
    }
    for (std::size_t failed = 0; failed < setup.size(); ++failed) {
        s.Start();
        for (std::size_t i = 0; i < failed; ++i) {
            finish(s, setup[i]);
        }
        finish(s, setup[failed], false);
        while (const auto op = s.Next()) {
            finish(s, *op);
        }
        check(s.Done(), "failed acquisition restores all prior resources");
    }
    for (auto failed : teardown) {
        s.Start();
        for (auto op : setup) {
            finish(s, op);
        }
        s.Cancel();
        while (s.Next() != failed) {
            finish(s, *s.Next());
        }
        finish(s, failed, false);
        check(!s.Done() && s.Next() == failed, "failed cleanup retains ownership and retries");
        while (const auto op = s.Next()) {
            finish(s, *op);
        }
        check(s.Done(), "cleanup success unlocks formation");
    }
    s.Start();
    s.Begin(TetherOp::AddPlayer);
    check(TetherStepCurrent(s, 12, 12, 0), "current body ticket admitted");
    check(!TetherStepCurrent(s, 13, 12, 0), "old save/session cannot mutate loaded actors");
    check(!s.Complete(TetherOp::Connect, true), "out-of-order callback cannot acquire resource");
    s.Complete(TetherOp::AddPlayer, true);
    check(!s.Complete(TetherOp::AddPlayer, true), "duplicate result ignored");
    s.Cancel();
    s.Begin(TetherOp::RemovePlayer);
    check(TetherStepCurrent(s, 12, 12, 5), "teardown allowed after cancellation");
    s.Complete(TetherOp::RemovePlayer, true);
    check(s.Done(), "body-only attempt recovers without ever taking movement");

    // A gait/contact change between native dispatch and VM execution performs
    // no mutation. Preserve earlier resources instead of restarting the attempt.
    for (std::size_t at = 0; at < setup.size(); ++at) {
        s.Start();
        for (std::size_t i = 0; i < at; ++i) {
            finish(s, setup[i]);
        }
        const bool hadPlayer = s.player, hadFollower = s.follower, hadOffset = s.offset,
                   hadConstraint = s.constraint, wasConnected = s.connected;
        check(s.Begin(setup[at]), "deferred step starts normally");
        check(!s.Defer(TetherOp::Disconnect), "wrong result cannot clear a pending setup");
        check(s.Defer(setup[at]), "unexecuted setup can defer");
        check(!s.cancelled && !s.pending && s.Next() == setup[at],
              "deferred setup retries the same operation");
        check(s.player == hadPlayer && s.follower == hadFollower && s.offset == hadOffset &&
                  s.constraint == hadConstraint && s.connected == wasConnected,
              "deferral neither acquires nor discards physics resources");
        check(!s.Defer(setup[at]), "duplicate deferral cannot complete a future operation");
        check(s.Begin(setup[at]), "retry can be dispatched again");
        s.Cancel();
        check(s.Defer(setup[at]), "late no-mutation result permits cancellation cleanup");
        while (const auto op = s.Next()) {
            check(IsCleanup(*op), "deferred cancellation never restarts setup");
            check(s.Begin(*op), "cleanup starts after deferred cancellation");
            check(!s.Defer(*op), "cleanup must never silently relinquish a live resource");
            check(s.Complete(*op, true), "cleanup still receives its result");
        }
        check(s.Done(), "deferred cancelled attempt fully cleans up");
    }

    // A lost package may need another offset without giving up the original
    // cleanup obligation. Refresh callbacks use the same serial operation slot.
    auto heldSession = [&] {
        TetherSession held;
        held.Start();
        for (auto op : setup) {
            finish(held, op);
        }
        return held;
    };
    TetherSession renewal;
    check(!renewal.RefreshOffset(), "an inactive session cannot renew movement");
    renewal.Start();
    check(!renewal.RefreshOffset(), "acquisition cannot request a movement renewal");
    for (auto op : {TetherOp::AddPlayer, TetherOp::AddFollower, TetherOp::Connect}) {
        finish(renewal, op);
    }
    check(!renewal.RefreshOffset(), "connection alone cannot renew an offset never acquired");
    finish(renewal, TetherOp::Offset);
    check(renewal.RefreshOffset() && renewal.offset && renewal.Next() == TetherOp::Offset,
          "renewal retains the live offset and schedules only another offset call");
    check(!renewal.RefreshOffset(), "a queued renewal cannot be duplicated");
    check(renewal.Begin(TetherOp::Offset) && !renewal.Next() && !renewal.RefreshOffset(),
          "renewal occupies the serial callback slot");
    check(renewal.Defer(TetherOp::Offset) && renewal.offset && renewal.refreshOffset && renewal.connected &&
              renewal.Next() == TetherOp::Offset,
          "deferred renewal preserves the grip and old offset until a fresh retry");
    finish(renewal, TetherOp::Offset);
    check(renewal.offset && renewal.connected && !renewal.refreshOffset && !renewal.Next(),
          "successful renewal becomes quiet without rebuilding physics");
    check(renewal.RefreshOffset(), "a later package change can request a fresh renewal");
    finish(renewal, TetherOp::Offset, false);
    check(renewal.cancelled && renewal.offset && renewal.Next() == TetherOp::Disconnect,
          "failed renewal releases the grip while retaining offset cleanup ownership");
    for (auto op : teardown) {
        finish(renewal, op);
    }
    check(renewal.Done(), "failed renewal clears the original offset before returning movement");

    for (int result : {0, 1, 2}) {
        renewal = heldSession();
        check(renewal.RefreshOffset() && renewal.Begin(TetherOp::Offset),
              "cancellation scenario begins with an in-flight renewal");
        renewal.Cancel();
        check(!renewal.Next() && !renewal.RefreshOffset() && renewal.offset,
              "release waits for the renewal result without dropping ownership");
        check(!TetherStepCurrent(renewal, 12, 12, static_cast<int>(TetherOp::Offset)),
              "cancelled renewal cannot perform a new queued engine mutation");
        check(result == 2 ? renewal.Defer(TetherOp::Offset) : renewal.Complete(TetherOp::Offset, result == 1),
              "late success, failure and no-mutation results all unblock cleanup");
        check(!renewal.connected && renewal.offset && renewal.Next() == TetherOp::Disconnect,
              "a late renewal never revives the grip or discards the old offset");
        for (auto op : teardown) {
            finish(renewal, op);
        }
        check(renewal.Done(), "cancelled renewal drains each acquired resource exactly once");
    }
    renewal = heldSession();
    renewal.RefreshOffset();
    renewal.Begin(TetherOp::Offset);
    renewal.Defer(TetherOp::Offset);
    renewal.Cancel();
    for (auto op : teardown) {
        finish(renewal, op);
    }
    check(renewal.Done(), "release between deferred renewal attempts never restarts movement");

    constexpr float circle = 6.283185307F;
    const auto nearYaw = [&](std::optional<float> actual, float expected) {
        return actual && std::abs(std::remainder(*actual - expected, circle)) < .0001F;
    };
    check(nearYaw(TetherHeldHeading(1, 2, 1.0F / 60), 1.1F),
          "held facing advances at the bounded rate on an ordinary frame");
    check(nearYaw(TetherHeldHeading(1, 0, .5F), .7F),
          "a slow frame cannot snap a live grip through a large angle");
    check(nearYaw(TetherHeldHeading(6.25F, .04F, 1.0F / 60), .04F) &&
              nearYaw(TetherHeldHeading(.03F, 6.24F, 1.0F / 60), 6.24F),
          "held facing takes the short turn across zero in either direction");
    check(nearYaw(TetherHeldHeading(1, 1.02F, .05F), 1.02F) && nearYaw(TetherHeldHeading(1, 1, .05F), 1),
          "small and settled turns never overshoot the player's heading");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (float dt : {0.0F, -1.0F, 1.01F, nan, infinity}) {
        check(!TetherHeldHeading(0, 1, dt), "invalid elapsed time cannot rotate the follower");
    }
    check(!TetherHeldHeading(nan, 0, .05F) && !TetherHeldHeading(0, infinity, .05F),
          "invalid actor headings cannot enter a rotation update");

    using MovementAction = TetherMovementAction;
    TetherMovement motion;
    motion.Requested(0);
    check(motion.package && *motion.package == 0,
          "a null current package is recorded separately from an unrequested offset");
    check(motion.Observe(41, 0, 0, 0, .25F) == MovementAction::None &&
              motion.Observe(41, 0, 0, 0, .25F) == MovementAction::RefreshPackage,
          "a package appearing after null dispatch renews after the minimum spacing");
    motion.Requested(41);
    renewal = heldSession();
    renewal.RefreshOffset();
    renewal.Begin(TetherOp::Offset);
    // The engine changes 41 -> 42 while the VM call for 41 is pending. The
    // controller deliberately does not call Observe until that result arrives.
    check(motion.package == 41 && renewal.pending == TetherOp::Offset,
          "pending renewal keeps the dispatch package instead of adopting a later package");
    renewal.Complete(TetherOp::Offset, true);
    check(motion.Observe(42, 0, 0, 0, .5F) == MovementAction::RefreshPackage,
          "a package change during the VM wait is still detected after completion");
    motion.Requested(42);
    check(motion.Observe(0, 0, 0, 0, .5F) == MovementAction::RefreshPackage,
          "loss of a previously valid package also renews movement");
    motion.Requested(0);
    for (int frame = 0; frame < 120; ++frame) {
        check(motion.Observe(0, 50, 0, 0, .05F) == MovementAction::None,
              "unchanged null package and an idle pair do not repeatedly renew offset");
    }

    // Standing turns can move the side target without requiring locomotion.
    // Ordinary walking and small arrival errors also must not look like a freeze.
    for (const auto sample : std::array<std::array<float, 3>, 3>{{{60, 0, 0}, {60, 80, 70}, {10, 80, 0}}}) {
        motion = {};
        motion.Requested(41);
        for (int frame = 0; frame < 100; ++frame) {
            check(motion.Observe(41, sample[0], sample[1], sample[2], .05F) == MovementAction::None,
                  "idle turns, normal walking and arrival tolerance never trigger stall recovery");
        }
    }
    motion = {};
    motion.Requested(41);
    check(motion.Observe(41, 50, 80, 0, .5F) == MovementAction::None,
          "initial movement grace cannot immediately classify a follower as frozen");
    check(motion.Observe(41, 50, 80, 0, .125F) == MovementAction::RefreshStall,
          "sustained player travel with a motionless displaced follower renews once");
    motion.Requested(41);
    check(motion.Observe(41, 50, 80, 0, .5F) == MovementAction::None &&
              motion.Observe(41, 50, 80, 0, .5F) == MovementAction::None,
          "stall repair allows the renewed movement time to take effect");
    check(motion.Observe(41, 50, 80, 0, .5F) == MovementAction::Release,
          "persistent freeze releases after one renewal instead of retrying forever");
    check(motion.Observe(41, 50, 80, 20, .05F) == MovementAction::None && !motion.retriedStall,
          "observed follower movement rearms recovery for a separate later stall");
    check(motion.Observe(41, 50, 80, 0, .5F) == MovementAction::None,
          "a later stall begins a new observation window");
    motion.Pause();
    check(motion.Observe(41, 50, 80, 0, .125F) == MovementAction::None,
          "pause discards partial stalled time");
    for (const auto sample : std::array<std::array<float, 4>, 5>{{{50, 80, 0, 0},
                                                                  {50, 80, 0, 2},
                                                                  {nan, 80, 0, .05F},
                                                                  {50, infinity, 0, .05F},
                                                                  {50, 80, nan, .05F}}}) {
        check(motion.Observe(41, sample[0], sample[1], sample[2], sample[3]) == MovementAction::None &&
                  motion.stalled == 0,
              "invalid movement samples clear accumulated stall time without action");
    }

    TetherHandoff h;
    // Reproduce Rumarin's delayed alias sweep. Grip setup must neither wait for
    // its acknowledgement nor send him back to an ordinary formation target.
    s = {};
    s.Cancel();
    check(TetherRetainsApproach(s, h), "an idle cancelled state can begin a fresh approach");
    s.Start();
    for (auto op : {TetherOp::AddPlayer, TetherOp::AddFollower, TetherOp::Connect}) {
        check(TetherRetainsApproach(s, h), "the arrival target remains active throughout physics setup");
        check(!NeedsHandoff(op) && !h.ready, "grip creation does not wait for alias cleanup");
        finish(s, op);
    }
    check(s.connected && s.constraint && !s.offset && s.Next() == TetherOp::Offset,
          "hands are connected before any movement handoff");
    h.Start(42, 8);
    check(TetherRetainsApproach(s, h) && NeedsHandoff(*s.Next()),
          "connected handoff retains approach travel while offset waits for old cleanup");
    for (int frame = 0; frame < 180; ++frame) {
        check(s.connected && !h.ready && s.Next() == TetherOp::Offset,
              "a three-second alias delay does not delay or drop the existing grip");
    }
    s.Cancel();
    check(!TetherRetainsApproach(s, h), "cancellation ends approach even before handoff cleanup arrives");
    for (auto op : {TetherOp::Disconnect, TetherOp::RemoveFollower, TetherOp::RemovePlayer}) {
        finish(s, op);
    }
    check(s.Done() && !s.offset, "release before handoff drains physics without clearing an unowned offset");
    check(!h.Confirm(42, 8, true, true), "empty alias cannot bypass a script stack still clearing offset");
    check(!h.RecordCleanup(42, 7, true), "stale cleanup cannot arm new ticket");
    check(!h.RecordCleanup(43, 8, true), "reassigned actor cannot arm cleanup");
    check(!h.RecordCleanup(42, 8, false), "occupied alias cannot finish cleanup");
    check(h.RecordCleanup(42, 8, true), "script completion remembered independently of package state");
    check(!h.Confirm(42, 7, true, true), "old script pass cannot acknowledge newer handoff");
    check(!h.Confirm(43, 8, true, true), "reassigned slot cannot acknowledge another actor");
    check(!h.Confirm(42, 8, false, true), "alias must finish clearing");
    check(h.RecordCleanup(42, 8, true), "refilled alias requires fresh script completion");
    check(!h.Confirm(42, 8, true, false), "old package must finish releasing");
    check(!h.ready, "request alone does not grant movement");
    check(h.Confirm(42, 8, true, true) && h.ready, "completed slot cleanup grants movement");
    h.Start(42, 9);
    check(!h.ready && !h.cleanupComplete, "new attempt requires fresh script acknowledgement");
    h.RecordCleanup(42, 9, true);
    for (int frame = 0; frame < 90; ++frame) {
        check(!h.Confirm(42, 9, true, false) && h.cleanupComplete,
              "delayed engine transition retains script ack without granting ownership");
    }
    check(!h.Confirm(42, 9, false, true), "refilled alias blocks package completion");
    check(!h.Confirm(42, 9, true, true),
          "externally refilled and cleared alias needs a fresh script acknowledgement");
    h.RecordCleanup(42, 9, true);
    check(h.Confirm(42, 9, true, true), "later package transition completes without another Papyrus pass");

    Wayfarer::DialogueGuard dialogue;
    check(dialogue.Check(true, 1000), "dialogue suspends grip");
    check(dialogue.Check(false, 2499), "dialogue grace blocks immediate reconnection");
    check(!dialogue.Check(false, 2500), "normal approach allowed after grace");
    check(TetherNear(105, 0, true, 110), "normal WWM arrival can reach handoff threshold");
    check(!TetherGripReady(105, 0, 30, true, 90, 45), "coarse handoff cannot attach at excessive distance");
    check(TetherGripReady(65, 0, 45, true, 90, 45),
          "achievable proximity does not require r12 wrist alignment");
    check(!TetherGripReady(60, 41, 30, true, 90, 45), "vertical discontinuity blocks grip");
    check(!TetherGripReady(60, 0, 46, true, 90, 45), "excessive wrist separation blocks grip");
    check(!TetherGripReady(60, 0, 30, false, 90, 45), "obstructed grip blocked");
    check(!TetherGripReady(60, 0, 30, true, 50, 45), "configured connection limit respected");
    check(!TetherGripReady(60, 0, std::numeric_limits<float>::infinity(), true, 90, 45),
          "missing hand bone blocks physics");
    check(!TetherGripReady(60, 0, 30, true, 90, 20) && TetherGripReady(60, 0, 18, true, 90, 20),
          "the configured wrist gap decides when the constraint may be created");

    // Turning the follower into place finishes before any physics body exists.
    check(TetherHeadingSettled(.1F, 40, 1, 0), "a squared-up follower is ready for the ragdoll bodies");
    check(!TetherHeadingSettled(.3F, 40, 1, 0), "a follower still turning is not ready");
    check(!TetherHeadingSettled(.1F, 40, -1, 0), "the wrong side is never settled");
    check(!TetherHeadingSettled(.1F, 40, 1, 70), "a walking follower is not settled");
    check(!TetherHeadingSettled(std::numeric_limits<float>::quiet_NaN(), 40, 1, 0),
          "invalid heading is never settled");

    // Facing correction happens at the held arrival position before physics.
    // In particular, a pending add may already have created a body.
    TetherSession facingSession;
    facingSession.Start();
    check(CanCorrectTetherFacing(facingSession), "align without waiting for movement handoff");
    facingSession.Begin(TetherOp::AddPlayer);
    check(!CanCorrectTetherFacing(facingSession), "pending physics blocks direct rotation");
    facingSession.Complete(TetherOp::AddPlayer, true);
    check(!CanCorrectTetherFacing(facingSession), "live physics blocks direct rotation");
    facingSession = {};
    check(!CanCorrectTetherFacing(facingSession), "an idle follower is never rotated");
    facingSession.Start();
    facingSession.Cancel();
    check(!CanCorrectTetherFacing(facingSession), "cancellation cannot turn the follower");
    for (int side : {-1, 1}) {
        check(TetherFacingReady(.1F, 40.0F * side, side, 70),
              "either-side arrival can start offset once facing agrees");
        check(!TetherFacingReady(3.14F, 40.0F * side, side, 0),
              "offset cannot start a backward-facing slow spin");
        check(!TetherFacingReady(.1F, -40.0F * side, side, 0), "wrong-side facing never starts offset");
    }
    check(!TetherFacingReady(.1F, 40, 1, 140) &&
              !TetherFacingReady(std::numeric_limits<float>::quiet_NaN(), 40, 1, 0),
          "running past and invalid facing cannot start offset");

    // Each setup operation waits only on what it depends on, so the constraint is
    // the single Papyrus round trip left after the hands reach each other.
    check(NeedsHandoff(TetherOp::Offset) && !NeedsHandoff(TetherOp::Connect) &&
              !NeedsHandoff(TetherOp::AddPlayer) && !NeedsHandoff(TetherOp::AddFollower),
          "only offset movement waits for WWM's package cleanup");
    check(!NeedsHandoff(TetherOp::Disconnect) && !NeedsHandoff(TetherOp::RemovePlayer) &&
              !NeedsHandoff(TetherOp::RemoveFollower) && !NeedsHandoff(TetherOp::ClearOffset),
          "teardown is never blocked by a handoff that may never arrive");
    check(!NeedsHeading(TetherOp::Offset) && NeedsHeading(TetherOp::AddPlayer) &&
              NeedsHeading(TetherOp::AddFollower),
          "physics waits for settled facing; connected offset may start while walking");
    check(!NeedsGrip(TetherOp::AddPlayer) && !NeedsGrip(TetherOp::AddFollower) &&
              NeedsGrip(TetherOp::Connect),
          "only the constraint waits for the palms to arrive");
    check(TetherPlayerSpeed(20, 45, 100) == 1, "slack grip keeps player speed");
    check(std::abs(TetherPlayerSpeed(72.5F, 45, 100) - .775F) < .0001F, "distance interpolates speed");
    check(TetherPlayerSpeed(200, 45, 100) == .55F, "speed floor bounded");
    check(TetherPlayerSpeed(std::numeric_limits<float>::quiet_NaN(), 45, 100) == 1,
          "NaN cannot poison speed");
    TetherArrival arrival;
    for (float distance : {240.0F, 180.0F, 100.0F, 70.0F, 45.0F}) {
        check(!arrival.Observe(TetherGripReady(distance, 0, 30, true, 90, 45), distance, 140, 160, 40, -10, 0,
                               .2F),
              "normal moving catch-up never relinquishes the travel package");
    }
    check(!arrival.Observe(true, 45, 0, 180, 40, -10, 0, 1), "running past cannot trigger a handoff");
    check(!arrival.Observe(true, 45, 0, 0, 0, -45, 0, 1),
          "follower directly behind cannot drag an arm across the body");
    check(arrival.Observe(true, 45, 0, 70, 40, -10, 3.14F, .35F),
          "nearby arrival can start positioning before hands/facing align");
    check(!TetherAlignedGrip(45, 0, 30, true, 90, 40, 1, 3.14F, 0, 0, 45),
          "arrival alone cannot attach physics facing backwards");
    arrival.elapsed = 0;
    check(arrival.Observe(true, 45, 0, 0, 40, -10, 0, .016F), "adjacent arrival begins on the first frame");
    check(!arrival.Observe(false, 45, 0, 0, 40, -10, 0, .1F), "lost sight blocks acquisition");
    check(arrival.Observe(true, 45, 0, 0, 40, -10, 0, .016F), "regaining contact needs no extra dwell");
    arrival.Fail();
    for (int i = 0; i < 60; ++i) {
        check(!arrival.Observe(true, 45, 0, 0, 40, -10, 0, 1), "failed setup cannot loop package takeovers");
    }
    check(!arrival.Observe(false, 160, 0, 0, 40, -10, 0, .1F) && !arrival.failed,
          "leaving contact area rearms acquisition");
    check(arrival.Observe(true, 45, 0, 0, -40, -10, 0, .75F), "either side works after rearming");
    check(!arrival.Observe(true, 45, std::numeric_limits<float>::quiet_NaN(), 0, 40, -10, 0, 1),
          "invalid motion cannot acquire");
    check(TetherAlignedGrip(45, 0, 30, true, 90, 40, 1, .1F, 0, 0, 45),
          "aligned settled hands admit physics after positioning");
    check(!TetherAlignedGrip(45, 0, 30, true, 90, 40, -1, .1F, 0, 0, 45),
          "crossing to the other side cannot attach the wrong hands");
    check(!TetherAlignedGrip(45, 0, 30, true, 90, 40, 1, .1F, 0, 70, 45),
          "physics waits for follower locomotion to settle");
    check(!TetherAlignedGrip(45, 0, 60, true, 90, 40, 1, .1F, 0, 0, 45),
          "positioning is not proof of reachable hands");
    check(TetherAlignedGrip(45, 0, 30, true, 90, 40, 1, .1F, 0, 0, Tuning{}.gripGap),
          "the default gap admits Rumarin's reachable wrists without a widening timer");

    // Exercise the actual planner's destination and commit/repath rules with
    // the engine's stopping radius, not just isolated handholding thresholds.
    using namespace Wayfarer;
    const TravelTuning normal;
    const auto close = ApproachTuning(normal, true), unchanged = ApproachTuning(normal, false);
    check(unchanged.arrivalRadius == normal.arrivalRadius && unchanged.goalThreshold == normal.goalThreshold,
          "unselected companions keep normal tolerances");
    check(close.maxSpeedScale == normal.maxSpeedScale && close.refreshSeconds == normal.refreshSeconds &&
              close.catchUpSeconds == normal.catchUpSeconds,
          "approach keeps ordinary speed and refresh policy");
    for (int side : {-1, 1}) {
        const auto target = ApproachPoint({0, 0}, 0, side);
        check(target.x == 40 * side && target.y == -10, "destination agrees with offset handoff position");
        // Native travel is allowed to stop anywhere inside this circle.
        for (int i = 0; i < 16; ++i) {
            const float angle = i * 6.283185307F / 16;
            const Vec2 stopped{target.x + close.arrivalRadius * std::cos(angle),
                               target.y + close.arrivalRadius * std::sin(angle)};
            TetherArrival routed;
            check(
                routed.Observe(TetherNear(Length(stopped), 0, true, 90), Length(stopped), 0, 0, stopped.x,
                               stopped.y, 2, .4F),
                "native approach stopping circle fits the handoff region without requiring matching facing");
        }
    }
    TravelGoalState route;
    route.Commit({140, -100}, {140, -100}, TravelPace::kWalk);
    route.MarkRepathed();
    route.Tick({140, -100}, .4F, true, close);
    route.Tick({140, -100}, .4F, true, close);
    const auto destination = ApproachPoint({0, 0}, 0, 1);
    check(route.ShouldCommit(destination, {140, -100}, TravelPace::kWalk, close),
          "idle approach replaces distant formation goal");
    route.Commit(destination, {140, -100}, TravelPace::kWalk);
    check(route.ShouldRepath({140, -100}, 0, false, true, close),
          "committed approach requests a native route");
    route.MarkRepathed();
    check(!route.ShouldCommit(destination, {120, -80}, TravelPace::kWalk, close),
          "approach does not force a marker commit every tick");
    check(!route.ShouldRepath({120, -80}, 0, false, true, close),
          "approach does not refresh packages every tick");
    const Vec2 arrived{48, -10};
    route.Commit(destination, arrived, TravelPace::kWalk);
    route.MarkRepathed();
    check(!route.ShouldCommit(arrived, arrived, TravelPace::kWalk, close) &&
              RepathArrivalHold(route, arrived),
          "arrival stop immediately replaces even a recent target inside the ordinary change threshold");
    route.Commit(arrived, arrived, TravelPace::kWalk);
    route.MarkRepathed();
    for (int frame = 0; frame < 180; ++frame) {
        route.Tick(arrived, 1.0F / 60, true, close);
        check(!RepathArrivalHold(route, arrived) && !route.ShouldRepath(arrived, 0, false, true, close),
              "holding arrival does not repeatedly restart pathfinding during a delayed VM callback");
    }

    // The bridge can only advance a route still owned by this follower's slot.
    // Empty or reassigned aliases and foreign packages must never be reclaimed.
    struct TravelOwnership
    {
        bool requested, travelActive;
        std::uint32_t aliasActor, currentPackage;
        bool allowed;
    };
    constexpr std::uint32_t followerID = 42, slotPackage = 71;
    constexpr std::array ownershipCases{TravelOwnership{true, true, followerID, slotPackage, true},
                                        TravelOwnership{false, true, followerID, slotPackage, false},
                                        TravelOwnership{true, false, followerID, slotPackage, false},
                                        TravelOwnership{true, true, 0, slotPackage, false},
                                        TravelOwnership{true, true, 43, slotPackage, false},
                                        TravelOwnership{true, true, followerID, 0, false},
                                        TravelOwnership{true, true, followerID, 72, false},
                                        TravelOwnership{true, false, 0, 0, false}};
    for (const auto ownership : ownershipCases) {
        check(ContinueHandoffTravel(ownership.requested, ownership.travelActive,
                                    ownership.aliasActor == followerID,
                                    ownership.currentPackage == slotPackage) == ownership.allowed,
              "handoff travel requires the existing route, matching alias and matching package");
    }

    TravelGoalState bridgeRoute;
    bridgeRoute.Commit({40, -10}, {40, -10}, TravelPace::kWalk);
    bridgeRoute.MarkRepathed();
    bridgeRoute.sinceRepath = .149F;
    check(!RepathHandoffTravel(bridgeRoute, {48, -10}),
          "moving handoff target waits for the minimum repath interval");
    bridgeRoute.sinceRepath = .15F;
    check(!RepathHandoffTravel(bridgeRoute, {47.999F, -10}) && RepathHandoffTravel(bridgeRoute, {48, -10}),
          "handoff repath starts at eight units and the minimum cadence");
    check(RepathHandoffTravel(bridgeRoute, {40, -18}), "handoff measures target motion in either direction");
    bridgeRoute.recoveryTime = .01F;
    check(!RepathHandoffTravel(bridgeRoute, {140, -10}), "navigation recovery blocks handoff repathing");
    bridgeRoute.recoveryTime = 0;
    bridgeRoute.active = false;
    check(!RepathHandoffTravel(bridgeRoute, {140, -10}),
          "handoff repathing cannot reactivate a released route");
    bridgeRoute.active = true;
    bridgeRoute.hasRoute = false;
    bridgeRoute.sinceRepath = .149F;
    check(!RepathHandoffTravel(bridgeRoute, {40, -10}),
          "a missing submitted route still respects handoff cadence");
    bridgeRoute.sinceRepath = .15F;
    check(RepathHandoffTravel(bridgeRoute, {40, -10}),
          "an active route can submit its first handoff destination");
    bridgeRoute.MarkRepathed();
    for (int frame = 0; frame < 120; ++frame) {
        bridgeRoute.Tick({40, -10}, 1.0F / 60, true, close);
        check(!RepathHandoffTravel(bridgeRoute, {40, -10}),
              "a stationary connected pair never restarts its arrival route");
    }

    // Inigo's alias cleanup can take about two seconds after Connect succeeds.
    // Keep the slot's moving marker alive during that VM delay without admitting
    // Offset, which would race the script's pending ClearKeepOffsetFromActor.
    TetherSession bridgeSession;
    TetherHandoff bridgeHandoff;
    bridgeSession.Start();
    for (auto op : {TetherOp::AddPlayer, TetherOp::AddFollower, TetherOp::Connect}) {
        finish(bridgeSession, op);
    }
    ArrivalRadiusLease bridgeRadiusLease;
    std::uint32_t bridgeRadius = 65;
    bridgeRadiusLease.Update(bridgeRadius, true);
    bridgeHandoff.Start(followerID, 10);
    auto acquiredOffset = heldSession();
    check(!TetherRetainsApproach(acquiredOffset, bridgeHandoff),
          "an already acquired offset cannot retain the old approach route");
    TetherSession unconnectedBridge;
    unconnectedBridge.Start();
    check(!TetherRetainsApproach(unconnectedBridge, bridgeHandoff),
          "a handoff request without a connected grip cannot resume travel");
    bridgeRoute = {};
    bridgeRoute.Commit({40, -10}, {40, -10}, TravelPace::kWalk);
    bridgeRoute.MarkRepathed();
    bool travelActive = true;
    Vec2 marker = bridgeRoute.submittedGoal;
    int markerMoves = 0, firstMarkerMove = 0;
    for (int frame = 1; frame <= 120; ++frame) {
        const auto target = ApproachPoint({0, frame * 1.2F}, 0, 1);
        bridgeRoute.Tick(marker, 1.0F / 60, true, close);
        const bool requested = TetherRetainsApproach(bridgeSession, bridgeHandoff);
        check(ContinueHandoffTravel(requested, travelActive, true, true),
              "the connected follower retains its owned route throughout the alias delay");
        bridgeRadiusLease.Update(bridgeRadius, requested);
        check(bridgeRadius == approachRadius && bridgeRadiusLease.original == 65,
              "handoff continuation preserves the original arrival-radius lease");
        if (RepathHandoffTravel(bridgeRoute, target)) {
            check(target.y > marker.y, "the handoff marker advances with player travel");
            marker = target;
            bridgeRoute.Commit(target, marker, TravelPace::kWalk);
            bridgeRoute.MarkRepathed();
            ++markerMoves;
            if (!firstMarkerMove) {
                firstMarkerMove = frame;
            }
        }
        check(bridgeSession.connected && bridgeSession.constraint && bridgeSession.player &&
                  bridgeSession.follower && !bridgeSession.offset && !bridgeSession.pending,
              "native travel during handoff preserves connected physics without offset mutation");
        check(bridgeSession.Next() == TetherOp::Offset && NeedsHandoff(*bridgeSession.Next()) &&
                  !bridgeHandoff.ready,
              "every pre-acknowledgement frame leaves Offset blocked by the handoff gate");
    }
    check(firstMarkerMove > 0 && firstMarkerMove <= 10 && markerMoves >= 12 && marker.y >= 122,
          "the two-second handoff begins following within one cadence and continues advancing");

    const Vec2 releasedMarker = marker;
    check(TetherRetainsApproach(bridgeSession, bridgeHandoff) &&
              !ContinueHandoffTravel(true, travelActive, false, true),
          "alias clearing stops the old route even before cleanup acknowledgement");
    bridgeRoute.Release();
    travelActive = false;
    bridgeRadiusLease.Update(bridgeRadius, false);
    check(bridgeRadius == 65 && !bridgeRadiusLease.original,
          "releasing the handoff route restores its pre-approach arrival radius");
    for (int frame = 0; frame < 12; ++frame) {
        bridgeRoute.Tick(marker, 1.0F / 60, false, close);
        check(!ContinueHandoffTravel(true, travelActive, false, true) &&
                  !ContinueHandoffTravel(true, travelActive, true, true) &&
                  !RepathHandoffTravel(bridgeRoute, {40, 200}),
              "an empty or later refilled alias cannot reclaim a released handoff route");
        check(marker.x == releasedMarker.x && marker.y == releasedMarker.y && !bridgeSession.offset,
              "the released marker stays fixed while the VM finishes old offset cleanup");
    }
    check(!bridgeHandoff.Confirm(followerID, 10, true, true),
          "alias and package release alone cannot bypass the old offset cleanup callback");
    check(bridgeHandoff.RecordCleanup(followerID, 10, true) &&
              !TetherRetainsApproach(bridgeSession, bridgeHandoff),
          "cleanup acknowledgement ends approach retention before offset takes ownership");
    check(!bridgeHandoff.Confirm(followerID, 10, true, false) && !bridgeHandoff.ready &&
              bridgeSession.connected && !bridgeSession.offset,
          "a delayed package release keeps offset blocked after script cleanup");
    check(bridgeHandoff.Confirm(followerID, 10, true, true),
          "completed alias cleanup and package release admit the first offset");
    check(bridgeSession.Begin(TetherOp::Offset) && !bridgeSession.Next() &&
              !bridgeSession.Begin(TetherOp::Offset),
          "post-handoff Offset still uses exactly one serial callback slot");
    check(bridgeSession.Complete(TetherOp::Offset, true) && bridgeSession.offset && bridgeSession.connected &&
              !bridgeSession.Next() && !TetherRetainsApproach(bridgeSession, bridgeHandoff),
          "completed handoff settles into normal connected offset movement");

    TetherSession cancelledBridge;
    TetherHandoff cancelledHandoff;
    cancelledBridge.Start();
    for (auto op : {TetherOp::AddPlayer, TetherOp::AddFollower, TetherOp::Connect}) {
        finish(cancelledBridge, op);
    }
    cancelledHandoff.Start(followerID, 11);
    cancelledBridge.Cancel();
    check(!TetherRetainsApproach(cancelledBridge, cancelledHandoff) &&
              !ContinueHandoffTravel(TetherRetainsApproach(cancelledBridge, cancelledHandoff), true, true,
                                     true),
          "cancellation stops handoff travel even while the old slot still owns the actor");
    check(cancelledHandoff.RecordCleanup(followerID, 11, true) &&
              cancelledHandoff.Confirm(followerID, 11, true, true) &&
              cancelledBridge.Next() == TetherOp::Disconnect && !cancelledBridge.Begin(TetherOp::Offset),
          "late cleanup acknowledgement cannot restart a cancelled handoff");
    for (auto op : {TetherOp::Disconnect, TetherOp::RemoveFollower, TetherOp::RemovePlayer}) {
        finish(cancelledBridge, op);
    }
    check(cancelledBridge.Done(), "cancelled handoff drains physics without taking an offset");

    ArrivalRadiusLease radiusLease;
    std::uint32_t radius = 65;
    check(radiusLease.Update(radius, true) && radius == 12,
          "selected slot tightens its engine arrival radius");
    radiusLease.Update(radius, true);
    radiusLease.Update(radius, false);
    check(radius == 65 && !radiusLease.original,
          "repeated approach updates preserve the original radius for cleanup");
    radius = 9;
    radiusLease.Update(radius, true);
    radiusLease.Update(radius, false);
    check(radius == 9, "preexisting smaller radius survives the lease");
    radius = 65;
    radiusLease.Update(radius, true);
    radius = 27;
    radiusLease.Update(radius, false);
    check(radius == 27, "cleanup does not overwrite another runtime change");
    check(TetherApproachSide(-30, 1) == -1 && TetherApproachSide(30, -1) == 1,
          "player can step beside either side");
    check(TetherApproachSide(2, -1) == -1, "nearly centered follower does not oscillate between sides");
    check(TetherFollowerSpeed(200, false) == 1, "unattached follower keeps ordinary speed");
    check(TetherFollowerSpeed(100, true) == 1 && TetherFollowerSpeed(45, true) == 1,
          "grip cannot inject an overshooting speed boost");
    TetherSeparation gap;
    check(!gap.Release(130, 120, .1F), "one lag frame does not break the grip");
    check(!gap.Release(100, 120, .1F) && gap.elapsed == 0, "returning to range resets grace");
    check(gap.Release(170, 120, .01F), "hard overextension releases immediately");
    gap = {};
    check(!gap.Release(130, 120, .2F) && gap.Release(130, 120, .21F), "sustained separation still releases");
    const auto palm =
        PalmCenter({10, 20, 30}, std::array<IK::Point, 3>{{{9, 30, 30}, {10, 30, 30}, {11, 30, 30}}});
    check(palm && std::abs(palm->y - 25.5F) < .001F, "palm uses actor's wrist and finger bases");
    const std::array<IK::Point, 3> axes{{{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}}};
    const auto local = BodyLocalContact({10, 25.5F, 30}, {10, 20, 30}, axes);
    check(local && std::abs(local->x - 5.5F) < .001F && std::abs(local->y) < .001F,
          "palm transforms into rotated rigid-body coordinates");
    check(!BodyLocalContact({100, 0, 0}, {0, 0, 0}, axes),
          "unreasonable pivot cannot pull an arm across the body");
    check(!PalmCenter({0, 0, 0}, std::array<IK::Point, 3>{{{0, 0, 0}, {0, 8, 0}, {1, 8, 0}}}),
          "invalid finger geometry refuses calibration");
    return failures ? 1 : 0;
}

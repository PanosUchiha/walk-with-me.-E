#include "dialogue_guard.h"
#include "tether_approach.h"
#include "tether_clasp.h"
#include "tether_contact.h"
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
    constexpr std::array setup{TetherOp::Offset, TetherOp::AddPlayer, TetherOp::AddFollower,
                               TetherOp::Connect};
    constexpr std::array teardown{TetherOp::Disconnect, TetherOp::RemoveFollower, TetherOp::RemovePlayer,
                                  TetherOp::ClearOffset};
    TetherSession s;
    s.Start();
    check(!s.Begin(TetherOp::AddPlayer), "physics cannot precede movement offset");
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
    s.Begin(TetherOp::Offset);
    check(TetherStepCurrent(s, 12, 12, 6), "current offset ticket admitted");
    check(!TetherStepCurrent(s, 13, 12, 6), "old save/session cannot mutate loaded actors");
    check(!s.Complete(TetherOp::Connect, true), "out-of-order callback cannot acquire resource");
    s.Complete(TetherOp::Offset, true);
    check(!s.Complete(TetherOp::Offset, true), "duplicate result ignored");
    s.Cancel();
    s.Begin(TetherOp::ClearOffset);
    check(TetherStepCurrent(s, 12, 12, 7), "teardown allowed after cancellation");
    s.Complete(TetherOp::ClearOffset, true);
    check(s.Done(), "offset-only attempt recovers");

    TetherHandoff h;
    h.Start(42, 8);
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

    // The constraint snaps the palms together when it is created, so acquisition
    // holds out for a close gap and only widens it if the pair cannot get there.
    check(TetherGripGap(0, 20, 45, 3) == 20, "a fresh approach demands the accurate gap");
    check(TetherGripGap(1.5F, 20, 45, 3) == 32.5F, "the requirement widens steadily while unreachable");
    check(TetherGripGap(3, 20, 45, 3) == 45 && TetherGripGap(30, 20, 45, 3) == 45,
          "widening stops at the reachable limit rather than growing without bound");
    check(TetherGripGap(std::numeric_limits<float>::quiet_NaN(), 20, 45, 3) == 20 &&
              TetherGripGap(1, 20, 10, 3) == 20 && TetherGripGap(1, 20, 45, 0) == 20,
          "invalid relaxation falls back to the accurate gap, never a wider one");

    // Turning the follower into place finishes before any physics body exists.
    check(TetherHeadingSettled(.1F, 40, 1, 0), "a squared-up follower is ready for the ragdoll bodies");
    check(!TetherHeadingSettled(.3F, 40, 1, 0), "a follower still turning is not ready");
    check(!TetherHeadingSettled(.1F, 40, -1, 0), "the wrong side is never settled");
    check(!TetherHeadingSettled(.1F, 40, 1, 70), "a walking follower is not settled");
    check(!TetherHeadingSettled(std::numeric_limits<float>::quiet_NaN(), 40, 1, 0),
          "invalid heading is never settled");

    // Each setup operation waits only on what it depends on, so the constraint is
    // the single Papyrus round trip left after the hands reach each other.
    check(NeedsHandoff(TetherOp::Offset) && NeedsHandoff(TetherOp::Connect) &&
              NeedsHandoff(TetherOp::AddPlayer),
          "nothing is dispatched over WWM's own package");
    check(!NeedsHandoff(TetherOp::Disconnect) && !NeedsHandoff(TetherOp::RemovePlayer) &&
              !NeedsHandoff(TetherOp::RemoveFollower) && !NeedsHandoff(TetherOp::ClearOffset),
          "teardown is never blocked by a handoff that may never arrive");
    check(!NeedsHeading(TetherOp::Offset) && NeedsHeading(TetherOp::AddPlayer) &&
              NeedsHeading(TetherOp::AddFollower),
          "the offset starts the turn that the ragdoll bodies wait for");
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
    check(!arrival.Observe(true, 45, 0, 0, 40, -10, 0, .12F), "momentary proximity does not seize movement");
    check(!arrival.Observe(false, 45, 0, 0, 40, -10, 0, .1F), "lost sight interrupts continuous settling");
    check(!arrival.Observe(true, 45, 0, 0, 40, -10, 0, .12F) &&
              arrival.Observe(true, 45, 0, 0, 40, -10, 0, .1F),
          "adjacent arrival may hand off after continuous dwell");
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
    check(!TetherAlignedGrip(45, 0, 30, true, 90, 40, 1, .1F, 0, 0, TetherGripGap(0, 20, 45, 3)) &&
              TetherAlignedGrip(45, 0, 30, true, 90, 40, 1, .1F, 0, 0, TetherGripGap(3, 20, 45, 3)),
          "a grip that cannot be made accurately is still made eventually");

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

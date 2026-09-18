#include "hand_hold_approach.h"
#include "hand_hold_attachment.h"
#include "hand_hold_ik.h"
#include "hand_hold_locomotion.h"
#include "hand_hold_overrides.h"
#include "hand_hold_pose.h"
#include "hand_hold_state.h"
#include "travel_planner.h"
#include <iostream>
#include <limits>

int main()
{
    using namespace Wayfarer::HandHolding;
    int failed{};
    auto check = [&](bool ok, const char* why) {
        if (!ok) {
            ++failed;
            std::cerr << why << '\n';
        }
    };
    check(Tuning{}.delay == 1.0F, "the shipped lead-in is one second; changing it is a deliberate act");
    Tuning t;
    t.enabled = true;
    t.partner = "follower.esp|001234";
    // The lead-in mechanism is exercised at a fixed two seconds so these timings
    // do not silently re-tune themselves when the shipped default changes.
    t.delay = 2.0F;
    State s;
    auto tick = [&](float time, bool allowed = true, bool paused = false) {
        while (time > .00001F) {
            const float dt = std::min(.05F, time);
            s.Tick(dt, allowed, paused, t);
            time -= dt;
        }
    };
    tick(1.5F);
    check(!s.Searching() && !s.Connect(1, 70, true, t), "no lookup or connection before lead-in");
    tick(20, true, true);
    check(!s.Searching(), "menu time cannot consume lead-in");
    tick(.55F);
    check(s.Searching(), "search starts after two gameplay seconds");
    check(!s.Connect(1, 91, true, t) && !s.Connect(1, 70, false, t),
          "acquisition requires distance AND reachable arms");
    check(s.Connect(1, 70, true, t), "eligible selected actor connects");
    tick(.7F);
    check(s.Weight() == 1, "reach blends fully in");
    check(!s.Connect(2, 60, true, t) && s.Partner() == 1,
          "a nearer group member cannot replace a holding partner");
    check(s.Maintain(1, 105, true, t), "grip survives gap between connection and release thresholds");
    check(!s.Maintain(1, 121, true, t), "separation releases");
    const float previous = s.Weight();
    tick(.15F);
    check(s.Weight() > 0 && s.Weight() < previous, "normal separation blends out");
    tick(.25F);
    tick(1.7F);
    check(!s.Searching(), "release restarts lead-in");
    tick(.4F);
    check(s.Searching(), "can reacquire after cooldown");
    s.Connect(1, 70, true, t);
    tick(.7F);
    t.partner = "other.esp|000abc";
    tick(.1F);
    check(s.Partner() == 0 && !s.Searching() && s.Weight() == 0,
          "selection change clears old grip and waits anew");
    tick(2);
    s.Connect(2, 70, true, t);
    tick(.7F);
    tick(.05F, false);
    check(s.Partner() == 0 && s.Weight() == 0, "unsafe state immediately releases");
    tick(1);
    t.enabled = false;
    tick(2);
    check(!s.Searching(), "disabled feature cannot search");
    t.enabled = true;
    t.partner.clear();
    tick(4);
    check(!s.Searching(), "None never falls back to nearest follower");
    t.partner = "follower.esp|001234";
    tick(1);
    s.Tick(std::numeric_limits<float>::quiet_NaN(), true, false, t);
    s.Tick(60, true, false, t);
    check(!s.Searching(), "invalid time does not bypass delay");

    Tuning leaseTuning;
    leaseTuning.enabled = true;
    leaseTuning.partner = "hlioremi.esp|033226";
    State leaseState;
    constexpr std::uint32_t remiel = 0x5C033226, other = 0x250AB6EC;
    leaseState.Tick(1, true, false, leaseTuning);
    check(!TravelRequested(false, true, leaseTuning, remiel, 0),
          "lead-in cannot acquire a stationary follower alias early");
    leaseState.Tick(1, true, false, leaseTuning);
    check(leaseState.Searching(), "stationary playtest reaches search after lead-in");
    check(TravelRequested(false, true, leaseTuning, remiel, remiel),
          "idle hand approach is published to the Papyrus package bridge");
    check(!TravelRequested(false, true, leaseTuning, other, remiel),
          "idle hand approach cannot acquire other party aliases");
    leaseState.Connect(remiel, 40, true, leaseTuning);
    check(TravelRequested(false, true, leaseTuning, leaseState.Partner(), remiel),
          "stopping after contact retains the selected partner's package");
    check(!TravelRequested(false, true, leaseTuning, remiel, 0),
          "release removes the stationary travel exception");
    check(!TravelRequested(false, false, leaseTuning, remiel, remiel),
          "changing away from Companion rejects even a stale approach");
    leaseTuning.enabled = false;
    check(!TravelRequested(false, true, leaseTuning, remiel, remiel),
          "disabling hand holding stops idle package ownership");
    leaseTuning.enabled = true;
    leaseTuning.partner.clear();
    check(!TravelRequested(false, true, leaseTuning, remiel, remiel), "None rejects a stale approach");
    check(!TravelRequested(false, true, t, 0, 0), "empty actor and approach IDs never request a package");
    check(TravelRequested(true, false, leaseTuning, other, 0),
          "ordinary moving followers retain their existing travel behavior");

    using namespace IK;
    struct Translation
    {
        float x{};
        Translation operator*(Translation child) const { return {x + child.x}; }
    };
    struct CachedBone
    {
        Translation local, world;
        int parentIndex;
        bool owned;
    };
    std::array<CachedBone, 5> skin{{{{0}, {0}, -1, false},
                                    {{2}, {2}, 0, true},
                                    {{3}, {5}, 1, false},
                                    {{4}, {9}, 2, false},
                                    {{10}, {10}, 0, false}}};
    for (int frame = 0; frame < 240; ++frame) {

        skin[1].world = {2};
        skin[2].world = {5};
        skin[3].world = {9};
        const float grip = 20 + std::sin(frame * .1F);
        SynchronizePoseCache(skin.data(), static_cast<std::uint32_t>(skin.size()), [&](CachedBone& bone) {
            if (!bone.owned) {
                return false;
            }
            bone.local = {grip};
            bone.world = {grip};
            return true;
        });
        check(std::abs(skin[1].world.x - grip) < .001F && std::abs(skin[3].world.x - grip - 7) < .001F,
              "each late pose pass replaces stale cached hand and descendant transforms without a one-frame "
              "gap");
        check(skin[4].world.x == 10, "paired arm refresh leaves unrelated skeleton branches untouched");
    }
    skin[1].world = {2};
    skin[1].local = {2};
    SynchronizePoseCache(skin.data(), static_cast<std::uint32_t>(skin.size()),
                         [](CachedBone& bone) { return bone.owned; });
    check(skin[3].world.x == 9, "release propagates restored arm transforms to cached descendants");
    skin[2].parentIndex = 4;
    skin[2].world = {55};
    SynchronizePoseCache(skin.data(), static_cast<std::uint32_t>(skin.size()),
                         [](CachedBone& bone) { return bone.owned; });
    check(skin[2].world.x == 55, "forward parent indices cannot read unprocessed cache entries");
    CollisionOwnership collision;
    collision.Acquire(false);
    collision.Acquire(true);
    check(collision.Release(),
          "restore character collision introduced by this approach, even after repeated frames");
    check(!collision.Release(), "collision restoration is idempotent");
    collision.Acquire(true);
    check(!collision.Release(), "preserve a character collision override that existed before hand holding");
    collision.Acquire(false);
    check(collision.Release(), "a replacement controller receives an independent collision lease");
    for (float speed : {0.0F, 100.0F, 245.0F, 335.0F, 450.0F}) {
        check(PoseSpeed(speed) == speed,
              "animation input follows attached motion through stop, walk, jog and run");
    }
    check(PoseSpeed(std::numeric_limits<float>::quiet_NaN()) == 0,
          "nonfinite speed cannot enter the animation graph");

    int oldSteeringDivergences{};
    for (float fps : {30.0F, 60.0F, 144.0F}) {
        for (int side : {-1, 1}) {
            Attachment pair;
            Point pc{};
            pair.Capture(pc, {40.0F * side, 0, 0}, 0);
            for (int frame = 0; frame < static_cast<int>(fps * 6); ++frame) {
                const float time = frame / fps;
                const float yaw = time < 2 ? 0.0F : time < 4 ? 3.14159265F : 4.71238898F;
                const float speed = time < 1 ? 110.0F : time < 3 ? 0.0F : 110.0F;
                pc = pc + Point{std::sin(yaw) * speed / fps, std::cos(yaw) * speed / fps, 0};
                const auto target = pair.Advance(1 / fps, pc, yaw, false);
                check(target.has_value(), "paired route direction replay keeps its valid attachment");
                if (!target) {
                    break;
                }
                auto movement = pair.Velocity();

                movement.x += 17.0F * std::cos(pair.Yaw());
                movement.y -= 17.0F * std::sin(pair.Yaw());
                const auto expected = Wayfarer::ForwardFromYaw(pair.Yaw());
                const auto heading = LocomotionDirection(true, pair.Yaw(), {movement.x, movement.y}, {-1, 0});
                check(heading.x * expected.x + heading.y * expected.y > .9999F,
                      "attached AI route faces the pair during straight walking, stops, reversals and "
                      "sideways corrections");
                const auto legacy = Wayfarer::Normalize({movement.x, movement.y});
                if (legacy.x * expected.x + legacy.y * expected.y < .5F) {
                    ++oldSteeringDivergences;
                }
            }
        }
    }
    check(oldSteeringDivergences > 0,
          "direction replay reproduces the old sideways/backward steering request");
    const auto approaching = LocomotionDirection(false, 0, {100, 0}, {0, 1});
    check(approaching.x == 1 && approaching.y == 0, "unattached approach still turns toward its destination");
    const auto idleDirection = LocomotionDirection(false, 0, {0, 0}, {0, -1});
    check(idleDirection.y == -1, "idle approach retains its native fallback heading");
    for (int side : {-1, 1}) {
        check(ReadyAtHand(40.0F * side, 0, side, 40), "contact begins in the close side-by-side position");
        check(!ReadyAtHand(73.0F * side, 47, side, 40),
              "the logged 87-unit gap cannot become a persistent attachment offset");
        check(!ReadyAtHand(40.0F * side, 40, side, 40),
              "close lateral spacing cannot capture a trailing follower");
        check(!ReadyAtHand(-40.0F * side, 0, side, 40), "contact cannot jump to the opposite hand");
    }

    for (float speed : {106.0F, 280.0F, 450.0F}) {
        Wayfarer::TravelGoalState route;
        Wayfarer::TravelTuning travel;
        Wayfarer::Vec2 actor{};
        int refreshes{};
        for (int i = 0; i < 180; ++i) {
            actor.y += speed / 6;
            route.Tick(actor, 1.0F / 6, true, travel);
            const auto pace = Wayfarer::ChoosePace(speed, 0, route.pace, true);
            route.Commit({actor.x, actor.y + LocomotionLead(speed)}, actor, pace);
            if (RepathAttached(route, actor, speed, true, travel)) {
                route.MarkRepathed();
                ++refreshes;
            }
        }
        check(refreshes < 25,
              "steady attached movement refreshes less than once per second, not every .3 seconds");
        route.sinceRepath = .4F;
        route.Commit(actor, actor, route.pace);
        check(RepathAttached(route, actor, 0, true, travel),
              "stopping cancels the old forward route promptly");
        route.MarkRepathed();
        route.sinceRepath = .4F;
        route.Commit({actor.x, actor.y + LocomotionLead(speed)}, actor, route.pace);
        check(RepathAttached(route, actor, speed, true, travel),
              "starting resumes the route even in the same gait");
    }
    for (float bob : {-12.0F, -6.0F, 0.0F, 6.0F, 12.0F}) {
        for (float side : {-1.0F, 1.0F}) {
            const Point shoulderA{0, 0, 100}, shoulderB{side * 52, 5, 100 + bob};
            const auto common = SharedTarget(shoulderA, 38, shoulderB, 43, {side * 26, 4, 65});
            check(common && Length(*common - shoulderA) <= 38.001F && Length(*common - shoulderB) <= 43.001F,
                  "shared grip adapts to unequal arms and walking shoulder bob without overreach");
        }
    }
    check(!SharedTarget({0, 0, 0}, 30, {100, 0, 0}, 30, {50, 0, -20}),
          "truly separated arms still cannot hold");
    auto common = SharedTarget({}, 40, {}, 30, {0, 0, -50});
    check(common && std::abs(Length(*common) - 30) < .001F,
          "coincident reach spheres choose the shorter arm");
    using Wayfarer::GroundKey;
    using Wayfarer::GroundPoint;
    using Wayfarer::GroundTriangle;
    using Wayfarer::TraceGroundStep;

    GroundTriangle ground{{{{-100000, -100000, 0}, {100000, -100000, 0}, {0, 100000, 0}}}, {}};
    auto floor = [&](GroundKey id) -> std::optional<GroundTriangle> {
        return id == 0 ? std::optional{ground} : std::nullopt;
    };
    for (float fps : {30.0F, 60.0F, 144.0F}) {
        for (float speed : {0.0F, .1F, 15.0F, 110.0F, 280.0F, 450.0F}) {
            Attachment grip;
            Point player{12000, -21000, 12}, follower = player + Point{60, 0, 0};
            grip.Capture(player, follower, 0);
            for (int i = 0; i < 180; ++i) {
                player.y += speed / fps;
                auto desired = grip.Advance(1 / fps, player, 0, false);
                check(std::abs(grip.Velocity().y - speed) < 1.0F,
                      "attachment velocity follows world movement, including sub-unit steps");
                auto grounded = desired ? TraceGroundStep({follower.x, follower.y, follower.z},
                                                          {desired->x, desired->y, desired->z}, 0, floor)
                                        : std::nullopt;
                check(grounded.has_value(),
                      "stationary, sub-unit, walking and running attachment corrections stay grounded");
                if (grounded) {
                    follower = {grounded->x, grounded->y, grounded->z};
                    check(std::abs(follower.z - 12) < .01F,
                          "attachment preserves controller clearance instead of snapping down to navmesh");
                    check(Length(follower - player - Point{60, 0, 0}) < .01F,
                          "grounded attachment has no separation drift");
                }
            }
        }
    }
    for (float fps : {30.0F, 60.0F, 144.0F}) {
        for (float side : {-1.0F, 1.0F}) {
            for (float speed : {0.0F, 110.0F, 280.0F, 450.0F}) {
                for (float nativeFraction : {0.0F, 1.0F}) {
                    Point player{100, 100, 0}, follower = player + Point{40 * side, -65, 0};
                    const Point playerVelocity{0, speed, 0};
                    Point previousVelocity = playerVelocity;
                    SeekMotion seek;
                    const float dt = 1 / fps;
                    for (int i = 0; i < 6 * fps; ++i) {
                        player = player + playerVelocity * dt;
                        follower = follower + previousVelocity * (dt * nativeFraction);
                        const auto before = follower,
                                   target = SeekPoint(player, 0, static_cast<int>(side), 40);
                        const auto step = seek.Advance(follower, player, target, playerVelocity,
                                                       previousVelocity, 0, 0, dt, i >= 3);
                        check(step.has_value(),
                              "walking approach closes the native arrival gap at every gait and frame rate");
                        if (!step) {
                            break;
                        }
                        check(Length(seek.Velocity() - previousVelocity) <= 420 * dt + .02F,
                              "approach accelerates and brakes without a speed jump");
                        if (!seek.Integrating()) {
                            check(Length(*step - before) < .001F,
                                  "no position correction before animation has started");
                        }
                        check(Length(*step - before) <= 24.01F,
                              "approach cannot drag a follower through a large residual error");
                        follower = *step;
                        previousVelocity = seek.Velocity();
                    }
                    check(Length(follower - SeekPoint(player, 0, static_cast<int>(side), 40)) < 1,
                          "arrival converges without adding native movement twice");
                    check(seek.Settled(playerVelocity, 0),
                          "arrival matches player speed and heading before attaching");
                    Attachment hold;
                    hold.Capture(player, follower, 0, playerVelocity);
                    check(Length(hold.Velocity() - playerVelocity) < .001F,
                          "contact preserves walking animation speed on its first frame");
                }
            }
        }
    }

    SeekMotion unanimated;
    for (int i = 0; i < 180; ++i) {
        const auto step = unanimated.Advance({40, -65, 0}, {}, {40, 0, 0}, {}, {}, 0, 0, 1.0F / 60, false);
        check(step && Length(*step - Point{40, -65, 0}) < .001F && !unanimated.Settled({}, 0),
              "unanimated follower stays under native movement control");
    }
    SeekGait noActivity;
    for (int i = 0; i < 180; ++i) {
        check(!noActivity.Observe(1.0F / 60, true, 100, 100, 0),
              "reading our own Speed write is not evidence of native gait activity");
    }
    check(!noActivity.Observe(1.0F / 60, true, 0, 100, 0), "an idle graph does not activate forced movement");
    check(!noActivity.Observe(.5F, true, 130, 100, 10), "a stalled frame does not count as gait evidence");
    for (int side : {-1, 1}) {
        SeekMotion idleMotion;
        State idleState;
        Tuning idleTuning;
        idleTuning.enabled = true;
        idleTuning.partner = "selected.esp|123";
        idleTuning.delay = .5F;
        idleState.Tick(.5F, true, false, idleTuning);
        const Point pc{}, npc{40.0F * side, 0, 0};
        const auto step =
            idleMotion.Advance(npc, pc, SeekPoint(pc, 0, side, 40), {}, {}, 0, 0, 1.0F / 60, false);
        check(step && Length(*step - npc) == 0 && !idleMotion.Integrating(),
              "idle contact does not fake gait activity or move the body");
        const bool contactReady = ReadyAtHand(npc.x, npc.y, side, 40) &&
                                  (idleMotion.Settled({}, 0) || IdleContactReady(0, 0, 0, 0));
        check(contactReady && idleState.Connect(123, Length(npc), true, idleTuning),
              "already aligned idle pair can hold hands without starting a walk");
        check(!ReadyAtHand(40.0F * side, -65, side, 40),
              "stationary contact never skips the remaining approach distance");
    }
    check(!IdleContactReady(110, 0, 0, 0) && !IdleContactReady(0, 110, 0, 0) &&
              !IdleContactReady(0, 0, 110, 0),
          "moving participants still require a settled animated approach");
    check(!IdleContactReady(0, 0, 0, 3.14159265F), "idle contact cannot attach a facing-away follower");
    check(!IdleContactReady(std::numeric_limits<float>::quiet_NaN(), 0, 0, 0),
          "invalid movement data cannot permit idle contact");

    for (float fps : {30.0F, 60.0F, 144.0F}) {
        for (float speed : {0.0F, 110.0F}) {
            for (bool nativeOnly : {false, true}) {
                for (int side : {-1, 1}) {
                    SeekGait gait;
                    SeekMotion motion;
                    State contact;
                    Tuning config;
                    config.enabled = true;
                    config.partner = "selected.esp|123";
                    config.delay = .5F;
                    contact.Tick(.5F, true, false, config);
                    Point pc{}, npc{side * 40.0F, -65, 0}, lastVelocity{0, speed, 0};
                    const Point pcVelocity{0, speed, 0};
                    const float dt = 1 / fps;
                    float writtenSpeed{};
                    bool written = false, connected = false;
                    for (int i = 0; i < 6 * fps; ++i) {
                        pc = pc + pcVelocity * dt;
                        const auto nativeDelta = nativeOnly ? lastVelocity * dt : Point{};
                        npc = npc + nativeDelta;
                        const bool ready = gait.Observe(dt, written, nativeOnly ? writtenSpeed : 130.0F,
                                                        writtenSpeed, Length(nativeDelta));
                        const auto step = motion.Advance(npc, pc, SeekPoint(pc, 0, side, 40), pcVelocity,
                                                         lastVelocity, 0, 0, dt, ready);
                        check(step.has_value(),
                              "zero Character callbacks no longer trap a live walking approach");
                        if (!step) {
                            break;
                        }
                        npc = *step;
                        lastVelocity = motion.Velocity();
                        writtenSpeed = Length(lastVelocity);
                        written = true;
                        const auto offset = npc - pc;
                        if (ReadyAtHand(offset.x, offset.y, side, 40) && motion.Settled(pcVelocity, 0)) {
                            connected = contact.Connect(123, Length(offset), true, config);
                            break;
                        }
                    }
                    check(connected && contact.GetPhase() == Phase::Holding,
                          "observed gait reaches Holding with no NPC callback dependency");
                    check(gait.Observe(dt, true, writtenSpeed, writtenSpeed, 0),
                          "readiness stays latched through equal-value graph updates");
                    gait.Reset();
                    check(!gait.Ready(), "release requires fresh gait evidence for the next approach");
                }
            }
        }
    }
    SeekMotion sideApproach;
    Point sideFollower{100, 0, 0};
    for (int i = 0; i < 360; ++i) {
        const auto step = sideApproach.Advance(sideFollower, {}, {40, 0, 0}, {}, {}, 0, 0, 1.0F / 60, i >= 8);
        if (step) {
            sideFollower = *step;
        }
        if (i == 30) {
            check(sideApproach.Heading() < -.5F,
                  "side approach faces travel instead of sliding sideways while facing the player heading");
        }
    }
    check(sideApproach.Settled({}, 0), "side approach eases back to player heading before contact");
    SeekMotion stoppedApproach;
    Point stopPlayer{}, stopFollower{40, -65, 0}, lastApproachVelocity{0, 110, 0};
    for (int i = 0; i < 420; ++i) {
        const Point speed{0, i < 30 ? 110.0F : 0.0F, 0};
        stopPlayer = stopPlayer + speed * (1.0F / 60);
        const auto step = stoppedApproach.Advance(stopFollower, stopPlayer, stopPlayer + Point{40, 0, 0},
                                                  speed, lastApproachVelocity, 0, 0, 1.0F / 60, i >= 8);
        check(step.has_value(), "player stopping during approach does not reset or snap the follower");
        if (!step) {
            break;
        }
        check(Length(stoppedApproach.Velocity() - lastApproachVelocity) <= 7.01F,
              "stopping player still brakes the follower gradually");
        stopFollower = *step;
        lastApproachVelocity = stoppedApproach.Velocity();
    }
    check(stoppedApproach.Settled({}, 0) && Length(stopFollower - stopPlayer - Point{40, 0, 0}) < 1,
          "approach settles at the hand after the PC stops");
    stoppedApproach.Reset();
    check(!stoppedApproach.Active() && !stoppedApproach.Integrating() &&
              Length(stoppedApproach.Velocity()) == 0,
          "interruption clears approach motion and startup state");
    for (float spacing : {.6F, 1.0F, 2.0F, 3.0F}) {
        const Point player{0, 0, 0}, farFollower{250 * spacing, -300 * spacing, 0};
        const auto target = SeekPoint(player, 0, 1, SeekSpacing(90));
        check(Length(target - player) == 40, "hand goal stays close with maximum general spacing");
        SeekMotion seek;
        check(!seek.Advance(farFollower, player, target, {0, 280, 0}, {}, 0, 0, 1.0F / 60, true),
              "distant follower must use native pathfinding, not warp to hand");
    }
    SeekMotion invalidSeek;
    check(!invalidSeek.Advance({100, 0, 0}, {}, {60, 0, 0}, {0, 100, 0}, {}, 0, 0, .5F, true),
          "approach rejects long-frame warps");
    check(!invalidSeek.Advance({100, 0, 80}, {}, {60, 0, 0}, {0, 100, 0}, {}, 0, 0, .016F, true),
          "approach never pulls a follower off another floor");
    float heading = 3.14159265F;
    for (int i = 0; i < 31; ++i) {
        heading = SeekHeading(heading, 0, 1.0F / 60);
    }
    check(std::abs(heading) < .001F, "last-step heading aligns for a reachable hand");

    for (float fps : {30.0F, 60.0F, 144.0F}) {
        for (float side : {-1.0F, 1.0F}) {
            Attachment attached;
            Point player{1000, -500, 0}, follower = player + Point{70 * side, -8, 0};
            check(attached.Capture(player, follower, 0), "capture original partner position without a snap");
            const float dt = 1 / fps;
            for (int i = 0; i < 600; ++i) {
                const float speed = i < 200 ? 110.0F : i < 400 ? 280.0F : 450.0F;
                const float heading = i * dt * .4F;
                player = player + Point{std::sin(heading) * speed * dt, std::cos(heading) * speed * dt, 0};
                if (i % 20 < 10) {
                    player.x += dt * 30;
                }
                auto goal = attached.Advance(dt, player, heading, false);
                check(goal.has_value(), "walking, jogging and non-sprint running remain attached");
                if (goal) {
                    const auto offset = *goal - player;
                    check(std::abs(Dot(offset, attached.Right()) - 70 * side) < .003F,
                          "captured side and spacing stay fixed while moving");
                    check(std::abs(Dot(offset, attached.Forward()) + 8) < .003F,
                          "forward offset has no route anticipation or catch-up delay");
                    follower = *goal;
                }
            }
            check(!attached.Advance(dt, player, attached.Yaw(), true) && !attached.Active(),
                  "sprint releases even without a speed increase");
            check(!attached.Advance(dt, player, 0, false),
                  "ending sprint cannot reattach without a fresh delayed acquisition");
            attached.Capture(player, follower, 0);
            attached.Reset();
            check(!attached.Advance(dt, player, 0, false), "release removes positional attachment");
        }
    }
    Attachment turn;
    turn.Capture({0, 0, 0}, {70, 0, 0}, 0);
    auto turnGoal = turn.Advance(1.0F / 60, {0, 0, 0}, 3.14159265F, false);
    check(Length(turn.Velocity()) > 100,
          "turning locomotion includes the follower's movement around the player");
    check(turnGoal && Length(*turnGoal - Point{70, 0, 0}) < 8,
          "sudden reversal rotates gradually without teleporting through the player");
    for (int i = 0; i < 31; ++i) {
        turnGoal = turn.Advance(1.0F / 60, {0, 0, 0}, 3.14159265F, false);
    }
    check(turnGoal && Length(*turnGoal - Point{-70, 0, 0}) < .01F,
          "partner completes a half-turn on the same chosen side");
    check(!turn.Advance(1.0F / 60, {1000, 0, 0}, 0, false),
          "teleports release instead of dragging the follower");
    check(Length(turn.Velocity()) == 0, "release clears attachment motion immediately");
    turn.Capture({0, 0, 0}, {70, 0, 0}, 0);
    check(!turn.Advance(.5F, {30, 0, 0}, 0, false), "long stalls do not cause large attachment warps");
    auto transformed = [](const Matrix& m, Point p) {
        return Point{m.m[0][0] * p.x + m.m[0][1] * p.y + m.m[0][2] * p.z,
                     m.m[1][0] * p.x + m.m[1][1] * p.y + m.m[1][2] * p.z,
                     m.m[2][0] * p.x + m.m[2][1] * p.y + m.m[2][2] * p.z};
    };

    const Point down{0, 0, -1}, indexAcross{0, 1, 0};
    for (int side : {-1, 1}) {
        for (bool player : {false, true}) {
            const bool left = player ? side < 0 : side > 0;
            const Point neutralPalm{left ? 1.0F : -1.0F, 0, 0};
            for (int heading = 0; heading < 360; heading += 15) {
                const auto turn = Rotation(heading * .0174532925F, {0, 0, 1});
                const auto desiredPalm =
                    transformed(turn, {static_cast<float>(side) * (player ? 1.0F : -1.0F), 0, 0});
                for (auto axis : {Point{1, 0, 0}, Point{0, 1, 0}, Point{0, 0, 1}, Point{1, 2, 3}}) {
                    for (float angle : {0.0F, .6F, 1.5F, 3.14159265F, -2.8F}) {
                        const auto animated = Rotation(angle, axis);
                        const auto fingers = transformed(animated, down),
                                   across = transformed(animated, indexAcross);
                        const auto palm = transformed(animated, neutralPalm);
                        const auto wrist = WristAlignment(fingers, across, left, desiredPalm);
                        check(wrist.has_value(),
                              "both anatomical hands have a valid wrist frame in every pair role");
                        if (!wrist) {
                            continue;
                        }
                        check(Length(transformed(*wrist, fingers) - down) < .001F,
                              "palm alignment never flips fingers upward, including an opposite palm");
                        check(Length(transformed(*wrist, palm) - desiredPalm) < .001F,
                              "actual mirrored palm faces the partner for PC and follower");
                        const auto x = transformed(*wrist, {1, 0, 0}), y = transformed(*wrist, {0, 1, 0}),
                                   z = transformed(*wrist, {0, 0, 1});
                        check(Dot(Cross(x, y), z) > .999F,
                              "mirrored hands are rotated without reflecting the mesh");
                        const auto curled = transformed(Rotation(.38F, Cross(down, desiredPalm)), down);
                        check(Dot(curled, desiredPalm) > 0 && curled.z < 0,
                              "fingers curl into the palm while staying downward");
                    }
                }
            }
        }
    }
    for (bool left : {false, true}) {

        const auto opposite = WristAlignment(down, indexAcross, left, {left ? -1.0F : 1.0F, 0, 0});
        check(opposite.has_value(), "opposite palm alignment is supported");
        if (opposite) {
            for (float weight : {0.0F, .1F, .5F, .9F, 1.0F}) {
                check(Length(transformed(BlendRotation({}, *opposite, weight), down) - down) < .001F,
                      "180-degree palm blend turns around fingers without lifting them");
            }
        }
    }
    check(!WristAlignment(down, down, false, {1, 0, 0}),
          "collinear knuckles cannot introduce an arbitrary wrist roll");
    check(!WristAlignment({}, indexAcross, true, {1, 0, 0}),
          "collapsed fingers leave the animated wrist alone");
    check(!WristAlignment(down, indexAcross, false, {0, 0, 1}),
          "vertical target cannot define a downward palm frame");
    check(!WristAlignment(down, indexAcross, false, {std::numeric_limits<float>::infinity(), 0, 0}),
          "nonfinite wrist geometry is rejected");
    for (auto axis : {Point{1, 0, 0}, Point{0, 1, 0}, Point{0, 0, 1}, Point{1, 2, 3}}) {
        for (float angle : {0.0F, 1.0F, 3.14159265F, -2.8F}) {
            const auto end = Rotation(angle, axis);
            check(Length(transformed(BlendRotation({}, end, 0), {1, 2, 3}) - Point{1, 2, 3}) < .001F,
                  "rotation blend begins at animated pose");
            check(Length(transformed(BlendRotation({}, end, 1), {1, 2, 3}) - transformed(end, {1, 2, 3})) <
                      .001F,
                  "rotation blend ends at grip orientation, including 180-degree turns");
            for (float weight : {.1F, .4F, .8F}) {
                auto matrix = BlendRotation({}, end, weight);
                const auto x = transformed(matrix, {1, 0, 0}), y = transformed(matrix, {0, 1, 0}),
                           z = transformed(matrix, {0, 0, 1});
                check(std::abs(Length(x) - 1) < .001F && std::abs(Length(y) - 1) < .001F &&
                          std::abs(Length(z) - 1) < .001F,
                      "blended rotation preserves scale");
                check(std::abs(Dot(x, y)) < .001F && Dot(Cross(x, y), z) > .999F,
                      "blended rotation remains orthogonal and does not reflect bones");
            }
        }
    }
    const Point origin{0, 0, 0};
    for (float upper : {20.0F, 35.0F, 50.0F}) {
        for (float lower : {22.0F, 35.0F, 45.0F}) {
            for (float fraction : {.4F, .65F, .9F}) {
                for (float sign : {-1.0F, 1.0F}) {
                    const float distance = (upper + lower) * fraction;
                    const Point target{distance * .6F * sign, 0, -distance * .8F};
                    auto result = Solve(origin, upper, lower, target, {0, -40, 0});
                    if (distance <= std::abs(upper - lower) + .01F) {
                        continue;
                    }
                    check(result.has_value(), "reachable varied-size arms solve on either side");
                    if (result) {
                        check(std::abs(Length(result->elbow) - upper) < .001F, "upper arm length preserved");
                        check(std::abs(Length(result->hand - result->elbow) - lower) < .001F,
                              "forearm length preserved");
                        check(Length(result->hand - target) < .001F, "hand reaches shared target");
                        check(result->elbow.y < 0, "elbow bend follows pole direction");
                    }
                }
            }
        }
    }
    check(!Solve(origin, 30, 30, {100, 0, 0}, {0, 1, 0}), "overreach rejected rather than stretching");
    check(!Solve(origin, 30, 10, {5, 0, 0}, {0, 1, 0}), "unreachable folded arm rejected");
    check(Solve(origin, 30, 30, {40, 0, 0}, {100, 0, 0}).has_value(), "collinear pole has a stable fallback");
    check(!Solve(origin, 30, 30, {std::numeric_limits<float>::infinity(), 0, 0}, {0, 1, 0}),
          "nonfinite geometry rejected");
    return failed ? 1 : 0;
}

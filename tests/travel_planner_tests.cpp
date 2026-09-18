#include "individual_travel.h"
#include "travel_planner.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>

using namespace Wayfarer;
namespace
{
    int checks = 0;
    void Check(bool good, const char* message)
    {
        ++checks;
        if (!good) {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(1);
        }
    }
    float Distance(Vec2 a, Vec2 b)
    {
        return Length({a.x - b.x, a.y - b.y});
    }
    void Move(TravelIntent& intent, Vec2 velocity, float seconds, TravelTuning t = {})
    {
        for (float time = 0; time < seconds; time += 0.05F) {
            intent.Update({velocity.x * time, velocity.y * time}, velocity, 0.05F, t);
        }
    }
} // namespace

int main()
{
    TravelTuning t;
    TravelIntent intent;
    intent.Update({}, {0, 10}, 0.1F, t);
    Check(!intent.CanTravel(), "minor movement cannot acquire a party");
    intent.Update({}, {0, 100}, 0.1F, t);
    Check(!intent.CanTravel(), "one brief step cannot acquire a party");
    intent.Update({}, {}, 0.2F, t);
    Check(!intent.CanTravel(), "brief movement cancels cleanly");
    Move(intent, {0, 100}, 2);
    Check(intent.IsMoving(), "sustained walking acquires the party");
    const auto goal = intent.Goal(FormationMode::kDynamic, 0, false, 1, t);
    Check(goal.y > 350, "walking companion is sent ahead");
    for (int i = 0; i < 40; ++i) {
        intent.Update({0, 205}, {}, 0.05F, t);
    }
    Check(intent.Phase() == TravelPhase::kSettling, "stop enters settling");
    Check(Distance(goal, intent.Goal(FormationMode::kDynamic, 0, false, 1, t)) < 0.01F,
          "stop does not retract destination");
    for (int i = 0; i < 140; ++i) {
        intent.Update({0, 205}, {}, 0.05F, t);
    }
    Check(!intent.CanTravel(), "long idle releases the party");
    intent.Reset();
    Move(intent, {0, 350}, 2);
    intent.Update({}, {0, -350}, 0.05F, t);
    Check(intent.Direction().y > -0.9F, "180 degree reversal does not snap in one frame");
    Move(intent, {0, -350}, 2);
    Check(intent.Direction().y < -0.98F, "exact reversal converges without normalization deadlock");
    Check(std::abs(Length(intent.Direction()) - 1) < 0.001F, "direction remains normalized");
    TravelIntent filtered, unfiltered;
    TravelTuning immediate = t;
    immediate.turnDelay = 0;
    Move(filtered, {0, 100}, 2, t);
    Move(unfiltered, {0, 100}, 2, immediate);
    float filteredHeading = 0, rawHeading = 0, filteredWiggle = 0, rawWiggle = 0;
    auto previousFiltered = filtered.Goal(FormationMode::kDynamic, 0, false, 1, t);
    auto previousRaw = unfiltered.Goal(FormationMode::kDynamic, 0, false, 1, immediate);
    for (int frame = 1; frame <= 180; ++frame) {
        const float time = frame / 60.0F, oscillation = time * 31.41592654F;
        const Vec2 position{8 * std::sin(oscillation), 195 + 100 * time};
        const Vec2 velocity{251.3274F * std::cos(oscillation), 100};
        filtered.Update(position, velocity, 1.0F / 60, t);
        unfiltered.Update(position, velocity, 1.0F / 60, immediate);
        const auto a = filtered.Goal(FormationMode::kDynamic, 0, false, 1, t),
                   b = unfiltered.Goal(FormationMode::kDynamic, 0, false, 1, immediate);
        filteredHeading = std::max(filteredHeading, std::abs(filtered.Direction().x));
        rawHeading = std::max(rawHeading, std::abs(unfiltered.Direction().x));
        filteredWiggle += std::abs(a.x - previousFiltered.x);
        rawWiggle += std::abs(b.x - previousRaw.x);
        previousFiltered = a;
        previousRaw = b;
    }
    Check(filteredHeading < .03F && rawHeading > .1F, "brief alternating zigzags do not rotate the party");
    Check(filteredWiggle < rawWiggle * .35F, "lateral footwork is smoothed without copying each sidestep");
    const auto beforeTurn = filtered.Direction();
    filtered.Update({10, 500}, {100, 0}, .1F, t);
    Check(Distance(beforeTurn, filtered.Direction()) < .01F, "a new corner gets a short recognition delay");
    for (int frame = 1; frame <= 40; ++frame) {
        filtered.Update({10 + frame * 100 / 60.0F, 500}, {100, 0}, 1.0F / 60, t);
    }
    Check(filtered.Direction().x > .9F, "deliberate ninety-degree turn is followed promptly");
    TravelIntent gradual;
    Move(gradual, {0, 100}, 2, t);
    Vec2 curved{0, 195};
    for (int frame = 0; frame < 180; ++frame) {
        float yaw = 1.5707963F * frame / 179;
        Vec2 velocity{100 * std::sin(yaw), 100 * std::cos(yaw)};
        curved.x += velocity.x / 60;
        curved.y += velocity.y / 60;
        gradual.Update(curved, velocity, 1.0F / 60, t);
    }
    for (int frame = 0; frame < 45; ++frame) {
        curved.x += 100 / 60.0F;
        gradual.Update(curved, {100, 0}, 1.0F / 60, t);
    }
    Check(gradual.Direction().x > .97F, "gradual curves are not mistaken for persistent jitter");
    const auto stoppedGoal = filtered.Goal(FormationMode::kDynamic, 0, false, 1, t);
    for (int frame = 0; frame < 30; ++frame) {
        filtered.Update({80, 500}, {}, 1.0F / 60, t);
    }
    Check(Distance(stoppedGoal, filtered.Goal(FormationMode::kDynamic, 0, false, 1, t)) < .01F,
          "filtered route freezes on stop instead of retracting");
    TravelIntent walk, run;
    for (int i = 0; i < 80; ++i) {
        walk.Update({}, {0, 100}, .05F, t);
        run.Update({}, {0, 350}, .05F, t);
    }
    Check(run.Goal(FormationMode::kDynamic, 0, false, 1, t).y >
              walk.Goal(FormationMode::kDynamic, 0, false, 1, t).y + 100,
          "running gets additional lead room");
    const auto outdoor = run.Goal(FormationMode::kDynamic, 0, false, 1, t);
    const auto indoor = run.Goal(FormationMode::kDynamic, 0, true, 1, t);
    Check(indoor.y < outdoor.y && std::abs(indoor.x) < std::abs(outdoor.x),
          "interiors compress lead and width");
    for (int slot = 0; slot < 5; ++slot) {
        auto right = run.Goal(FormationMode::kDynamic, slot, false, 1, t);
        auto left = run.Goal(FormationMode::kDynamic, slot, false, -1, t);
        Check(std::abs(right.x + left.x) < .01F && std::abs(right.y - left.y) < .01F,
              "preferred side mirrors lateral spacing");
    }
    Check(ChoosePace(100, 150, TravelPace::kWalk, true) == TravelPace::kWalk, "walking player selects walk");
    Check(ChoosePace(350, 150, TravelPace::kWalk, true) == TravelPace::kRun, "running player selects run");
    Check(ChoosePace(100, 700, TravelPace::kWalk, true) == TravelPace::kRun, "lagging companion catches up");
    Check(ChoosePace(170, 150, TravelPace::kRun, true) == TravelPace::kRun,
          "pace hysteresis prevents chattering");
    Check(ChoosePace(0, 70, TravelPace::kRun, false) == TravelPace::kWalk, "settling eases pace");
    Check(ChoosePace(106, 175, TravelPace::kJog, true) == TravelPace::kWalk,
          "ordinary 106-unit walk exits catch-up before reaching arrival");
    Check(ChoosePace(119, 155, TravelPace::kJog, true) == TravelPace::kWalk,
          "faster walking does not latch Jog near formation");
    Check(ChoosePace(106, 205, TravelPace::kJog, true) == TravelPace::kJog,
          "catch-up keeps a separate threshold from resuming Walk");
    TravelGoalState route;
    route.Commit({0, 500}, {0, 0}, TravelPace::kWalk);
    Check(route.ShouldRepath({}, 106, true, true, t), "initial route is submitted");
    route.MarkRepathed();
    route.Tick({0, 32}, .30F, true, t);
    route.Commit({0, 532}, {0, 32}, TravelPace::kWalk);
    Check(!route.ShouldRepath({0, 32}, 106, true, true, t),
          "moving the marker preserves a useful straight route");
    route.Tick({0, 400}, .30F, true, t);
    route.Tick({0, 400}, .15F, true, t);
    Check(route.ShouldRepath({0, 400}, 106, true, true, t),
          "route updates before consuming its original endpoint even after marker commits");
    route.MarkRepathed();
    route.Tick({0, 410}, .15F, true, t);
    Check(!route.ShouldRepath({0, 410}, 106, true, false, t),
          "package reacquisition cannot spam resets each update");
    route.Release();
    Check(!route.ShouldRepath({0, 410}, 106, true, true, t), "released lease cannot request a route");
    TravelGoalState state;
    state.Commit({0, 400}, {}, TravelPace::kWalk);
    state.Tick({}, .15F, true, t);
    Check(!state.ShouldCommit({0, 600}, {}, TravelPace::kRun, t),
          "minimum route lifetime prevents per-tick restarts");
    for (int i = 0; i < 5; ++i) {
        state.Tick({}, .15F, true, t);
    }
    Check(!state.ShouldCommit({5, 405}, {}, TravelPace::kWalk, t), "small jitter does not restart path");
    Check(state.ShouldCommit({0, 600}, {}, TravelPace::kWalk, t), "meaningful movement refreshes path");
    for (int i = 0; i < 40 && state.recoveryTime <= 0; ++i) {
        state.Tick({}, .15F, true, t);
        if (state.ShouldCommit({0, 600 + static_cast<float>(i) * 100}, {}, TravelPace::kWalk, t)) {
            state.Commit({0, 600 + static_cast<float>(i) * 100}, {}, TravelPace::kWalk);
        }
    }
    Check(state.recoveryTime > 0 && !state.active, "moving destinations cannot hide a stuck follower");
    Check(!state.ShouldCommit({0, 400}, {}, TravelPace::kWalk, t), "recovery yields before reacquiring");
    for (int i = 0; i < 30; ++i) {
        state.Tick({}, .15F, false, t);
    }
    Check(state.ShouldCommit({0, 400}, {}, TravelPace::kWalk, t), "recovery permits reacquisition");
    state.Commit({0, 40}, {}, TravelPace::kWalk);
    for (int i = 0; i < 100; ++i) {
        state.Tick({}, .15F, true, t);
    }
    Check(state.active && state.recoveryTime == 0, "arrived companion is not stuck");
    TravelGoalState handoff;
    handoff.Commit({0, 1000}, {}, TravelPace::kRun);
    handoff.MarkRepathed();
    for (int i = 0; i < 40 && handoff.active; ++i) {
        handoff.Tick({}, .15F, false, t);
        if (handoff.active) {
            handoff.Commit({0, 1000 + float(i) * 20}, {}, TravelPace::kRun);
        }
    }
    Check(!handoff.active && handoff.recoveryTime > 0,
          "stationary actor with a competing package triggers recovery despite moving targets");
    TravelGoalState movingHandoff;
    movingHandoff.Commit({0, 3000}, {}, TravelPace::kRun);
    movingHandoff.MarkRepathed();
    for (int i = 1; i <= 40; ++i) {
        movingHandoff.Tick({0, float(i) * 20}, .15F, false, t);
    }
    Check(movingHandoff.active && movingHandoff.recoveryTime == 0,
          "moving actor under another package is not mistaken for a frozen actor");
    TravelGoalState retry;
    retry.Commit({0, 1000}, {}, TravelPace::kRun);
    retry.MarkRepathed();
    for (int i = 0; i < 6; ++i) {
        retry.Tick({}, .15F, false, t);
        Check(!retry.ShouldRepath({}, 100, true, false, t), "handoff receives a full second before a retry");
    }
    retry.Tick({}, .15F, false, t);
    Check(retry.ShouldRepath({}, 100, true, false, t), "unaccepted handoff retries after its grace period");
    const std::array<GroundPoint, 3> slope{{{0, 0, 0}, {100, 0, 100}, {0, 100, 0}}};
    const auto weights = GroundWeights({25, 25, 0}, slope);
    Check(weights && std::abs((*weights)[1] * 100 - 25) < .01F, "ground height follows slopes");
    const auto outside = GroundWeights({100, 100, 0}, slope);
    Check(outside && (*outside)[0] < 0, "outside triangle is distinguishable");
    Check(!GroundWeights({}, {{{0, 0, 0}, {0, 0, 100}, {0, 100, 0}}}), "degenerate nav triangles rejected");
    std::array<GroundTriangle, 2> corridor{
        {{{{{0, 0, 0}, {600, 0, 120}, {0, 600, 0}}}, {{std::nullopt, GroundKey{1}, std::nullopt}}},
         {{{{600, 0, 120}, {600, 600, 120}, {0, 600, 0}}}, {{std::nullopt, std::nullopt, GroundKey{0}}}}}};
    auto query = [&](GroundKey id) -> std::optional<GroundTriangle> {
        return id < corridor.size() ? std::optional<GroundTriangle>{corridor[id]} : std::nullopt;
    };
    auto path = TraceGroundCorridor({100, 100, 20}, {500, 500}, 0, query);
    Check(path && std::abs(path->x - 500) < .01F && std::abs(path->z - 100) < .01F,
          "corridor crosses linked triangles and follows elevation");
    auto boundaryStart = TraceGroundCorridor({300, 300, 60}, {500, 500}, 0, query);
    Check(boundaryStart && std::abs(boundaryStart->x - 500) < .01F,
          "starting exactly on a shared edge traverses its neighbor");
    Check(!TraceGroundCorridor({500, 500, 100}, {450, 500}, 0, query),
          "a companion outside the player triangle cannot trace from the player triangle");
    auto companionPath = TraceGroundCorridor({500, 500, 100}, {450, 500}, 1, query);
    Check(companionPath && std::abs(companionPath->x - 450) < .01F,
          "companion route succeeds from its own ground triangle");
    auto tiny = TraceGroundStep({100, 100, 32}, {100.25F, 100, 32}, 0, query);
    Check(tiny && std::abs(tiny->z - 32.05F) < .01F,
          "sub-unit step follows slope while preserving feet clearance");
    auto still = TraceGroundStep({100, 100, 32}, {100, 100, 32}, 0, query);
    Check(still && std::abs(still->z - 32) < .01F,
          "standing attachment stays at its current physical height");
    auto seam = TraceGroundStep({299.75F, 300, 71.95F}, {300.25F, 300, 72.05F}, 0, query);
    Check(seam && std::abs(seam->x - 300.25F) < .001F, "sub-unit step crosses a connected triangle seam");
    Check(!TraceGroundStep({100, 100, 20}, {500, 500, 100}, 0, query),
          "attachment rejects a large change in ground height");
    Check(!TraceGroundStep({100, 100, 20}, {100, 100, 100}, 0, query),
          "attachment rejects a different floor at the requested endpoint");
    Check(!TraceGroundStep({100, 100, 20}, {100, 100, 20}, 100, query),
          "stationary attachment still requires valid ground");
    corridor[0].neighbors[1] = std::nullopt;
    Check(!TraceGroundStep({299.75F, 300, 71.95F}, {300.25F, 300, 72.05F}, 0, query),
          "even a tiny attachment step cannot cross a disconnected edge");
    path = TraceGroundCorridor({100, 100, 20}, {500, 500}, 0, query);
    Check(path && path->x < 280 && path->y < 280,
          "disconnected navmesh clips before boundary instead of jumping to a nearby surface");
    Check(!TraceGroundCorridor({290, 290, 58}, {500, 500}, 0, query),
          "too-short corridor yields instead of leading off an edge");
    corridor[0].neighbors[1] = GroundKey{0};
    Check(!TraceGroundCorridor({100, 100, 20}, {500, 500}, 0, query),
          "cyclic navigation terminates within a fixed work limit");
    Check(!TraceGroundCorridor({100, 100, 20}, {500, 500}, 100, query), "missing navmesh fails closed");
    Check(ChoosePace(100, 420, TravelPace::kWalk, true) == TravelPace::kRun,
          "catch-up starts running before a 650-unit gap develops");
    Check(ChoosePace(100, 280, TravelPace::kRun, true) == TravelPace::kRun,
          "catch-up does not drop to jog prematurely");
    const float cruiseBuffer = 335.0F * (t.refreshSeconds + .20F);
    const float catching = DesiredSpeedScale(335, cruiseBuffer + 600, TravelPace::kRun, true, t);
    const float cruising = DesiredSpeedScale(335, cruiseBuffer, TravelPace::kRun, true, t);
    Check(catching > 1.4F && catching <= t.maxSpeedScale, "lagging runner has capped speed above the player");
    Check(std::abs(cruising - 1.0F) < .001F, "at formation buffer running matches player speed");
    Check(DesiredSpeedScale(335, cruiseBuffer + 80, TravelPace::kRun, true, t) < catching,
          "catch-up tapers continuously near formation");
    Check(DesiredSpeedScale(335, 120, TravelPace::kRun, true, t) < cruising,
          "companion eases pace when too far ahead");
    Check(DesiredSpeedScale(0, 1000, TravelPace::kRun, false, t) == 1.0F,
          "stopping removes the catch-up speed request");
    Check(DesiredSpeedScale(1000, 2000, TravelPace::kRun, true, t) <= 1.5F,
          "extreme player speed cannot bypass multiplier cap");
    Check(DesiredSpeedScale(335, 1000, TravelPace::kRun, true, t, -1.0F) <= 1.0F,
          "turning around cannot request extra catch-up speed");
    Check(DesiredSpeedScale(335, 1000, TravelPace::kRun, true, t, 0.0F) <= 1.0F,
          "sideways obstacle avoidance does not get a catch-up boost");
    Check(SmoothSpeedScale(1.0F, 1.5F, .15F) <= 1.1201F, "speed acceleration is bounded per update");
    Check(SmoothSpeedScale(1.5F, 1.0F, .15F) >= 1.2599F, "speed deceleration is bounded per update");
    Check(SmoothSpeedScale(1.0F, 1.5F, 0.0F) == 1.0F, "zero elapsed time never changes speed");
    float gap = cruiseBuffer + 600.0F;
    float multiplier = 1.0F;
    for (int i = 0; i < 100; ++i) {
        multiplier =
            SmoothSpeedScale(multiplier, DesiredSpeedScale(335, gap, TravelPace::kRun, true, t), .1F);
        gap += (335.0F - 335.0F * multiplier) * .1F;
    }
    Check(gap - cruiseBuffer < 35.0F,
          "equal-speed running companion closes a 600-unit excess gap within ten simulated seconds");
    Check(multiplier < 1.06F, "catch-up returns to cruise after closing the gap");
    const auto stable = MakeTravelDisposition(1234), same = MakeTravelDisposition(1234);
    Check(UrgentIndividualTravel(1000, 500, false, 0, 140),
          "distant newcomer skips personal acquisition delay");
    Check(UrgentIndividualTravel(800, 500, true, 900, 140), "real route gap gets immediate catch-up");
    Check(!UrgentIndividualTravel(800, 500, true, 200, 140),
          "distant assigned Rear role retains individuality near its point");
    Check(!UrgentIndividualTravel(300, 500, true, 900, 140),
          "nearby corner still uses personal turn response");
    Check(stable.cruise == same.cruise && stable.reaction == same.reaction &&
              stable.turnDelay == same.turnDelay,
          "travel disposition is stable for an identity");
    std::array<IndividualTravel, 6> people;
    std::set<int> reactions, turns, cruises;
    for (int i = 0; i < 6; ++i) {
        people[i].Initialize(1234 + i * 7919);
        const auto& profile = people[i].Disposition();
        reactions.insert(static_cast<int>(profile.reaction * 1000));
        turns.insert(static_cast<int>(profile.turnSharpness * 1000));
        cruises.insert(static_cast<int>(profile.cruise * 1000));
        Check(profile.reaction >= .07F && profile.reaction <= .45F && profile.cruise >= .925F &&
                  profile.cruise <= .995F,
              "personal pace and reaction bounds");
        Check(profile.catchGap > profile.settleGap + 20, "catch-up thresholds have a useful hysteresis band");
        people[i].Update({0, 1}, .01F, true, false, false, 1);
        Check(!people[i].Ready(false, 1) && people[i].Ready(true, 1) && people[i].Ready(false, 0),
              "initial delay is skipped for urgent recovery or individuality off");
    }
    Check(reactions.size() >= 5 && turns.size() >= 5 && cruises.size() >= 5,
          "party members receive different independent travel tendencies");
    for (auto& person : people) {
        for (int frame = 0; frame < 6; ++frame) {
            person.Update({.7071068F, .7071068F}, .05F, true, false, false, 1);
        }
    }
    float minHeading = 1, maxHeading = 0;
    for (auto& person : people) {
        minHeading = std::min(minHeading, person.Direction().x);
        maxHeading = std::max(maxHeading, person.Direction().x);
    }
    Check(maxHeading - minHeading > .05F, "companions turn at visibly different rates");
    for (auto& person : people) {
        for (int frame = 0; frame < 60; ++frame) {
            person.Update({1, 0}, .05F, true, false, false, 1);
        }
        Check(person.Direction().x > .99F, "even the slowest turning companion completes a sustained turn");
        person.Update({-1, 0}, .05F, true, false, true, 1);
        Check(person.Direction().x < -.93F, "indoor reversal cannot retain a stale opposite route");
        const auto heading = person.Direction();
        person.Update({0, 1}, .5F, false, false, false, 1);
        Check(Distance(heading, person.Direction()) < .001F,
              "settling freezes the individual's endpoint heading");
        person.Update({0, 1}, .05F, true, false, false, 0);
        Check(Distance(person.Direction(), {0, 1}) < .001F, "zero individuality restores shared steering");
    }
    TravelTuning personalTuning;
    personalTuning.individuality = 1;
    personalTuning.catchUpSeconds = 1.6F;
    std::array<float, 6> personalGap, personalScale;
    personalGap.fill(135);
    personalScale.fill(1);
    std::array<TravelPace, 6> personalPace{};
    std::array<bool, 6> wasCatching{};
    std::array<int, 6> caughtCount{};
    int mixedFrames = 0;
    float peakGap = 0;
    for (int frame = 0; frame < 2400; ++frame) {
        int catchingCount = 0;
        for (int i = 0; i < 6; ++i) {
            auto& person = people[i];
            person.Update({0, 1}, .05F, true, false, false, 1);
            personalPace[i] = ChoosePace(100, personalGap[i], personalPace[i], true);
            const float target =
                person.SpeedScale(100, personalGap[i], personalPace[i], true, personalTuning, 1, false);
            personalScale[i] = std::min(SmoothSpeedScale(personalScale[i], target, .05F), target + .12F);
            const float nominal = personalPace[i] == TravelPace::kRun   ? 335
                                  : personalPace[i] == TravelPace::kJog ? 245
                                                                        : 100;
            personalGap[i] += (100 - nominal * personalScale[i]) * .05F;
            peakGap = std::max(peakGap, personalGap[i]);
            if (person.CatchingUp()) {
                ++catchingCount;
                if (!wasCatching[i]) {
                    ++caughtCount[i];
                }
            }
            wasCatching[i] = person.CatchingUp();
            Check(target >= .65F && target <= personalTuning.maxSpeedScale && std::isfinite(target),
                  "personal cadence stays within native speed caps");
        }
        if (catchingCount > 0 && catchingCount < 6) {
            ++mixedFrames;
        }
    }
    Check(peakGap < 300 && mixedFrames > 300,
          "individual catch-up cycles are bounded and do not synchronize");
    for (int count : caughtCount) {
        Check(count >= 1, "each companion catches up instead of falling behind indefinitely");
    }
    for (auto& person : people) {
        const auto ordinary = DesiredSpeedScale(100, 700, TravelPace::kRun, true, personalTuning, 1);
        Check(std::abs(person.SpeedScale(100, 700, TravelPace::kRun, true, personalTuning, 1, true) -
                       ordinary) < .001F,
              "large gaps bypass relaxed individual pacing");
        auto off = personalTuning;
        off.individuality = 0;
        Check(std::abs(person.SpeedScale(100, 180, TravelPace::kWalk, true, off, 1, false) -
                       DesiredSpeedScale(100, 180, TravelPace::kWalk, true, off, 1)) < .001F,
              "zero individuality restores previous speed feedback");
        Check(person.SpeedScale(0, 150, TravelPace::kWalk, false, personalTuning, 1, false) == 1,
              "stopped player gets no artificial cadence");
        const auto saved = person.Disposition();
        person.ResetMotion();
        person.Initialize(999);
        Check(saved.reaction == person.Disposition().reaction && saved.cruise == person.Disposition().cruise,
              "recovery retains the companion's disposition");
    }
    std::cout << checks << " travel regression checks passed; individual peak gap=" << peakGap
              << ", mixed catch-up frames=" << mixedFrames << "\n";
}

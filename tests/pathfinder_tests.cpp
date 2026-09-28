#include "pathfinder.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
using namespace Wayfarer;
using namespace Wayfarer::Pathfinder;
int checks = 0;
void Check(bool ok, const char* why)
{
    ++checks;
    if (!ok) {
        std::cerr << why << '\n';
        std::exit(1);
    }
}

int main()
{
    const Tuning tuning{};
    // What is worth pointing out.
    const Vec2 north{0, 1};
    Check(WorthPointing(north, {0, 3000}, 3000, tuning.range), "a place straight ahead");
    Check(WorthPointing(north, {3000, 200}, 3005, tuning.range), "a place off to the side");
    Check(!WorthPointing(north, {0, -3000}, 3000, tuning.range), "never a place behind the party");
    Check(!WorthPointing(north, {0, 900}, 900, tuning.range), "too close: the compass already shows it");
    Check(!WorthPointing(north, {0, 9000}, 9000, tuning.range), "too far to make out");
    Check(!WorthPointing(north, {0, 3000}, std::numeric_limits<float>::quiet_NaN(), tuning.range),
          "bad distances are ignored");

    // Where they run: straight at the place, well out from the player, never onto it.
    Check(RunLength(8000, tuning.run) == tuning.run, "a distant place: the full run");
    Check(std::abs(RunLength(1600, tuning.run) - (1600 - standoff)) < 1, "a near place: stop short of it");
    Check(RunLength(300, tuning.run) == 0 && RunLength(std::numeric_limits<float>::infinity(), 900) == 0,
          "never past the place, and bad distances run nowhere");
    Check(tuning.run >= 1000, "far enough to be seen closing on it");
    const auto ahead = RunPoint({0, 0}, north, {0, 3000}, 1200);
    Check(std::abs(ahead.x) < 1 && std::abs(ahead.y - 1200) < 1, "straight ahead: run straight ahead");
    const auto side = RunPoint({100, 100}, north, {3000, 0}, 1200);
    Check(std::abs(side.x - 1300) < 1 && std::abs(side.y - 100) < 1, "a place to the east: run east, at it");
    const auto right = RunPoint({0, 0}, north, {0, 3000}, 1000, 90);
    Check(std::abs(right.x - 1000) < 1 && std::abs(right.y) < 1, "a fanned line turns clockwise");
    const auto fan = RunPoint({0, 0}, north, {0, 3000}, 1000, -30);
    Check(fan.x < 0 && std::abs(std::hypot(fan.x, fan.y) - 1000) < 1, "and the same distance either side");
    Check(Leash(tuning) > tuning.run + 300, "the leash leaves room for the run");

    // Seeing a distant structure.
    Check(Visible(1, 3000), "nothing in the way");
    Check(Visible(.92F, 3000), "the ray stops at the structure itself");
    Check(!Visible(.3F, 3000), "a hill in between hides it");

    // The sequence. Each step gives the distances from the companion to the running point, to the
    // player and to the place, and from the player to the place.
    Cue cue;
    const auto tick = [&](State& state, float toGoal, float fromPlayer, float toPlace, float playerToPlace,
                          bool facing, const Rules& rules) {
        return state.Tick(.1F, {toGoal, fromPlayer, toPlace, playerToPlace, facing}, rules, cue);
    };
    const Rules rules = RulesFor(tuning);
    Rules single = rules;
    single.lead = false;
    Check(rules.stopAt == tuning.run && rules.resume == tuning.resume && rules.leash == Leash(tuning),
          "rules follow the tuning");
    Tuning cramped;
    cramped.run = 400;
    cramped.resume = 1200;
    Check(RulesFor(cramped).resume == 200, "they wait for you to come within half the stopping distance at most");

    // Without leading: run, turn to the player and wave, turn to the place and point, rejoin.
    State s;
    Check(tick(s, 300, 100, 5000, 5300, false, single) == End::None && !s.Active(), "idle does nothing");
    s.Start(300, 5000);
    Check(s.phase == Phase::Dash, "starts running");
    for (int i = 0; i < 10; ++i) {
        tick(s, 300 - i * 20.0F, 200, 5000 - i * 20.0F, 5300, false, single);
    }
    Check(s.phase == Phase::Dash, "still running while short of the point");
    tick(s, 80, 200, 4780, 5300, false, single);
    Check(s.phase == Phase::Wave && !cue.wave, "arrives and turns toward the player");
    tick(s, 0, 200, 4780, 5300, true, single);
    Check(cue.wave, "waves once facing the player");
    tick(s, 0, 200, 4780, 5300, true, single);
    Check(!cue.wave, "one wave, not a stream of them");
    float waved = 0;
    while (s.phase == Phase::Wave && waved < 5) {
        tick(s, 0, 250, 4780, 5300, true, single);
        waved += .1F;
    }
    Check(s.phase == Phase::Point && waved >= waveSeconds - .15F && waved <= waveSeconds + .15F,
          "holds the wave, then turns to the place");
    int points = 0;
    End end = End::None;
    for (int i = 0; i < 60 && end == End::None; ++i) {
        end = tick(s, 0, 300, 4780, 5300, i >= 3, single);
        points += cue.point;
    }
    Check(points == 1 && end == End::Done, "points once and rejoins");
    Check(s.elapsed < DashSeconds(300) + 2 * turnSeconds + waveSeconds + pointSeconds + 1, "brief");

    // Leading the way: out ahead, point it out, wait, walk on when the player comes, stop again
    // whenever far enough ahead, and point once more on arriving.
    const auto gestures = [&](State& state, float fromPlayer, float toPlace, float playerToPlace, int& waves,
                              int& pointed) {
        waves = pointed = 0;
        for (int i = 0; i < 100 && state.phase != Phase::Wait && state.Active(); ++i) {
            tick(state, 0, fromPlayer, toPlace, playerToPlace, true, rules);
            waves += cue.wave;
            pointed += cue.point;
        }
    };
    int waves = 0;
    State g;
    g.Start(1800, 6000);
    tick(g, 900, 1000, 5500, 6200, false, rules);
    Check(g.phase == Phase::Dash, "runs on while still close to the player");
    tick(g, 400, rules.stopAt - farEnough, 5000, 6200, false, rules);
    Check(g.phase == Phase::Wave && g.stops == 1 && !g.last,
          "far enough ahead of the player: stops short of the running point");
    gestures(g, 1400, 5000, 6200, waves, points);
    Check(g.phase == Phase::Wait && waves == 1 && points == 1 && g.pointed, "waves, points it out, then waits");
    int beckons = 0;
    for (int i = 0; i < 100; ++i) {
        tick(g, 0, 1300, 5000, 6100, true, rules);
        beckons += cue.wave;
    }
    Check(g.phase == Phase::Wait && beckons == 1, "waits, waving you on again every few seconds");
    tick(g, 0, rules.resume - 10, 5000, 5400, true, rules);
    Check(cue.leadOn && g.phase == Phase::Wait, "you came close: time to lead on");
    tick(g, 0, rules.resume - 10, 5000, 5400, true, rules);
    Check(cue.leadOn, "and it keeps asking until the next leg is chosen");
    g.Lead(1600, 5000);
    Check(g.phase == Phase::Dash, "leads on");
    tick(g, 800, 900, 4200, 5000, false, rules);
    tick(g, 300, 1450, 3600, 5000, false, rules);
    Check(g.phase == Phase::Wave && g.stops == 2 && !g.last, "stops again once far enough ahead");
    gestures(g, 1400, 3600, 5000, waves, points);
    Check(g.phase == Phase::Wait && waves == 1 && points == 0, "a wave, no second announcement");
    tick(g, 0, 500, 3600, 4000, true, rules);
    Check(cue.leadOn, "you came close again");
    g.Lead(3100, 3600);
    tick(g, 1500, 800, 2000, 3900, false, rules);
    tick(g, 50, 1200, nearPlace - 10, 3900, false, rules);
    Check(g.phase == Phase::Wave && g.last, "arriving by the place makes this the last stop");
    gestures(g, 1200, nearPlace - 10, 3900, waves, points);
    Check(g.phase == Phase::Wait && points == 0, "waits for you there");
    tick(g, 0, 500, nearPlace - 10, 1000, true, rules);
    Check(g.phase == Phase::Point && g.closing && !cue.leadOn, "you arrive: they point at it");
    points = 0;
    end = End::None;
    for (int i = 0; i < 60 && end == End::None; ++i) {
        end = tick(g, 0, 500, nearPlace - 10, 1000, i >= 2, rules);
        points += cue.point;
    }
    Check(end == End::Done && points == 1, "then rejoin");

    // Ways a trip ends early.
    const auto toWait = [&](State& state) {
        state.Start(1600, 6000);
        tick(state, 200, 1450, 5000, 6000, false, rules);
        gestures(state, 1400, 5000, 6000, waves, points);
    };
    State away;
    toWait(away);
    Check(away.phase == Phase::Wait, "waiting");
    tick(away, 0, 1000, 5000, 5800, true, rules);
    end = tick(away, 0, 1000 + awayMargin + 10, 5000, 5800, true, rules);
    Check(end == End::WentAnotherWay, "you walk away from them while they wait: they come back");
    State patient;
    toWait(patient);
    end = End::None;
    float waited = 0;
    for (int i = 0; i < 2000 && end == End::None; ++i) {
        end = tick(patient, 0, 1300, 5000, 6000, true, rules);
        waited += .1F;
    }
    Check(end == End::StoppedFollowing && waited >= patienceSeconds - .5F,
          "you stand still too long: they come back");
    State stray;
    stray.Start(1600, 6000);
    tick(stray, 1200, 600, 5500, 5000, false, rules);
    end = tick(stray, 1100, 900, 5400, 5000 + strayMargin + 10, false, rules);
    Check(end == End::WentAnotherWay, "you turn away from the place while they run: they come back");
    State alone;
    alone.Start(1600, 6000);
    end = tick(alone, 1200, 600, 5500, 5000 + strayMargin + 10, false, single);
    Check(end == End::None, "without leading, only the leash applies");
    State blocked;
    toWait(blocked);
    tick(blocked, 0, 500, 5000, 5500, true, rules);
    blocked.Lead(900, 5000);
    tick(blocked, 800, 700, 4900, 5500, false, rules);
    for (int i = 0; i < 40 && blocked.phase == Phase::Dash; ++i) {
        tick(blocked, 800, 700, 4900, 5500, false, rules);
    }
    Check(blocked.phase == Phase::Wave && blocked.last, "a leg that gains too little is the last");
    State closed;
    toWait(closed);
    closed.Close();
    Check(closed.phase == Phase::Point && closed.closing, "no way closer: point it out from where they stand");
    end = End::None;
    for (int i = 0; i < 60 && end == End::None; ++i) {
        end = tick(closed, 0, 500, 5000, 5500, true, rules);
    }
    Check(end == End::Done, "and rejoin");
    Rules still = rules;
    still.wave = false;
    State quiet;
    quiet.Start(1600, 6000);
    tick(quiet, 50, 900, 5000, 6000, false, still);
    Check(quiet.phase == Phase::Point, "without waving: straight to pointing");
    for (int i = 0; i < 60 && quiet.phase != Phase::Wait; ++i) {
        tick(quiet, 0, 900, 5000, 6000, true, still);
        Check(!cue.wave, "and no waves");
    }
    for (int i = 0; i < 100; ++i) {
        tick(quiet, 0, 900, 5000, 6000, true, still);
        Check(!cue.wave, "not even while waiting");
    }
    tick(quiet, 0, 500, 5000, 5500, true, still);
    quiet.Lead(1600, 5000);
    tick(quiet, 50, 900, 3800, 5500, false, still);
    Check(quiet.phase == Phase::Wait, "later stops go straight to waiting");

    // A long run is given the time it needs while they keep gaining ground.
    State far;
    far.Start(1800, 8000);
    float run = 0;
    for (int i = 0; i < 80 && far.phase == Phase::Dash; ++i) {
        tick(far, 1800 - i * 30.0F, 900, 8000, 8000, false, rules);
        run += .1F;
    }
    Check(far.phase == Phase::Wave && run > 5.5F, "a 1800-unit run at 300 units a second arrives");
    // Their package can take a moment to engage: standing still for a second is not a stall.
    State late;
    late.Start(900, 8000);
    for (int i = 0; i < 15; ++i) {
        tick(late, 900, 400, 8000, 8000, false, rules);
    }
    Check(late.phase == Phase::Dash, "a slow start is waited out");
    for (int i = 0; i < 20; ++i) {
        tick(late, 900 - i * 30.0F, 400, 8000, 8000, false, rules);
    }
    Check(late.phase == Phase::Dash, "and then they run");
    // The running point moves on with the player: the clock allows for the extra distance.
    State moved;
    moved.Start(400, 8000);
    for (int i = 0; i < 40; ++i) {
        tick(moved, 1000 - i * 12.0F, 400, 8000, 8000, false, rules);
    }
    moved.Retarget(1200);
    Check(moved.phase == Phase::Dash && moved.dashLimit > moved.inPhase + 4, "retargeting extends the run");
    Check(moved.dashLimit <= maxDashSeconds, "but never past the overall limit");

    State slow;
    slow.Start(500, 8000);
    for (int i = 0; i < 50 && slow.phase == Phase::Dash; ++i) {
        tick(slow, 500, 200, 8000, 8000, false, rules);
    }
    Check(slow.phase != Phase::Dash && slow.elapsed < DashSeconds(500),
          "a run that stops gaining ground stops where it is");
    Check(slow.stalled >= stallSeconds, "because it stalled, not because time ran out");
    State capped;
    capped.Start(20000, 30000);
    for (int i = 0; i < 400 && capped.phase == Phase::Dash; ++i) {
        tick(capped, 20000 - i * 45.0F, 200, 30000, 30000, false, rules);
    }
    Check(capped.phase != Phase::Dash && capped.elapsed <= maxDashSeconds + .2F, "even a steady run has a limit");
    int turns = 0;
    while (slow.phase == Phase::Wave && turns < 30) {
        tick(slow, 0, 200, 8000, 8000, false, rules);
        ++turns;
        if (cue.wave) {
            break;
        }
    }
    Check(cue.wave && turns <= static_cast<int>(turnSeconds / .1F) + 1, "never turns for longer than a second");

    State behind;
    behind.Start(200, 5000);
    Check(tick(behind, 200, Leash(tuning) + 1, 5000, 5000, false, rules) == End::FellBehind,
          "gives up rather than straggle");

    // Words.
    Check(std::string_view(Noun(4)) == "a cave" && std::string_view(Noun(7)) == "a Nordic ruin",
          "places are named by kind");
    Check(IsPlace(6) && !IsPlace(61) && !IsPlace(64) && !IsPlace(0), "doors, quest and player markers are not places");
    Check(std::string_view(Bearing({0, 1})) == "north" && std::string_view(Bearing({1, 1})) == "north-east" &&
              std::string_view(Bearing({-1, 0})) == "west" && std::string_view(Bearing({0, -1})) == "south",
          "compass bearings, north up");
    std::cout << "Pathfinder checks passed: " << checks << '\n';
    return 0;
}

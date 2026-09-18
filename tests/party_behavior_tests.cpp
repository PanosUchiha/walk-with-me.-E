#include "dialogue_guard.h"
#include "hud_notice.h"
#include "party_behavior.h"
#include "party_life.h"
#include "party_orders.h"
#include "rest_activities.h"
#include "rest_pose.h"
#include "rest_recovery.h"
#include "social_planner.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
using namespace Wayfarer;
int checks = 0;
void Check(bool ok, const char* why)
{
    ++checks;
    if (!ok) {
        std::cerr << why << '\n';
        std::exit(1);
    }
}
void Wait(SandboxState& state, float seconds, bool explicitOrder = false, const SandboxTuning& tuning = {})
{
    for (int i = 0; i < static_cast<int>(seconds * 4); ++i) {
        state.Update({0, 0}, 0, .25F, explicitOrder, 8, tuning);
    }
}
int main()
{
    {
        SandboxTuning tavern;
        Check(ActivityRadius(RestContext::Tavern, 180, tavern) == 1000,
              "temporary tavern rest can reach room activities immediately");
        Check(ActivityRadius(RestContext::Wilderness, 180, tavern) == 180,
              "wilderness keeps its nearby phase");
        Check(ActivityRadius(RestContext::Interior, 180, tavern) == 1000,
              "homes and other safe interiors can use activity markers immediately");
        Check(ActivityRadius(RestContext::Settlement, 180, tavern) == 1000,
              "outdoor towns use the same activity range");
        Check(ActivityRadius(RestContext::Dungeon, 180, tavern) == 180,
              "dungeon stays local before the gate");
        tavern.settledActivityRadius = 650;
        Check(ActivityRadius(RestContext::Tavern, 180, tavern) == 650,
              "tavern activity range is configurable");
        Check(ActivityRadius(RestContext::Tavern, 1000, tavern) == 1000,
              "explicit Relax retains full free-roam range");
        tavern.maxRadius = 500;
        Check(ActivityRadius(RestContext::Tavern, 180, tavern) == 500,
              "tavern search respects the overall activity cap");
        Check(SettledRefreshments(RestContext::Tavern, tavern) && !tavern.extraPoses,
              "tavern meals do not need extra scripted poses");
        Check(!SettledRefreshments(RestContext::Dungeon, tavern), "no tavern refreshments in dungeons");
        tavern.meals = false;
        Check(!SettledRefreshments(RestContext::Tavern, tavern), "meal switch disables tavern refreshments");
        int drinks = 0, eats = 0;
        for (unsigned i = 0; i < 100; ++i) {
            drinks += ChooseRefreshment(RestContext::Tavern, i) == RestActivity::Drink;
            eats += ChooseRefreshment(RestContext::Tavern, i) == RestActivity::Eat;
        }
        Check(drinks == 80 && eats == 20, "taverns favor drinking while retaining meals");
        for (auto context : {RestContext::Interior, RestContext::Settlement}) {
            drinks = eats = 0;
            for (unsigned i = 0; i < 100; ++i) {
                drinks += ChooseRefreshment(context, i) == RestActivity::Drink;
                eats += ChooseRefreshment(context, i) == RestActivity::Eat;
            }
            Check(eats == 80 && drinks == 20,
                  "ordinary interiors and town spaces favor food over tavern drinking");
            Check(SettledRefreshments(context, {}), "settled refreshments cover homes and towns");
        }
        Check(!SettledRefreshments(RestContext::Wilderness, {}),
              "new settled-place meals do not change wilderness behavior");
        ActivitySearch search;
        bool ready = false;
        for (int i = 0; i < 28; ++i) {
            ready |= search.Ready(.25F, true, 42);
        }
        Check(!ready, "native sandbox gets the first chance to choose an activity");
        for (int i = 0; i < 40; ++i) {
            ready |= search.Ready(.25F, true, 42);
        }
        Check(ready, "idle marker search starts without waiting for thirty-second expansion");
        Check(!search.Ready(.25F, false, 42) && search.quiet == 0,
              "furniture speech and animations cancel plain-standing time");
        search.Attempt(42);
        Check(search.cooldown >= 35 && search.cooldown <= 50,
              "failed and completed searches have a varied quiet interval");
        ready = false;
        for (int i = 0; i < 120; ++i) {
            ready |= search.Ready(.25F, true, 42);
        }
        Check(!ready, "failed search cannot retry continuously");
        for (int i = 0; i < 84; ++i) {
            ready |= search.Ready(.25F, true, 42);
        }
        Check(ready, "an idle companion eventually gets another search opportunity");
        search.Reset();
        Check(search.quiet == 0 && search.cooldown == 0, "travel clears leftover activity timers");
    }
    DialogueGuard dialogue;
    Check(!dialogue.Check(false, 100), "ordinary gameplay permits AI updates");
    Check(dialogue.Check(true, 200), "dialogue blocks package and gesture changes");
    Check(dialogue.Check(true, 10000), "dialogue remains protected however long the menu stays open");
    Check(dialogue.Check(false, 11499), "goodbye grace protects final response");
    Check(!dialogue.Check(false, 11500), "AI resumes after dialogue grace");
    Check(dialogue.Check(true, 12000) && dialogue.Check(true, 12500) && dialogue.Check(false, 13999),
          "reopened dialogue extends protection");
    dialogue = {};
    Check(!dialogue.Check(false, 100), "load reset clears dialogue protection");
    bool distant = false;
    for (float d : {400.0F, 700.0F, 899.0F}) {
        distant = DistantCatchup(distant, d, 900, 500);
        Check(!distant, "normal inside entry threshold");
    }
    distant = DistantCatchup(distant, 900, 900, 500);
    Check(distant, "enter distant boost at threshold");
    for (float d : {899.0F, 700.0F, 501.0F}) {
        distant = DistantCatchup(distant, d, 900, 500);
        Check(distant, "retain boost until return threshold");
    }
    distant = DistantCatchup(distant, 500, 900, 500);
    Check(!distant, "return to normal at threshold");
    Check(TravelSpeedTarget(1.1F, 1.5F, true, 2200, 1800, 900, 500) == 3,
          "distant speed doubles selected multiplier");
    Check(TravelSpeedTarget(1.1F, 1.5F, false, 700, 400, 900, 500) == 1.1F,
          "normal pace keeps existing taper");
    Check(TravelSpeedTarget(1, 1.75F, true, 2200, 1800, 900, 500) == 3.5F, "maximum selected cap doubles");
    SandboxState state;
    Wait(state, 7.75F);
    Check(!state.active, "ordinary stop waits for delay");
    Wait(state, .25F);
    Check(state.active && state.radius == 180, "sandbox starts small");
    Check(state.AllowsSocial({}) && !state.expanded, "nearby rest allows conversations before expansion");
    SandboxTuning silent;
    silent.social = false;
    Check(!state.AllowsSocial(silent), "social toggle still disables nearby conversations");
    SandboxState inactive;
    Check(!inactive.AllowsSocial({}), "travel cannot start a rest conversation");
    Wait(state, 10);
    Check(state.radius == 180 && !state.expanded, "ten seconds does not expand or restart rest");
    Wait(state, 19.75F);
    Check(state.radius == 180 && !state.expanded, "nearby phase lasts the full thirty seconds");
    Wait(state, .25F);
    Check(state.radius == 1000 && state.expanded, "one gate opens the full native sandbox radius");
    Wait(state, 1000);
    Check(state.active && state.radius == 1000 && state.expanded, "no repeated growth stages after opening");
    state.Update({200, 0}, 100, .25F, false, 8, {});
    Check(state.active && state.center.x == 0, "walking to nearby NPC retains temporary rest anchor");
    state.Update({250, 0}, 0, .25F, false, 8, {});
    Check(state.active, "rest continues at resume boundary");
    state.Update({251, 0}, 100, .25F, false, 8, {});
    Check(!state.active, "leaving resume area ends temporary sandbox");
    Wait(state, 1);
    Check(!state.active, "does not instantly restart after leaving rest area");
    state.Reset();
    Wait(state, .25F, true);
    Check(state.active && state.radius == 1000 && state.expanded,
          "explicit Relax immediately uses the full activity area");
    Wait(state, 30, true);
    Check(state.radius == 1000 && state.expanded, "explicit Relax remains in the full area");
    state.Update({5000, 0}, 300, .25F, true, 8, {});
    Check(state.active && state.radius == 1000 && state.center.x == 0,
          "Relax neither follows nor shrinks when player leaves");
    SandboxTuning off;
    off.automatic = false;
    state.Reset();
    Wait(state, 100, false, off);
    Check(!state.active, "automatic sandbox can be disabled");
    Wait(state, .25F, true, off);
    Check(state.active, "explicit order works with automatic relaxation off");
    state.Reset();
    Check(!state.active && !state.initialized, "load reset clears runtime sandbox");
    state.Update({0, 0}, 0, .25F, false, 8, {});
    state.Update({60, 0}, 0, .25F, false, 8, {});
    Check(state.stationaryTime == 0, "displacement resets stationary timer even with zero reported speed");
    float last = 3.5F;
    for (float distance = 2200; distance >= 500; distance -= 10) {
        const float scale = TravelSpeedTarget(1, 1.75F, true, distance, 1000, 900, 500);
        Check(scale <= last + .00001F && scale >= 1, "extra boost brakes monotonically on approach");
        last = scale;
    }
    Check(last == 1, "extra boost is completely gone at near threshold");
    Check(TravelSpeedTarget(1.1F, 1.75F, true, 2200, 200, 900, 500) == 1.1F,
          "formation arrival brakes even when player is far away");
    Check(TravelSpeedTarget(1, 1.75F, true, 700, 700, 900, 500) < 1.2F,
          "near party does not retain doubled speed");
    int drinks = 0, eats = 0, examine = 0, ground = 0, handsBack = 0;
    for (unsigned roll = 0; roll < 1000; ++roll) {
        auto dungeon = ChooseRestActivity(RestContext::Dungeon, roll, {});
        Check(dungeon != RestActivity::Eat && dungeon != RestActivity::Drink &&
                  dungeon != RestActivity::Ground && dungeon != RestActivity::Read,
              "dungeons select alert activities");
        examine += dungeon == RestActivity::Examine;
        handsBack += ChooseRestActivity(RestContext::Wilderness, roll, {}) == RestActivity::Stand;
        auto tavern = ChooseRestActivity(RestContext::Tavern, roll, {});
        Check(tavern != RestActivity::Ground, "no ground seats indoors");
        drinks += tavern == RestActivity::Drink;
        eats += tavern == RestActivity::Eat;
        ground += ChooseRestActivity(RestContext::Wilderness, roll, {}) == RestActivity::Ground;
        for (auto context : {RestContext::Interior, RestContext::Settlement}) {
            Check(ChooseRestActivity(context, roll, {}) != RestActivity::Ground,
                  "floor sitting excluded in settled locations");
        }
    }
    Check(handsBack <= 32, "hands-behind-back is at most one opportunity in 32");
    Check(drinks > eats && ground > 100 && examine > 500,
          "inn drinking, wilderness ground rests and dungeon curiosity have meaningful weights");
    SandboxTuning activitiesOff;
    activitiesOff.meals = activitiesOff.reading = activitiesOff.stretching = activitiesOff.groundSitting =
        false;
    for (unsigned roll = 0; roll < 100; ++roll) {
        for (auto context :
             {RestContext::Wilderness, RestContext::Tavern, RestContext::Dungeon, RestContext::Interior}) {
            const auto activity = ChooseRestActivity(context, roll, activitiesOff);
            Check(activity == RestActivity::Stand || activity == RestActivity::Examine,
                  "activity toggles remove disabled choices");
        }
    }
    Check(!MealsAllowed(RestContext::Dungeon, {}) && MealsAllowed(RestContext::Tavern, {}),
          "native meals follow the same context policy");
    Check(!SandboxTuning{}.extraPoses, "native sandbox activities are the default");
    SandboxTuning gate;
    gate.startRadius = 150;
    gate.maxRadius = 850;
    gate.fullSandboxAfter = 12;
    state.Reset();
    Wait(state, 8, false, gate);
    Wait(state, 11.75F, false, gate);
    Check(state.radius == 150 && !state.expanded, "configured initial radius and delay apply");
    Wait(state, .25F, false, gate);
    Check(state.radius == 850 && state.expanded, "configured larger radius opens at exact gate");
    gate.fullSandboxAfter = 120;
    Wait(state, 1, false, gate);
    Check(state.expanded && state.radius == 850,
          "opened gate never returns to a prior phase during the same rest");
    state.Reset();
    gate.fullSandboxAfter = 0;
    Wait(state, 8, false, gate);
    Check(state.expanded && state.radius == 850, "zero delay starts directly with standard sandbox");
    state.Reset();
    Wait(state, 8);
    Wait(state, 15);
    state.Update({400, 0}, 100, .25F, false, 8, {});
    Check(!state.active && !state.expanded, "leaving automatic rest resets the gate");
    for (int i = 0; i < 32; ++i) {
        state.Update({400, 0}, 0, .25F, false, 8, {});
    }
    Check(state.active && state.radius == 180 && !state.expanded && state.center.x == 400,
          "a fresh stop begins at the small radius again");

    state.Reset();
    Wait(state, 8);
    Wait(state, 5);
    state.Update({120, 0}, 60, .25F, true, 8, {});
    Check(state.expanded && state.radius == 1000 && state.center.x == 0,
          "Relax opens an existing nearby stop without moving its anchor");
    SandboxTuning custom;
    custom.startRadius = 80;
    custom.maxRadius = 700;
    state.Reset();
    Wait(state, 8, false, custom);
    Check(state.radius == 80, "custom small radius remains configurable");
    Wait(state, 30, false, custom);
    Check(state.expanded && state.radius == 700, "custom radius advances after thirty unpaused seconds");
    SocialPlanner social;
    RestRecovery waiting;
    for (int tick = 0; tick < 120; ++tick) {
        Check(!waiting.Ready(.25F, true, false, 45, 123), "recovery never starts before the free-roam gate");
    }
    bool gateReady = false;
    for (int tick = 0; tick < 121; ++tick) {
        gateReady |= waiting.Ready(.25F, true, true, 45, 123);
    }
    Check(gateReady, "quiet time before expansion counts toward recovery, avoiding another full delay");
    for (std::uint32_t id = 1; id <= 10; ++id) {
        RestRecovery recovery;
        for (int tick = 0; tick < 179; ++tick) {
            Check(!recovery.Ready(.25F, true, true, 45, id),
                  "recovery preserves at least forty-five seconds of quiet rest");
        }
        bool ready = false;
        for (int tick = 0; tick < 62; ++tick) {
            ready |= recovery.Ready(.25F, true, true, 45, id);
        }
        Check(ready, "plain standing becomes eligible within sixty seconds");
        Check(!recovery.Ready(.25F, false, true, 45, id) && recovery.quiet == 0,
              "an actual activity resets the inactivity timer");
        Check(!recovery.Ready(.25F, true, false, 45, id), "nearby rest never forces a recovery excursion");
        Check(!recovery.Ready(.25F, true, true, 0, id), "zero delay disables recovery");
        recovery.Begin();
        for (int tick = 0; tick < 79; ++tick) {
            Check(!recovery.Done(.25F, false, false), "native excursion gets time to complete");
        }
        Check(recovery.Done(.25F, false, false), "blocked excursion always yields after twenty seconds");
        recovery.Finish();
        Check(!recovery.walking && recovery.quiet == 0 && recovery.sequence == 1,
              "ending excursion starts another full quiet interval");
        recovery.Begin();
        Check(recovery.Done(.25F, true, false), "arrival immediately returns to native sandbox");
        recovery.Begin();
        Check(recovery.Done(.25F, false, true), "furniture or conversation interrupts recovery");
        recovery.Reset();
        Check(!recovery.walking && recovery.quiet == 0, "rest ending clears recovery state");
    }
    auto begin = [&](const std::vector<SocialCandidate>& candidates) {
        for (int i = 0; i < 30 && !social.count; ++i) {
            social.cooldown = 0;
            social.Update(.25F, candidates);
        }
    };
    std::vector<SocialCandidate> nearby{{1, {0, 0}},    {2, {80, 0}},    {3, {160, 0}},
                                        {4, {100, 80}}, {5, {100, 100}}, {6, {200, 0}}};
    Check(!social.Update(2.5F, nearby) && !social.count,
          "initial pause before optional conversation gestures");
    begin(nearby);
    Check(social.count == 2, "six followers only select a one-to-one conversation");
    auto speaker = social.Speaker();
    auto listener = social.Listener(speaker);
    Check(speaker && listener && speaker != listener, "each participant addresses only its partner");
    int unselected = 0;
    for (auto& candidate : nearby) {
        unselected += social.Index(candidate.id) < 0;
    }
    Check(unselected == 4, "other four companions remain uninvolved");
    social.Update(5, nearby);
    Check(social.Speaker() == listener, "the pair exchanges turns");
    social.Update(40, nearby);
    Check(!social.count && social.cooldown >= 20, "conversation ends and leaves a quiet interval");
    social.Reset();
    begin({{1, {0, 0}}, {2, {1000, 0}}});
    Check(!social.count, "no movement orders to pull distant companions together");
    social.Reset();
    begin({{1, {0, 0}, false}, {2, {80, 0}, false}});
    Check(!social.count, "walking actors are not stopped for gestures");
    social.Reset();
    begin({{1, {0, 0}, true, 1}, {2, {80, 0}, true, 2}});
    Check(!social.count, "pairs cannot cross travel spaces");
    social.Reset();
    nearby = {{1, {0, 0}}, {2, {80, 0}}};
    begin(nearby);
    nearby[1].settled = false;
    social.Update(1, nearby);
    Check(social.count == 2, "brief incidental movement gets grace");
    social.Update(.6F, nearby);
    Check(!social.count, "native wandering ends the gesture pair without a holding package");
    social.Reset();
    nearby[1].settled = true;
    begin(nearby);
    nearby[1].position.x = 501;
    social.Update(.1F, nearby);
    Check(!social.count, "departing companion ends the pair");
    social.Reset();
    nearby[1].position.x = 80;
    begin(nearby);
    nearby.clear();
    social.Update(.1F, nearby);
    Check(!social.count, "unavailable participants release the conversation");
    HudNotice notice;
    std::set<int> traits;
    for (int i = 0; i < 100; ++i) {
        auto key = std::string("companion.esp|") + std::to_string(i);
        auto trait = ResolvePersonality(key, -1, true);
        traits.insert(static_cast<int>(trait));
        Check(ResolvePersonality(key, -1, true) == trait,
              "automatic personalities remain stable for a saved identity");
        Check(ResolvePersonality(key, 3, true) == Personality::Reserved,
              "manual override replaces the automatic personality");
        Check(ResolvePersonality(key, 3, false) == Personality::Balanced,
              "global off restores neutral preferences without discarding overrides");
    }
    Check(traits.size() == 4, "automatic assignments include all four tendencies");
    Check(SocialWeight(Personality::Reserved) > 0 &&
              SocialWeight(Personality::Sociable) > SocialWeight(Personality::Reserved),
          "reserved companions can socialize while sociable companions favor it");
    Check(RecoveryPreference(Personality::Curious) < RecoveryPreference(Personality::Reserved),
          "curious companions explore sooner than reserved companions");
    Check(WatchWeight(Personality::Watchful) > WatchWeight(Personality::Sociable),
          "watchful personality increases lookout preference");
    LookoutState watch;
    std::vector<WatchCandidate> watchers{{1, 4}, {2, 2}, {3, 1}};
    Check(!LookoutPermitted(true, true, true, true, 2, false),
          "two-companion parties keep both members free for conversation");
    Check(LookoutPermitted(true, true, true, true, 3, false),
          "three-companion resting parties may have one lookout");
    Check(!LookoutPermitted(true, true, true, false, 6, false) &&
              !LookoutPermitted(true, true, true, true, 6, true),
          "taverns, homes and combat never start a watch");
    watch.Update(.25F, true, 59, 60, watchers);
    Check(!watch.actor, "short rests do not start a lookout");
    watch.Update(.25F, true, 60, 60, watchers);
    const auto firstWatch = watch.actor;
    Check(firstWatch && watch.duration >= 30 && watch.duration <= 45,
          "long rest chooses one bounded lookout");
    watch.Update(29, true, 90, 60, watchers);
    Check(watch.actor == firstWatch, "lookout does not switch companions during a watch");
    watch.Update(16, true, 106, 60, watchers);
    Check(!watch.actor && watch.cooldown >= 90, "watch ends with a long quiet interval");
    for (int i = 0; i < 601 && !watch.actor; ++i) {
        watch.Update(.25F, true, 300, 60, watchers);
    }
    Check(watch.actor && watch.actor != firstWatch, "next watch rotates when another companion is available");
    watch.Update(.25F, false, 300, 60, watchers);
    Check(!watch.actor, "travel or disabled lookout releases its hold immediately");
    watch.Reset();
    watch.Update(.25F, true, 100, 60, {});
    Check(!watch.actor, "no free candidate means no lookout");
    watch.Update(.25F, true, 100, 60, watchers);
    watch.Update(.25F, true, 100, 60, {});
    Check(!watch.actor, "busy or departed lookout releases immediately");
    for (Vec2 a : {Vec2{0, 0}, Vec2{0, 100}, Vec2{30, 200}, Vec2{-20, 100}}) {
        Check(IntrudesConversation(a, {0, 0}, {0, 200}, 150),
              "conversation space covers both participants and their connecting path");
        auto exit = ConversationExit(a, {0, 0}, {0, 200}, 150, 3);
        Check(!IntrudesConversation(exit, {0, 0}, {0, 200}, 190),
              "step-aside target clears the protected corridor with padding");
        if (a.x != 0) {
            Check(a.x * exit.x > 0,
                  "companion steps to its existing side instead of crossing the conversation");
        }
    }
    Check(!IntrudesConversation({400, 100}, {0, 0}, {0, 200}, 150),
          "companions already giving space are unaffected");
    auto samePoint = ConversationExit({0, 0}, {0, 0}, {0, 0}, 150, 2);
    Check(std::isfinite(samePoint.x) && std::isfinite(samePoint.y) && Length(samePoint) > 150,
          "overlapping conversation positions remain well-defined");
    for (unsigned id = 1; id <= 100; ++id) {
        SocialPlanner pair;
        pair.Update(3, {{id, {0, 0}}, {id + 1000, {400, 0}}});
        Check(pair.count == 2, "exactly two eligible companions reliably start their first conversation");
        const auto first = pair.Speaker(), second = pair.Listener(first);
        pair.Update(5, {{id, {0, 0}}, {id + 1000, {400, 0}}});
        Check(pair.TakeGesture(first) && pair.TakeGesture(second),
              "slow script delivery preserves both participants queued gestures");
        Check(!pair.TakeGesture(first), "queued gesture is consumed only once");
        pair.Cancel();
        Check(!pair.TakeGesture(second), "cancelled pair cannot deliver stale gestures");
    }
    SocialPlanner visibility;
    visibility.Update(3, {{1, {0, 0}}, {2, {50, 0}}, {3, {150, 0}}},
                      [](auto a, auto b) { return (a == 1 && b == 3) || (a == 3 && b == 1); });
    Check(visibility.Listener(1) == 3, "blocked nearest partner does not starve a visible alternative");
    visibility.Reset();
    visibility.Update(3, {{1, {0, 0}}, {2, {80, 0}}}, [](auto, auto) { return false; });
    Check(!visibility.count, "invisible companions are never paired through walls");
    for (int i = 0; i < 6; ++i) {
        Check(static_cast<int>(OrderMode(i)) == i,
              "all six displayed options map to their matching formation");
        Check(OrderSelection(i, 4, 0, true) == i,
              "number shortcut cannot be overwritten by another activation");
        Check(OrderSelection(-1, i, 0, true) == i,
              "mouse choice takes priority over incidental keyboard activation");
        Check(OrderSelection(-1, -1, i, true) == i, "confirmation chooses the explicitly highlighted row");
    }
    Check(OrderSelection(-1, -1, 2, false) == -1, "hover alone never issues an order");
    RegroupState regroup;
    regroup.Begin();
    TravelIntent gathering;
    TravelTuning travel;
    travel.idleRelease = 4;
    gathering.BeginOrder({0, 0}, {0, 1});
    SandboxState resting;
    SandboxTuning rest;
    for (int tick = 0; tick < 48; ++tick) {
        gathering.MaintainOrder();
        gathering.Update({0, 0}, {0, 0}, .25F, travel);
        regroup.Tick(.25F, false, false);
        rest.automatic = !regroup.active;
        resting.Update({0, 0}, 0, .25F, false, 4, rest);
        Check(gathering.CanTravel() && !resting.active,
              "stationary regroup survives the ordinary four-second automatic rest delay");
    }
    Check(regroup.Tick(.25F, false, true) && !regroup.active,
          "gathered party releases the temporary regroup priority");
    regroup.Begin();
    Check(regroup.Tick(.25F, true, false), "moving resumes ordinary travel immediately");
    regroup.Begin();
    for (int i = 0; i < 180; ++i) {
        regroup.Tick(.25F, false, false);
    }
    Check(!regroup.active, "unreachable followers cannot keep regroup priority forever");
    notice.Update(1, .016F);
    Check(notice.Alpha() == 1 && notice.Slide() == 0, "new HUD notice starts fully visible");
    notice.Update(1, 5);
    Check(notice.Alpha() == 1 && notice.Slide() == 0, "HUD holds for five seconds");
    notice.Update(1, 1);
    Check(notice.Alpha() == .5F && notice.Slide() == 40, "HUD fades and slides together");
    notice.Update(1, 1);
    Check(notice.Alpha() == 0 && notice.Slide() == 80, "HUD disappears after two-second transition");
    notice.Update(1, 30);
    Check(notice.Alpha() == 0, "unchanged party state does not repeat HUD");
    notice.Update(2, .016F);
    Check(notice.Alpha() == 1 && notice.Slide() == 0, "changed party state restarts HUD");
    notice.Update(2, 6);
    notice.Update(2, 0, true);
    Check(notice.Alpha() == 1, "leaving settings preview restarts notice");
    float firstDuration = 0;
    bool varied = false;
    for (std::uint32_t id = 1; id <= 10; ++id) {
        RestPose pose;
        pose.Init(id);
        Check(pose.cooldown >= 12 && pose.cooldown <= 24, "rest starts are staggered");
        pose.Begin(2, id);
        Check(pose.duration >= 25 && pose.duration <= 45, "rest pose lasts 25-45 seconds");
        if (id == 1) {
            firstDuration = pose.duration;
        }
        else {
            varied |= firstDuration != pose.duration;
        }
        Check(!pose.Tick(5, false) && !pose.started, "entry time does not consume pose hold");
        Check(!pose.Tick(.25F, true) && pose.started && pose.elapsed == 0, "hold starts after seat entry");
        Check(!pose.Tick(pose.duration - 1, true), "pose holds through its selected interval");
        Check(pose.Tick(1, true), "pose finishes at duration");
        pose.End(id);
        Check(pose.kind == 0 && pose.cooldown >= 25 && pose.cooldown <= 45,
              "quiet interval after a held pose");
        pose.Begin(1, id);
        Check(pose.Tick(12, false), "unavailable pose times out rather than freezing actor");
        pose.End(id);
        pose.Begin(2, id);
        pose.Tick(.1F, true);
        Check(pose.Tick(.1F, false), "interrupted seat releases pose");
    }
    Check(varied, "companions do not share identical pose durations");
    std::cout << checks << " behavior checks passed\n";
}

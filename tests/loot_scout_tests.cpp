#include "loot_scout_policy.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace Wayfarer::Loot;
void Check(bool v, const char* message)
{
    if (!v) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
Item Stack(int value, int count = 1)
{
    Item item;
    item.value = value;
    item.count = count;
    item.playable = item.named = true;
    return item;
}
// Walks a fresh visit up to the start of its search.
Visit Kneeling(float searchSeconds = 12)
{
    Visit visit;
    visit.Start(false);
    visit.Tick(.1F, -5, 400, 1500, false, searchSeconds);
    visit.Tick(.1F, -5, 400, 1500, true, searchSeconds);
    return visit;
}
int main()
{
    Check(!Valuable(1000, 1000) && Valuable(1001, 1000), "valuable means worth more than the threshold");

    Contents chest;
    chest.Add(Stack(1200), 1000);
    Check(chest.Worthwhile() && chest.valuable && chest.bestValue == 1200 && chest.total == 1200,
          "an expensive item makes a gold find");
    Contents plain;
    plain.Add(Stack(25), 1000);
    Check(plain.Worthwhile() && !plain.valuable, "ordinary loot is still worth pointing out");

    Contents food;
    auto cabbage = Stack(2, 12);
    cabbage.provisions = true;
    food.Add(cabbage, 1000);
    Check(!food.Worthwhile() && food.total == 24, "a sack of food is not a find");
    auto heart = Stack(2500);
    heart.provisions = true;
    food.Add(heart, 1000);
    Check(food.Worthwhile() && food.valuable, "a valuable ingredient still counts");

    Contents skin;
    auto hidden = Stack(3000);
    hidden.playable = false;
    skin.Add(hidden, 1000);
    auto unnamed = Stack(3000);
    unnamed.named = false;
    skin.Add(unnamed, 1000);
    Check(!skin.Worthwhile() && !skin.valuable && skin.total == 0,
          "non-playable and unnamed items are ignored");

    Contents purse;
    auto septims = Stack(1, 1200);
    septims.coins = true;
    purse.Add(septims, 1000);
    Check(purse.valuable && purse.bestValue == 1200 && purse.total == 1200,
          "a pile of septims is judged by its total");
    Contents arrows;
    arrows.Add(Stack(5, 400), 1000);
    Check(!arrows.valuable && arrows.total == 2000, "other stacks are judged per item but add up");

    Check(!WorthPointingOut(Kind::Item, plain), "loose clutter is ignored");
    Check(WorthPointingOut(Kind::Item, chest), "loose valuables are pointed out");
    Check(WorthPointingOut(Kind::Container, plain) && WorthPointingOut(Kind::Body, plain),
          "containers and bodies with ordinary loot are pointed out");

    Check(Richer(arrows, 900, chest, 200), "the find holding more in total wins, even further away");
    Check(Richer(chest, 900, purse, 200) == false && Richer(purse, 200, chest, 900),
          "equal totals: the nearer find wins");
    Contents gem;
    gem.Add(Stack(1200), 1000);
    Contents trinkets;
    trinkets.Add(Stack(600, 2), 1000);
    Check(Richer(gem, 500, trinkets, 100), "equal totals: the better single item wins before distance");

    Tuning tuning;
    Check(tuning.searchSeconds >= 10 && tuning.searchSeconds <= 15,
          "the default search gives the player time");
    Check(InReach({600, 500, 20}, tuning), "a visible chest across the room qualifies");
    Check(!InReach({1100, 500, 20}, tuning), "beyond the follower's notice radius");
    Check(!InReach({600, 100, 20}, tuning), "the player is already standing at it");
    Check(!InReach({600, 1300, 20}, tuning), "too close to the leave distance to be worth starting");
    Check(!InReach({600, 500, 260}, tuning), "another floor or a ledge");
    Check(!InReach({std::numeric_limits<float>::quiet_NaN(), 500, 0}, tuning), "bad positions never qualify");

    Check(MayScout("", "skyrim.esm|01a694") && MayScout("", ""), "anyone may scout without a finder");
    Check(MayScout("katana.esp|000d62", "katana.esp|000d62"), "the designated finder scouts");
    Check(!MayScout("katana.esp|000d62", "skyrim.esm|01a694"), "other companions stay in formation");
    Check(!MayScout("katana.esp|000d62", ""), "a temporary actor never matches a finder");

    Route unknown;
    Check(!unknown.known && !unknown.Disconnected() && Walkable(unknown, 600, tuning),
          "no navmesh to judge by: leave it to the stall check");
    Route walledOff;
    walledOff.known = true;
    Check(walledOff.Disconnected() && !Walkable(walledOff, 600, tuning), "no way round: never tried");
    Check(Walkable({true, 700}, 600, tuning), "a direct walk is fine");
    Check(Walkable({true, 1100}, 600, tuning), "a modest detour round a table is fine");
    Check(!Walkable({true, 1400}, 150, tuning),
          "a chest just behind a wall, reached via the next room, is skipped");
    Check(!Walkable({true, 1600}, 900, tuning), "too long a walk even when the way is fairly direct");

    Survey survey;
    Check(!survey.Update(1, true), "the first sighting does not choose yet");
    Check(survey.Update(1, true), "after a moment of looking, the finder chooses");
    Survey interrupted;
    interrupted.Update(1, true);
    Check(!interrupted.Update(1, false) && !interrupted.Update(1, true), "losing sight restarts the look");

    Area area;
    Check(!area.Holds(0, 0, 2000), "no find yet: free to search");
    area.Finish(100, 100);
    Check(area.Holds(100, 100, 2000) && area.Holds(1500, 100, 2000), "after one find, no more nearby");
    Check(!area.Holds(2200, 100, 2000) && !area.done, "moving on opens a new area");
    Check(!area.Holds(100, 100, 2000), "and it stays open until the next find");
    area.Finish(0, 0);
    Check(!area.Holds(std::numeric_limits<float>::quiet_NaN(), 0, 2000),
          "a bad position never locks scouting");

    Visit visit;
    Check(!visit.Active() && visit.Tick(.1F, 0, 0, 1500, false, 12) == End::None, "idle visits do nothing");
    visit.Start(true);
    Check(visit.Active() && visit.valuable && visit.phase == Phase::Approach && !visit.reached,
          "a visit starts by walking over");
    for (int i = 0; i < 30; ++i) {
        Check(visit.Tick(.1F, 500 - i * 15.0F, 400, 1500, false, 12) == End::None,
              "steady progress continues");
    }
    Check(visit.Tick(.1F, -3, 400, 1500, false, 12) == End::None && visit.phase == Phase::Turn &&
              visit.reached,
          "arrival within arm's length turns to face it");
    Check(visit.Tick(.1F, -3, 400, 1500, true, 12) == End::None && visit.phase == Phase::Search,
          "facing it starts the search");
    Check(visit.ConsumeSearch() && !visit.ConsumeSearch(), "the search animation starts once");
    for (int i = 0; i < 119; ++i) {
        Check(visit.Tick(.1F, -3, 1400, 1500, false, 12) == End::None && visit.phase == Phase::Search,
              "keeps kneeling for the whole search time");
    }
    visit.Tick(.2F, -3, 400, 1500, false, 12);
    Check(visit.phase == Phase::Rise && visit.ConsumeRise() && !visit.ConsumeRise(),
          "stands up once the search time is over");
    for (int i = 0; i < 23; ++i) {
        visit.Tick(.1F, -3, 400, 1500, false, 12);
    }
    Check(visit.phase == Phase::Present, "then waits beside the find");
    for (int i = 0; i < 100; ++i) {
        Check(visit.Tick(.1F, -3, 1400, 1500, false, 12) == End::None, "waits while the player stays near");
    }
    Check(visit.Tick(.1F, -3, 1501, 1500, false, 12) == End::PlayerLeft, "leaves once the player moves on");

    // Rumarin reached and searched a chest, then remained parked while it was unopened.
    // Approach/search time must not shorten the chance to notice it after he stands.
    auto unopened = Kneeling(30);
    unopened.Tick(30, -3, 400, 1500, false, 30);
    Check(unopened.phase == Phase::Rise, "a long search still gets its stand-up");
    unopened.Tick(riseSeconds, -3, 400, 1500, false, 30);
    Check(unopened.phase == Phase::Present && unopened.phaseTime == 0,
          "the wait limit starts after standing, not at the start of scouting");
    Check(unopened.Tick(presentSeconds - .25F, -3, 400, 1500, false, 30) == End::None,
          "an unopened find stays indicated for the promised wait");
    Check(unopened.Tick(0, -3, 400, 1500, false, 30) == End::None &&
              unopened.phaseTime == presentSeconds - .25F,
          "no simulation time means no progress toward the wait limit");
    Check(unopened.Tick(.25F, -3, 400, 1500, false, 30) == End::Presented,
          "an ignored unopened find releases the follower after fifteen seconds");
    unopened.Stop();
    Check(!unopened.Active(), "the completed visit stops owning the follower");
    unopened.Start(false);
    Check(unopened.phaseTime == 0 && unopened.pendingEnd == End::None,
          "a later visit does not inherit the old timeout");

    for (const auto reason : {End::Claimed, End::Emptied, End::Lost, End::Released}) {
        auto indicated = Kneeling();
        indicated.Tick(12, -3, 400, 1500, false, 12);
        indicated.Tick(riseSeconds, -3, 400, 1500, false, 12);
        Check(indicated.Tick(presentSeconds, -3, 400, 1500, false, 12, reason) == reason,
              "opening, emptying or losing the find takes precedence over the wait limit");
    }

    auto opened = Kneeling();
    Check(opened.phase == Phase::Search, "helper reaches the search");
    Check(opened.Tick(.1F, -3, 100, 1500, false, 12, End::Claimed) == End::None &&
              opened.phase == Phase::Rise,
          "opening the find stands the follower up first");
    Check(opened.ConsumeRise(), "the stand-up plays");
    auto result = End::None;
    for (int i = 0; i < 30 && result == End::None; ++i) {
        result = opened.Tick(.1F, -3, 100, 1500, false, 12);
    }
    Check(result == End::Claimed && opened.elapsed >= riseSeconds, "then leaves with the original reason");

    auto walkedOff = Kneeling();
    Check(walkedOff.Tick(.1F, -3, 1600, 1500, false, 12) == End::None && walkedOff.phase == Phase::Rise,
          "walking away also waits for the stand-up");
    result = End::None;
    for (int i = 0; i < 30 && result == End::None; ++i) {
        result = walkedOff.Tick(.1F, -3, 1700, 1500, false, 12, End::Emptied);
    }
    Check(result == End::PlayerLeft, "the first reason is kept");

    Visit early;
    early.Start(false);
    Check(early.Tick(.1F, 300, 400, 1500, false, 12, End::Claimed) == End::Claimed,
          "a visit that has not reached the find ends at once");

    Visit stuck;
    stuck.Start(false);
    auto end = End::None;
    for (int i = 0; i < 60 && end == End::None; ++i) {
        end = stuck.Tick(.1F, 500, 400, 1500, false, 12);
    }
    Check(end == End::Stalled && stuck.elapsed <= stallSeconds + .25F && !stuck.reached,
          "no progress gives up quickly");
    Visit slow;
    slow.Start(false);
    end = End::None;
    for (int i = 0; i < 400 && end == End::None; ++i) {
        end = slow.Tick(.1F, 5000 - i * 10.0F, 400, 1500, false, 12);
    }
    Check(end == End::Stalled && slow.elapsed >= approachLimit - .15F, "a very long approach times out");
    Visit turning;
    turning.Start(false);
    turning.Tick(.1F, -1, 400, 1500, false, 12);
    for (int i = 0; i < 13; ++i) {
        turning.Tick(.1F, -1, 400, 1500, false, 12);
    }
    Check(turning.phase == Phase::Search, "a blocked turn still searches after a moment");
    Visit broken;
    broken.Start(false);
    Check(broken.Tick(.1F, std::numeric_limits<float>::infinity(), 400, 1500, false, 12) == End::Lost,
          "invalid geometry ends the visit");

    Check(Cooldown(End::Stalled) > Cooldown(End::PlayerLeft), "unreachable finds back off longer");
    Check(IconAlpha(0) == 0 && IconAlpha(.35F) == 1 && IconAlpha(10) == 1, "the icon fades in");
    std::cout << "Loot scouting tests passed\n";
    return 0;
}

#include "action_grid.h"
#include "follower_catchup.h"
#include "order_wheel.h"
#include "party_recall.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace Wayfarer;
void Check(bool v, const char* message)
{
    if (!v) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
int main()
{
    PartyRecallRequest request;
    Check(request.Request(4, 1000), "explicit teleport can be queued");
    Check(!request.Request(4, 1001), "double click cannot queue a duplicate teleport");
    Check(request.Poll(4, 1100, true, true) == RecallDispatch::Wait, "teleport waits for the menu to close");
    Check(request.Poll(4, 1200, false, true) == RecallDispatch::Run, "teleport runs after menu close");
    Check(request.Poll(4, 1201, false, true) == RecallDispatch::None, "teleport only executes once");
    request.Request(4, 2000);
    Check(request.Poll(5, 2100, false, true) == RecallDispatch::Cancel,
          "save change cancels queued teleport");
    request.Request(5, 3000);
    Check(request.Poll(5, 8001, true, true) == RecallDispatch::Cancel,
          "a blocked request expires instead of teleporting much later");
    request.Request(5, 9000);
    Check(request.Poll(5, 9100, false, false) == RecallDispatch::Cancel,
          "combat or unsafe player state cancels the request");
    for (auto screen : {std::pair{1280.0F, 720.0F}, std::pair{1920.0F, 1080.0F}, std::pair{3440.0F, 1440.0F},
                        std::pair{3840.0F, 2160.0F}}) {
        const float width = Wheel::Width(screen.first, screen.second);
        const auto left = Wheel::UtilityBox(0, width), right = Wheel::UtilityBox(1, width);
        Check(width < screen.first && width * Wheel::windowHeight < screen.second,
              "complete menu stays on screen");
        Check(left.y > width * .91F && left.x + left.w < right.x,
              "utility cards cannot overlap radial mouse target or each other");
    }
    FollowerCatchup c;
    for (int i = 0; i < 19; ++i) {
        Check(!c.Update(.1F, true, 4500, 4000), "must remain distant for two seconds");
    }
    Check(c.Update(.11F, true, 4500, 4000), "sustained separation recalls");
    c.Attempt(true);
    for (int i = 0; i < 79; ++i) {
        Check(!c.Update(.1F, true, 4500, 4000), "cooldown prevents repeated recalls");
    }
    Check(c.Update(.2F, true, 4500, 4000), "recall available after cooldown");
    c.Attempt(false);
    Check(!c.Update(1, true, 4500, 4000), "failed landing backs off");
    Check(!c.Update(1, false, 4500, 4000), "unsafe state clears delay");
    Check(!c.Update(1, true, 4500, 4000), "resumption earns a fresh delay");
    Check(!c.Update(1, true, 3999, 4000), "returning within distance cancels recall");
    Check(!c.Update(1, true, std::numeric_limits<float>::quiet_NaN(), 4000), "bad position never recalls");
    Check(!c.Update(1, true, 4500, 4000), "bad position reset delay");
    Check(c.Update(1, true, 4500, 4000), "valid distance eventually recalls");

    FollowerCatchup away;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Check(!away.Update(1, true, nan, 4000, true), "another cell counts as distant");
    Check(!away.Update(1.9F, true, nan, 4000, true), "the engine gets three seconds to bring them through a door");
    Check(away.Update(.2F, true, nan, 4000, true), "a companion left in another cell is recalled");
    away.Attempt(true);
    Check(!away.Update(1, false, nan, 4000, true), "a held companion in another cell stays put");
    Check(CatchupDistance({0, 0, -1300}, {0, 0, 0}) >= 2463, "a long drop counts as distance");
    Check(std::abs(CatchupDistance({300, 400, 0}, {0, 0, 0}) - 500) < .01F, "level ground is plain distance");

    PlayerTrail trail;
    for (int i = 0; i <= 40; ++i) {
        trail.Record({i * 25.0F, 0, 0}, 7, true);
    }
    Check(trail.Size() == 21, "the trail keeps one point per 48 units walked");
    trail.Record({1000, 0, 0}, 7, false);
    Check(trail.Size() == 21, "airborne positions are not landings");
    const auto behind = trail.Behind({1000, 0, 0}, 7, 200, 600, 150, 800);
    Check(!behind.empty() && behind.front().x <= 800 && behind.back().x >= 400, "trail points lie behind by route");
    for (std::size_t i = 1; i < behind.size(); ++i) {
        Check(behind[i].x < behind[i - 1].x, "trail points are returned nearest first");
    }
    Check(trail.Behind({1000, 0, 0}, 8, 0, 600, 0, 800).empty(), "another cell has no trail");
    trail.Record({1050, 0, 400}, 7, true);
    Check(trail.Size() == 1, "a fall the navmesh may not link starts a fresh trail");
    trail.Record({5000, 0, 400}, 7, true);
    Check(trail.Size() == 1, "fast travel starts a fresh trail");
    trail.Record({5000, 0, 400}, 9, true);
    Check(trail.Size() == 1 && trail.Behind({5000, 0, 400}, 7, 0, 600, 0, 800).empty(),
          "changing cell forgets the old route");
    PlayerTrail corner;
    for (int i = 0; i <= 10; ++i) {
        corner.Record({0, i * 50.0F, 0}, 1, true);
    }
    for (int i = 1; i <= 10; ++i) {
        corner.Record({i * 50.0F, 500, 0}, 1, true);
    }
    const auto around = corner.Behind({500, 500, 0}, 1, 700, 900, 0, 800);
    Check(!around.empty() && around.front().x == 0, "route distance follows the path around a corner");

    const auto ranked = RankLandings({{{0, 300, 0}, 300}, {{0, -300, 0}, 300}, {{300, 0, 0}, 300}},
                                     {0, 0, 0}, {0, 1}, 300);
    Check(ranked.size() == 3 && ranked[0].y < 0, "landings behind the player come first");
    Check(ranked[2].y > 0, "landings ahead of the player come last");
    const auto walled = RankLandings({{{0, -300, 0}, 1400}, {{-100, -280, 0}, 310}}, {0, 0, 0}, {0, 1}, 300);
    Check(walled[0].x < 0, "a spot behind a wall loses to one reached directly");
    Wheel::Motion m;
    m.Open(0);
    float last = 0;
    for (int i = 0; i < 40; ++i) {
        m.Update(.01F, 0);
        Check(m.visibility >= last && m.visibility <= 1, "opening is monotonic and bounded");
        last = m.visibility;
    }
    m.Update(.016F, 3);
    Check(m.centerWeight[0] > 0 && m.centerWeight[3] > 0, "center crossfades instead of switching");
    float sum = 0;
    for (auto weight : m.centerWeight) {
        sum += weight;
    }
    Check(std::abs(sum - 1) < .0001F, "center maintains opacity through transitions");
    m.Close();
    last = m.visibility;
    for (int i = 0; i < 20; ++i) {
        m.Close();
        m.Update(.01F, 3);
        Check(m.visibility <= last && m.visibility >= 0, "repeated cancel never restarts close");
        last = m.visibility;
    }
    Check(m.Finished() && m.visibility == 0, "exit reaches invisible before closing");
    m.Open(1);
    m.Update(.01F, 1);
    m.Close();
    m.Update(.01F, 1);
    Check(m.visibility < .1F, "cancel during opening does not flash to full opacity");
    m.Open(0);
    Check(!m.closing && !m.Finished(), "reopening resets close state");
    Wheel::Motion a, b;
    a.Open(0);
    b.Open(0);
    for (int i = 0; i < 30; ++i) {
        a.Update(1.0F / 30, 1);
    }
    for (int i = 0; i < 144; ++i) {
        b.Update(1.0F / 144, 1);
    }
    Check(std::abs(a.highlight[1] - b.highlight[1]) < .0001F,
          "focus smoothing is independent of refresh rate");
    a.Open(0);
    a.Update(.016F, 1);
    Check(Wheel::Difference(a.angle, Wheel::Angle(0)) < .4F,
          "indicator takes the short path across angle wrap");
    // The party actions grid.
    for (auto screen : {std::pair{1280.0F, 720.0F}, std::pair{1920.0F, 1080.0F}, std::pair{3440.0F, 1440.0F},
                        std::pair{3840.0F, 2160.0F}}) {
        const float w = Wheel::Width(screen.first, screen.second);
        for (int i = 0; i < ActionGrid::count; ++i) {
            const auto a = ActionGrid::TileBox(i, w);
            Check(a.x > 0 && a.x + a.w < w && a.y > ActionGrid::BackBar(w).y + ActionGrid::BackBar(w).h &&
                      a.y + a.h < w * .99F,
                  "tiles sit inside the window, under the back bar and above the detail line");
            for (int j = i + 1; j < ActionGrid::count; ++j) {
                const auto b = ActionGrid::TileBox(j, w);
                Check(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y,
                      "tiles never overlap");
            }
        }
        const auto more = ActionGrid::MoreBar(w);
        Check(more.y > w * (.48F + .41F) && more.y + more.h < w * 1.2F,
              "the down bar sits between the wheel and the hints");
    }
    Check(ActionGrid::Move(0, 0, -1) == -1 && ActionGrid::Move(2, 0, -1) == -1,
          "up from the top row goes back to the wheel");
    // Four party actions on top; the three roles below, centred between them.
    Check(ActionGrid::Move(0, 0, 1) == 4 && ActionGrid::Move(1, 0, 1) == 4 && ActionGrid::Move(2, 0, 1) == 5 &&
              ActionGrid::Move(3, 0, 1) == 6,
          "down goes to the nearest role, the left one on a tie");
    Check(ActionGrid::Move(4, 0, -1) == 0 && ActionGrid::Move(5, 0, -1) == 1 && ActionGrid::Move(6, 0, -1) == 2,
          "up goes to the nearest party action");
    Check(ActionGrid::Move(0, -1, 0) == 0 && ActionGrid::Move(3, 1, 0) == 3 && ActionGrid::Move(6, 1, 0) == 6 &&
              ActionGrid::Move(4, -1, 0) == 4 && ActionGrid::Move(5, 0, 1) == 5,
          "edges stop instead of wrapping");
    Check(ActionGrid::Move(4, 1, 0) == 5 && ActionGrid::Move(5, -1, 0) == 4 && ActionGrid::Move(1, 1, 0) == 2,
          "left and right move along a row");
    for (int i = 0; i < 3; ++i) {
        const auto top = ActionGrid::TileBox(i, 1), below = ActionGrid::TileBox(4 + i, 1);
        Check(std::abs(below.x - (top.x + top.w * .5F + .013F)) < .02F, "the roles row sits centred between");
    }
    std::array<float, 3> settled{};
    constexpr std::array<float, 3> rates{30, 60, 144};
    for (int r = 0; r < 3; ++r) {
        ActionGrid::Motion g;
        const std::array<bool, ActionGrid::count> on{false, true, false, true, false, true, true};
        g.Reset(0, false, on);
        for (int i = 0; i < static_cast<int>(rates[r] * .5F); ++i) {
            g.Update(1 / rates[r], true, 4, on);
        }
        settled[r] = g.page;
        Check(g.page > .99F && g.Reveal(ActionGrid::count - 1) > .99F, "the grid is fully in within half a second");
        Check(std::abs(g.frameX - ActionGrid::TileBox(4, 1).x) < .005F &&
                  std::abs(g.frameY - ActionGrid::TileBox(4, 1).y) < .005F,
              "the selection frame reaches the focused tile");
        Check(g.toggle[1] == 1 && g.toggle[4] == 0, "switches start in their saved position");
        g.Update(1 / rates[r], false, 4, on);
        Check(g.Reveal(0) > .99F && g.page < 1, "leaving keeps the tiles up while they fade with the page");
        for (int i = 0; i < static_cast<int>(rates[r] * .5F); ++i) {
            g.Update(1 / rates[r], false, 4, on);
        }
        Check(g.page < .01F, "and the wheel returns within half a second");
        g.Update(1 / rates[r], true, 4, on);
        Check(g.Reveal(0) < .1F, "returning to the grid replays the reveal");
    }
    Check(std::abs(settled[0] - settled[2]) < .01F, "page motion does not depend on frame rate");

    // The right stick on the grid.
    {
        ActionGrid::StickSteps s;
        auto steps = [&](float x, float y, float seconds) {
            std::array<int, 2> total{};
            for (float t = 0; t < seconds; t += 1.0F / 60) {
                const auto step = s.Update(x, y, 1.0F / 60);
                total[0] += step[0];
                total[1] += step[1];
            }
            return total;
        };
        Check(steps(.3F, 0, .5F) == std::array<int, 2>{0, 0}, "a light touch does not move");
        Check(steps(.9F, .1F, .2F) == std::array<int, 2>{1, 0}, "a firm push right moves one tile");
        Check(steps(.9F, .1F, .2F) == std::array<int, 2>{0, 0}, "holding briefly does not repeat");
        Check(steps(.9F, .1F, .5F)[0] >= 2, "holding longer repeats");
        steps(0, 0, .1F);
        Check(steps(0, -.8F, .1F) == std::array<int, 2>{0, -1}, "up is negative y, as on screen");
        s.Lock();
        Check(steps(0, .9F, .5F) == std::array<int, 2>{0, 0}, "after a page change a held stick is ignored");
        steps(0, 0, .1F);
        Check(steps(0, .9F, .1F) == std::array<int, 2>{0, 1}, "until it has been released");
    }

    // Taking over Nether's Follower Framework's catch-up.
    Check(OwnsFrameworkCatchup(true, true, true, true, true, false), "teleporting takes over NFF's catch-up");
    Check(OwnsFrameworkCatchup(true, true, true, true, false, true),
          "a held Relax camp takes over NFF's catch-up even with teleporting off");
    Check(!OwnsFrameworkCatchup(true, false, true, true, true, true), "NFF's catch-up switched off: hands off");
    Check(!OwnsFrameworkCatchup(true, true, false, true, true, true), "the player can forbid the takeover");
    Check(!OwnsFrameworkCatchup(true, true, true, false, true, true), "Vanilla or disabled gives it back");
    Check(!OwnsFrameworkCatchup(false, true, true, true, true, true), "no NFF, nothing to take over");
    Check(!OwnsFrameworkCatchup(true, true, true, true, false, false), "nothing to do, nothing taken");
    FrameworkWarpPark park;
    Check(park.Update(true, 8) == FrameworkWarpPark::parked && park.saved == 8, "parks and remembers 8 s");
    Check(std::isnan(park.Update(true, FrameworkWarpPark::parked)), "an existing park is left alone");
    Check(park.Release(FrameworkWarpPark::parked) == 8, "a save sees the player's own interval");
    Check(park.Update(true, 8) == FrameworkWarpPark::parked, "and it is parked again after the save");
    Check(park.Release(FrameworkWarpPark::parked) == 8, "the journal shows the player's interval");
    Check(park.Update(true, 12) == FrameworkWarpPark::parked && park.saved == 12,
          "a change made in NFF's menu is kept");
    Check(park.Update(false, FrameworkWarpPark::parked) == 12, "handing back restores the player's value");
    Check(std::isnan(park.Update(false, 12)), "nothing to restore twice");
    FrameworkWarpPark fresh;
    Check(fresh.Update(false, FrameworkWarpPark::parked) == FrameworkWarpPark::fallback,
          "a stray park from an earlier session is cleared to NFF's default");
    Check(std::isnan(fresh.Update(true, std::numeric_limits<float>::quiet_NaN())),
          "an unreadable value is never overwritten");
    Check(std::isnan(fresh.Release(8)), "releasing an unparked timer writes nothing");
    std::cout << "Catch-up timing and wheel motion checks passed\n";
}

#include "role_assignment.h"
#include "travel_banter.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
using namespace Wayfarer;
int checks{};
void Check(bool value, const char* message)
{
    ++checks;
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
int main()
{
    Check(CanWalkBanterScene(2, true, true, false, false, false, false),
          "simple two-follower dialogue may travel");
    Check(!CanWalkBanterScene(3, true, true, false, false, false, false), "larger scenes retain control");
    Check(!CanWalkBanterScene(2, false, true, false, false, false, false),
          "unmanaged participants retain scene control");
    Check(!CanWalkBanterScene(2, true, false, false, false, false, false), "timer-only scene is not banter");
    Check(!CanWalkBanterScene(2, true, true, true, false, false, false),
          "scripted movement package is protected");
    Check(!CanWalkBanterScene(2, true, true, false, true, false, false),
          "exclusive scene actor flags are protected");
    Check(!CanWalkBanterScene(2, true, true, false, false, true, false),
          "player-directed scenes are protected");
    Check(!CanWalkBanterScene(2, true, true, false, false, false, true), "looping scenes are protected");
    std::vector<Vec2> goals{{-100, 0}, {100, 0}, {-100, -450}, {100, -300}, {0, -600}};
    auto actors = goals;
    std::vector<int> roles{0, 1, 2, 3, 4};
    std::vector<std::array<int, 2>> pairs{{0, 3}};
    auto paired = PairNearbyRoles(actors, goals, roles, pairs, {0, 1});
    Check(paired == std::vector<int>({0, 3, 2, 1, 4}),
          "one partner swaps with the nearest suitable teammate");
    Check(paired[2] == roles[2] && paired[4] == roles[4], "unrelated companions keep their positions");
    Check(std::set<int>(paired.begin(), paired.end()).size() == roles.size(),
          "swap never duplicates or drops a role");
    for (int i = 0; i < 100; ++i) {
        actors[0].x += .1F;
        Check(PairNearbyRoles(actors, goals, paired, pairs, {0, 1}) == paired,
              "paired actors do not swap back during positional jitter");
    }
    pairs = {{0, 1}};
    Check(PairNearbyRoles(goals, goals, roles, pairs, {0, 1}) == roles,
          "already adjacent partners do not displace anyone");
    pairs = {{0, 3}};
    auto rotated = goals;
    for (auto& p : rotated) {
        auto x = p.x;
        p.x = p.y;
        p.y = -x;
    }
    Check(PairNearbyRoles(rotated, rotated, roles, pairs, {1, 0}) == paired,
          "beside is relative to travel direction");
    auto distant = goals;
    distant[3] = {5000, 5000};
    distant[0] = {-5000, -5000};
    Check(PairNearbyRoles(distant, goals, roles, pairs, {0, 1}) == roles,
          "distant partners do not cause long cross-party swaps");
    auto corridor = goals;
    for (std::size_t i = 0; i < corridor.size(); ++i) {
        corridor[i] = {0, -200.0F * static_cast<float>(i)};
    }
    Check(PairNearbyRoles(corridor, corridor, roles, pairs, {0, 1}) == roles,
          "single file navigation does not force a side-by-side swap");
    pairs = {{0, 3}, {1, 2}};
    Check(PairNearbyRoles(goals, goals, roles, pairs, {0, 1}) == paired,
          "second conversation preserves the first pair");
    pairs = {{0, 3}, {0, 4}, {-1, 9}, {2, 2}};
    Check(PairNearbyRoles(goals, goals, roles, pairs, {0, 1}) == paired,
          "conflicting and invalid pairs do not corrupt assignments");
    Check(PairNearbyRoles({}, {}, {}, {}, {0, 1}).empty(), "empty party supported");
    Check(PairNearbyRoles(goals, goals, std::vector<int>{0, 0, 2, 3, 4}, pairs, {0, 1}).empty(),
          "duplicate previous roles request a fresh matching");
    Check(PairNearbyRoles(goals, goals, std::vector<int>{-1, 1, 2, 3, 4}, pairs, {0, 1}).empty(),
          "unassigned roles request a fresh matching");
    Check(PairNearbyRoles(goals, goals, roles, pairs, {0, 0}).empty(),
          "undefined travel direction does not assign side positions");
    auto invalid = goals;
    invalid[0].x = std::numeric_limits<float>::quiet_NaN();
    Check(PairNearbyRoles(invalid, goals, roles, pairs, {0, 1}).empty(),
          "non-finite positions cannot produce swaps");
    Check(TravelBanterPair{5, 8, 4}.Contains(8) && !TravelBanterPair{5, 8, 4}.Contains(9),
          "pair membership is exact");
    TravelBanterPair pair{5, 8, 4};
    Check(CanRelaxBanterFacing(pair, 5, 8, true, true, false, false),
          "initiating speaker releases the cached facing hold");
    Check(CanRelaxBanterFacing(pair, 8, 5, true, true, false, false),
          "replying speaker uses the same movement policy");
    Check(CanRelaxBanterFacing(pair, 8, 0, true, false, false, false),
          "admitted scene listener keeps moving through phase gaps");
    Check(CanRelaxBanterFacing(pair, 5, 8, false, true, false, false), "loose follower dialogue can walk");
    Check(!CanRelaxBanterFacing(pair, 5, 0, false, true, false, false),
          "unknown loose-dialogue listener is protected");
    Check(!CanRelaxBanterFacing(pair, 5, 9, true, true, false, false),
          "third party targets are protected even during an admitted scene");
    Check(!CanRelaxBanterFacing(pair, 5, 5, true, true, false, false),
          "self target is not a two-person conversation");
    Check(!CanRelaxBanterFacing(pair, 5, 8, true, true, true, false),
          "player conversation never releases facing");
    Check(!CanRelaxBanterFacing(pair, 5, 8, true, true, false, true),
          "force-greet info never releases facing");
    Check(!CanRelaxBanterFacing(pair, 9, 8, true, true, false, false), "unrelated actor is protected");
    std::cout << checks << " banter checks passed\n";
}

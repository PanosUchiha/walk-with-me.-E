#include "rest_navigation.h"
#include "rest_recovery.h"
#include <cstdlib>
#include <iostream>
using namespace Wayfarer;
int checks = 0;
void Check(bool ok, const char* message)
{
    ++checks;
    if (!ok) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
GroundTriangle Triangle(float x, float y, float z)
{
    return {{{{x - 20, y - 20, z}, {x + 20, y - 20, z}, {x, y + 40, z}}}, {}};
}
int main()
{

    std::unordered_map<GroundKey, GroundTriangle> mesh{{1, Triangle(0, 0, 0)},
                                                       {2, Triangle(250, 0, 0)},
                                                       {3, Triangle(250, 200, 160)},
                                                       {4, Triangle(0, 0, 320)},
                                                       {5, Triangle(0, 0, 640)}};
    mesh[1].neighbors[0] = 2;
    mesh[2].neighbors = {1, 3, {}};
    mesh[3].neighbors = {2, 4, {}};
    mesh[4].neighbors[0] = 3;
    const GroundQuery query = [&](GroundKey key) -> std::optional<GroundTriangle> {
        auto found = mesh.find(key);
        return found == mesh.end() ? std::nullopt : std::optional{found->second};
    };
    RestRouteLimits limits{{0, 0, 0}, 1000, 768, 4000, 64};
    auto area = ExploreRestGround({0, 0, 0}, 1, limits, query);
    Check(area.distance.contains(4), "connected stair detour reaches upstairs without a straight corridor");
    Check(area.distance.at(4) > 700,
          "route distance includes stair detour, not straight-line vertical separation");
    Check(!area.distance.contains(5), "stacked disconnected floor is never reachable merely by sharing X/Y");
    Check(!area.limited, "small connected room completes within search budget");
    auto downstairs = ExploreRestGround({0, 0, 320}, 4, limits, query);
    Check(downstairs.distance.contains(1), "same stair route works downstairs");
    auto broken = mesh[3].neighbors;
    mesh[3].neighbors[1].reset();
    Check(!ExploreRestGround({0, 0, 0}, 1, limits, query).distance.contains(4),
          "missing portal or excluded door link cannot bridge floors");
    mesh[3].neighbors = broken;
    limits.height = 120;
    area = ExploreRestGround({0, 0, 0}, 1, limits, query);
    Check(area.distance.contains(2) && !area.distance.contains(3) && !area.distance.contains(4),
          "height limit constrains intermediate stairs and destination");
    limits.height = 768;
    limits.radius = 180;
    Check(!ExploreRestGround({0, 0, 0}, 1, limits, query).distance.contains(4),
          "route cannot use a stair detour outside the search radius");
    limits.radius = 1000;
    limits.maxLength = 500;
    Check(!ExploreRestGround({0, 0, 0}, 1, limits, query).distance.contains(4),
          "long detours respect the route-length budget");
    limits.maxLength = 4000;
    limits.maxNodes = 2;
    area = ExploreRestGround({0, 0, 0}, 1, limits, query);
    Check(area.limited && area.distance.size() == 2 && !area.distance.contains(4),
          "bounded work never labels unvisited nodes reachable");
    limits.maxNodes = 64;
    Check(ExploreRestGround({0, 0, 0}, 99, limits, query).distance.empty(),
          "missing start triangle produces no destination");
    Check(ExploreRestGround({2000, 0, 0}, 1, limits, query).distance.empty(),
          "actor outside the anchored rest area cannot expand it");
    mesh[2].neighbors[2] = 99;
    Check(ExploreRestGround({0, 0, 0}, 1, limits, query).distance.contains(4),
          "missing neighbor is skipped without breaking a valid route");
    Check(!RestArrived({0, 0, 0}, {0, 0, 320}), "directly below an upstairs destination is not arrival");
    Check(!RestArrived({0, 0, 320}, {0, 0, 0}), "directly above a downstairs destination is not arrival");
    Check(RestArrived({30, 30, 310}, {0, 0, 320}), "nearby point on the correct floor is arrival");
    Check(!RestArrived({100, 0, 320}, {0, 0, 320}), "height match alone is not arrival");
    Check(RestWalkBudget(500) == 20 && RestWalkBudget(3000) > 40 && RestWalkBudget(100000) == 60,
          "long stair walks get extra time with a hard cap");
    RestRecovery walk;
    walk.Begin(RestWalkBudget(3000));
    for (int i = 0; i < 80; ++i) {
        Check(!walk.Done(.25F, false, false), "stair route does not time out at the old twenty-second limit");
    }
    Check(walk.Done(.25F, false, true), "scene or furniture still interrupts a longer walk immediately");
    walk.Reset();
    walk.Begin();
    for (int i = 0; i < 79; ++i) {
        walk.Done(.25F, false, false);
    }
    Check(walk.Done(.25F, false, false), "ordinary random recovery keeps its twenty-second limit");
    walk.Begin(1000);
    for (int i = 0; i < 239; ++i) {
        walk.Done(.25F, false, false);
    }
    Check(walk.Done(.25F, false, false), "even an unreachable long walk ends within sixty seconds");
    std::cout << checks << " multi-level navigation checks passed\n";
}

#include "obstacle_avoidance.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
using namespace Wayfarer;
int checks{};
void Check(bool v, const char* text)
{
    ++checks;
    if (!v) {
        std::cerr << "FAIL: " << text << '\n';
        std::exit(1);
    }
}
struct Box
{
    float x0, y0, x1, y1;
};
bool Crosses(GroundPoint from, Vec2 to, Box box)
{
    float low = 0, high = 1;
    auto slab = [&](float a, float b, float min, float max) {
        if (std::abs(b - a) < 1e-5F) {
            return a >= min && a <= max;
        }
        float enter = (min - a) / (b - a), exit = (max - a) / (b - a);
        if (enter > exit) {
            std::swap(enter, exit);
        }
        low = std::max(low, enter);
        high = std::min(high, exit);
        return low <= high;
    };
    return slab(from.x, to.x, box.x0, box.x1) && slab(from.y, to.y, box.y0, box.y1);
}
int main()
{
    const GroundPoint origin{0, 0, 0}, goal{0, 1500, 0};
    ObstacleAvoidance state;
    auto clear = [](GroundPoint, Vec2 to) -> std::optional<GroundPoint> {
        return GroundPoint{to.x, to.y, 0};
    };
    {
        auto open = FitFormationCorridor(origin, {160, 300}, {0, 1}, clear);
        Check(open && open->x == 160 && open->y == 300, "open space preserves formation width");
        auto gate = [&](GroundPoint, Vec2 to) -> std::optional<GroundPoint> {
            // A 100-unit opening, allowing the 28-unit radius on each side.
            if (std::abs(to.x) > 22) {
                return {};
            }
            return GroundPoint{to.x, to.y, 0};
        };
        auto narrow = FitFormationCorridor(origin, {160, 300}, {0, 1}, gate);
        Check(narrow && narrow->x == 0 && narrow->y == 300,
              "gateposts collapse a wide formation onto the traversable centerline");
        auto bend = [&](GroundPoint, Vec2 to) -> std::optional<GroundPoint> {
            if (std::abs(to.x) > 22 || to.y > 170) {
                return {};
            }
            return GroundPoint{to.x, to.y, 0};
        };
        auto shortRoute = FitFormationCorridor(origin, {160, 300}, {0, 1}, bend);
        Check(shortRoute && shortRoute->x == 0 && shortRoute->y == 150,
              "cave bend shortens prediction before assigning a goal beyond the wall");
        auto partial = [](GroundPoint, Vec2) -> std::optional<GroundPoint> { return GroundPoint{0, 10, 0}; };
        Check(!FitFormationCorridor(origin, {160, 300}, {0, 1}, partial),
              "a clipped navmesh edge cannot masquerade as a clear formation destination");
        auto missing = [](GroundPoint, Vec2) -> std::optional<GroundPoint> { return {}; };
        Check(!FitFormationCorridor(origin, {160, 300}, {0, 1}, missing),
              "disconnected navmesh yields to native following instead of crossing walls");
        auto rear = FitFormationCorridor(origin, {160, -300}, {0, 1}, gate);
        Check(rear && rear->x == 0 && rear->y == -300, "rear formations compress behind the player");
    }
    auto result = state.Plan(origin, goal, 90, .15F, 65, clear);
    Check(result.route == ObstacleRoute::Direct && result.target.y == 1500,
          "open ground retains the full formation destination");

    std::vector<Box> rocks{{-88, 150, 88, 370}};
    auto probe = [&](GroundPoint from, Vec2 to) -> std::optional<GroundPoint> {
        for (auto box : rocks) {
            if (Crosses(from, to, box)) {
                return {};
            }
        }
        return GroundPoint{to.x, to.y, 0};
    };
    result = state.Plan(origin, goal, 90, .15F, 65, probe);
    Check(result.route == ObstacleRoute::Detour && result.changed, "rock triggers steering before contact");
    Check(result.target.x > 0, "symmetric rock produces a deterministic side");
    const auto held = result.target;
    for (int i = 0; i < 8; ++i) {
        auto jitter = goal;
        jitter.x = i % 2 ? 15 : -15;
        result = state.Plan(origin, jitter, 90, .15F, 65, probe);
        Check(result.route == ObstacleRoute::Detour &&
                  std::hypot(result.target.x - held.x, result.target.y - held.y) < 1,
              "moving formation goals do not wobble the avoidance waypoint");
    }
    GroundPoint actor = origin;
    for (int i = 0; i < 180 && actor.y < 800; ++i) {
        result = state.Plan(actor, goal, 90, .15F, 65, probe);
        Check(result.route != ObstacleRoute::Blocked, "local steering finds a route around a finite rock");
        const auto direction = Normalize({result.target.x - actor.x, result.target.y - actor.y});
        const Vec2 next{actor.x + direction.x * 13.5F, actor.y + direction.y * 13.5F};
        Check(probe(actor, next).has_value(), "simulated walking step never enters the rock");
        actor = {next.x, next.y, 0};
    }
    Check(actor.y > 800, "follower makes forward progress after detouring");
    Check(!state.active, "formation resumes after passing the obstacle");
    rocks = {{-88, 150, 88, 370}, {90, -50, 600, 500}};
    state.Reset();
    result = state.Plan(origin, goal, 90, .15F, 65, probe);
    Check(result.route == ObstacleRoute::Detour && result.target.x < 0,
          "blocked preferred side uses the other side");
    rocks = {{-1000, 90, 1000, 150}};
    state.Reset();
    result = state.Plan(origin, goal, 90, .15F, 65, probe);

    Check(result.route == ObstacleRoute::Blocked || result.target.y < 90,
          "wide walls never produce a target through the wall");
    auto unavailable = [](GroundPoint, Vec2) -> std::optional<GroundPoint> { return {}; };
    result = state.Plan(origin, goal, 90, .15F, 65, unavailable);
    Check(result.route == ObstacleRoute::Blocked && !state.active,
          "missing navmesh or collision world yields control");
    auto invalid = [](GroundPoint, Vec2) -> std::optional<GroundPoint> {
        return GroundPoint{std::numeric_limits<float>::quiet_NaN(), 0, 0};
    };
    Check(state.Plan(origin, goal, 90, .15F, 65, invalid).route == ObstacleRoute::Blocked,
          "invalid hit data never becomes a destination");
    auto clipped = [](GroundPoint, Vec2) -> std::optional<GroundPoint> { return GroundPoint{0, 10, 0}; };
    Check(state.Plan(origin, goal, 90, .15F, 65, clipped).route == ObstacleRoute::Blocked,
          "clipped navmesh endpoints are not accepted as clear corridors");
    rocks = {{-88, 150, 88, 370}};
    state.Reset();
    state.Plan(origin, goal, 90, .15F, 65, probe);
    result = state.Plan(origin, {0, -1000, 0}, 90, .15F, 65, probe);
    Check(result.route == ObstacleRoute::Direct && result.target.y < 0,
          "player reversal abandons the old detour");
    state.Reset();
    state.Plan(origin, goal, 90, .15F, 65, probe);
    rocks.clear();
    result = state.Plan(origin, goal, 90, .15F, 65, probe);
    Check(result.route == ObstacleRoute::Detour, "one clear frame does not abandon the held side");
    for (int i = 0; i < 7; ++i) {
        result = state.Plan(origin, goal, 90, .15F, 65, probe);
    }
    Check(result.route == ObstacleRoute::Direct, "sustained clearance returns to formation");
    float walkHorizon = 0, runHorizon = 0;
    auto observe = [&](GroundPoint, Vec2 to) -> std::optional<GroundPoint> {
        runHorizon = to.y;
        return GroundPoint{to.x, to.y, 0};
    };
    state.Reset();
    state.Plan(origin, goal, 90, .15F, 65, observe);
    walkHorizon = runHorizon;
    state.Plan(origin, goal, 335, .15F, 65, observe);
    Check(runHorizon > walkHorizon && runHorizon <= 480, "running probes farther with a bounded horizon");
    state.Reset();
    Check(state.Plan(origin, origin, 90, .15F, 65, unavailable).route == ObstacleRoute::Direct,
          "already arrived actor needs no steering probe");
    Check(state.Plan(origin, goal, 90, 0, 65, probe).route == ObstacleRoute::Blocked,
          "invalid frame duration cannot change motion");
    Check(!UseLocalObstacleSteering(false, false, false, 100),
          "town and interior routes never use local detours");
    Check(!UseLocalObstacleSteering(true, false, true, 400),
          "catch-up hysteresis keeps native routing until the follower returns");
    Check(!UseLocalObstacleSteering(true, false, false, 900),
          "distant followers do not receive sideways targets or detour speed caps");
    Check(!UseLocalObstacleSteering(true, true, false, 100), "hand-guided travel retains its own controller");
    Check(UseLocalObstacleSteering(true, false, false, 200),
          "nearby wilderness travel still anticipates rocks");

    rocks = {{-10000, 120, 10000, 180}};
    state.Reset();
    actor = origin;
    bool handedOff = false;
    unsigned detourFrames = 0;
    for (int i = 0; i < 100; ++i) {
        const GroundPoint movingGoal{0, 1500 + i * 42.0F, 0};
        result = state.Plan(actor, movingGoal, 90, .15F, 65, probe);
        if (result.route == ObstacleRoute::Blocked) {
            result = state.UseNative(movingGoal);
        }
        if (result.route == ObstacleRoute::Native) {
            Check(result.target.x == movingGoal.x && result.target.y == movingGoal.y,
                  "native recovery restores the formation destination");
            Check(!state.active, "native recovery removes detour pace restrictions");
            handedOff = true;
            break;
        }
        Check(result.route == ObstacleRoute::Detour, "wall does not become a false direct corridor");
        ++detourFrames;
        const auto heading = Normalize({result.target.x - actor.x, result.target.y - actor.y});
        actor = {actor.x + heading.x * 13.5F, actor.y + heading.y * 13.5F, 0};
        Check(std::abs(actor.x) <= 360, "one obstruction cannot drift a follower far sideways");
    }
    Check(handedOff, "long walls return to native routing within a bounded episode");
    Check(detourFrames > 1, "wall regression exercises a moving detour before handing off");
    unsigned queries = 0;
    auto counted = [&](GroundPoint from, Vec2 to) {
        ++queries;
        return probe(from, to);
    };
    for (int i = 0; i < 40; ++i) {
        result = state.Plan(actor, {0, 3000 + i * 42.0F, 0}, 281, .15F, 65, counted);
        Check(result.route == ObstacleRoute::Native && !result.changed,
              "recovery does not repeatedly restart the route");
    }
    Check(queries == 0, "native routing cooldown suppresses repeated local obstacle searches");

    rocks = {{-88, 150, 88, 370}};
    state.Reset();
    state.Plan(origin, goal, 90, .15F, 65, probe);
    rocks.clear();
    actor = {held.x, held.y + 150, 0};
    result = state.Plan(actor, goal, 90, .15F, 65, probe);
    Check(result.route == ObstacleRoute::Direct && result.target.y > actor.y,
          "overshot detours never send followers back to old waypoints");

    rocks = {{-88, 150, 88, 370}};
    state.Reset();
    handedOff = false;
    for (int i = 0; i < 60; ++i) {
        result = state.Plan(origin, goal, 90, .15F, 65, probe);
        if (result.route == ObstacleRoute::Native) {
            handedOff = true;
            break;
        }
    }
    Check(handedOff, "episode time limit survives waypoint reselection");
    std::cout << checks << " obstacle checks passed, including collision-free walking simulation\n";
}

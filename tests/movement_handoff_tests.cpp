#include "hand_hold_locomotion.h"
#include "movement_handoff.h"
#include <cstdlib>
#include <iostream>

using namespace Wayfarer;
void Check(bool value, const char* reason)
{
    if (!value) {
        std::cerr << reason << '\n';
        std::exit(1);
    }
}

int main()
{
    Check(CanGrantMovement(true, true, 1, 1, 10, 10), "matching enrollment can receive movement");
    Check(!CanGrantMovement(false, true, 1, 1, 10, 10), "filled aliases cannot override a released grant");
    Check(!CanGrantMovement(true, false, 1, 1, 10, 10), "mismatched plugin disables grants");
    Check(!CanGrantMovement(true, true, 2, 1, 10, 10), "slot reuse cannot drive the previous actor");
    Check(!CanGrantMovement(true, true, 2, 2, 20, 10),
          "new actor with stale destination cannot receive movement");
    Check(CanGrantMovement(true, true, 2, 2, 20, 20),
          "slot reuse becomes available only after both aliases synchronize");
    Check(!CanGrantMovement(true, true, 0, 0, 10, 10) && !CanGrantMovement(true, true, 1, 1, 0, 0),
          "empty aliases fail closed");
    MovementHandoff h;
    constexpr unsigned follow = 1, travel = 2, task = 3;
    Check(h.Permit(true, false, follow), "initial movement request may acquire");
    Check(h.Permit(true, true, travel), "WWM may maintain its own route");
    Check(h.Permit(true, false, 0), "a transient empty package does not erase the previous owner");
    Check(!h.Permit(true, false, task), "foreign takeover yields immediately");
    for (int frame = 0; frame < 10000; ++frame) {
        Check(!h.Permit(true, false, task), "elapsed frames never trigger takeover retries");
    }
    Check(!h.Permit(true, false, 0), "missing package during transition does not end the yield");
    Check(h.Permit(true, false, follow), "a completed task allows a fresh request");
    Check(h.Permit(true, true, travel), "new route can become owned");
    Check(!h.Permit(true, false, follow), "even a competing follow package can displace WWM");
    Check(!h.Permit(false, false, follow), "stopping cancels the movement request");
    Check(h.Permit(true, false, follow), "resuming after stopping starts a new request");
    Check(h.Permit(true, true, travel), "travel resumes after a stop");
    h.Reset(); // Arrival or an explicit new order.
    Check(h.Permit(true, false, follow), "normal route completion permits the next route");

    TravelGoalState route;
    TravelTuning tuning;
    route.active = true;
    route.hasRoute = true;
    route.goal = {0, 500};
    route.submittedGoal = {0, 200};
    route.sinceRepath = 60;
    Check(!route.ShouldRepath({}, 100, true, false, tuning),
          "travel cannot reset an unowned package after long delay");
    Check(!HandHolding::RepathAttached(route, {}, 100, false, tuning),
          "hand holding obeys the same ownership rule");
    route.hasRoute = false;
    Check(!route.ShouldRepath({}, 100, true, false, tuning), "missing route does not bypass ownership");
    Check(!HandHolding::RepathAttached(route, {}, 100, false, tuning),
          "missing hand route does not bypass ownership");
    Check(route.ShouldRepath({}, 100, true, true, tuning), "an owned package may request its initial route");
    for (int frame = 0; frame < 1000; ++frame) {
        route.Tick({}, .15F, false, tuning);
    }
    Check(route.active && route.stalledTime == 0 && route.recoveryTime == 0,
          "another owner's stationary task cannot indirectly trigger takeover through recovery");
    std::cout << "Movement handoff scenarios passed\n";
}

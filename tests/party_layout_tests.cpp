#include "role_assignment.h"
#include "travel_planner.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numeric>
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
    {
        std::vector<Vec2> actors{{0, -900}, {0, -100}}, goals{{0, 100}, {0, 400}};
        std::vector<int> old{-1, -1};
        auto roles = AssignNearestRoles(actors, goals, old);
        Check(roles == std::vector<int>({0, 1}),
              "nearer companion takes far lead position; trailing companion gets shorter catch-up");
        roles = AssignNearestRoles({actors.data(), actors.size()}, goals, std::vector<int>{1, 0});
        Check(roles == std::vector<int>({0, 1}), "large distance improvement replaces stale roles");
        actors = {{-200, 0}, {200, 0}};
        goals = {{200, 200}, {-200, 200}};
        Check(AssignNearestRoles(actors, goals, old) == std::vector<int>({1, 0}),
              "matching respects lateral proximity without needless crossing");
        actors = {{-1, 0}, {1, 0}};
        goals = {{-100, 0}, {100, 0}};
        old = {1, 0};
        Check(AssignNearestRoles(actors, goals, old) == old, "small movement does not swap near-equal roles");
        for (int i = 0; i < 100; ++i) {
            const float jitter = static_cast<float>(i % 7) - 3;
            actors = {{jitter, 0}, {-jitter, 0}};
            Check(AssignNearestRoles(actors, goals, old) == old,
                  "repeated positional jitter retains existing assignment");
        }
        actors = {{-180, 0}, {180, 0}};
        Check(AssignNearestRoles(actors, goals, old) == std::vector<int>({0, 1}),
              "clear positional change overcomes stability penalty");
        actors = {{0, 0}, {100, 0}, {200, 0}};
        goals = actors;
        old = {0, 0, 9};
        roles = AssignNearestRoles(actors, goals, old);
        Check(std::set<int>(roles.begin(), roles.end()).size() == 3,
              "join/return repairs duplicate and out-of-range roles");
        actors.clear();
        goals.clear();
        old.clear();
        for (int i = 0; i < 10; ++i) {
            actors.push_back({static_cast<float>(i * 150), 0});
            goals.push_back({static_cast<float>((9 - i) * 150), 0});
            old.push_back(-1);
        }
        roles = AssignNearestRoles(actors, goals, old);
        Check(roles.size() == 10 && std::set<int>(roles.begin(), roles.end()).size() == 10,
              "ten companions receive unique roles");
        for (int i = 0; i < 10; ++i) {
            Check(roles[i] == 9 - i, "ten-member matching chooses nearby positions");
        }
        Check(AssignNearestRoles({}, {}, {}).empty(), "empty party is supported");
        {
            RoleDebounce hold;
            const std::vector<int> now{0, 1, 2}, swapped{1, 0, 2}, other{0, 2, 1};
            Check(hold.Update(now, now, .5F).empty(), "an unchanged assignment applies nothing");
            Check(hold.Update(now, swapped, .5F).empty(), "a new assignment waits");
            Check(hold.Update(now, swapped, .5F).empty() && hold.Update(now, swapped, .5F).empty(),
                  "it keeps waiting for the hold");
            Check(hold.Update(now, swapped, .5F) == swapped, "a proposal that keeps winning is applied");
            Check(hold.Update(now, swapped, .5F).empty(), "the hold restarts after applying");
            hold.Reset();
            for (int i = 0; i < 20; ++i) {
                Check(hold.Update(now, i % 2 ? swapped : other, .5F).empty(),
                      "alternating proposals never move anyone");
            }
            Check(hold.Update(now, swapped, .5F).empty() && hold.Update(now, now, .5F).empty() &&
                      hold.Update(now, swapped, .5F).empty(),
                  "going back to the current roles cancels the wait");
            for (int i = 0; i < 10; ++i) {
                Check(hold.Update(now, other, 0).empty(), "no time passes while someone detours");
            }
            Check(hold.Update(std::vector<int>{0, -1, 2}, std::vector<int>{0, 1, 2}, 0) ==
                      std::vector<int>({0, 1, 2}),
                  "a companion who just joined is placed at once");
            Check(hold.Update(std::vector<int>{0, 0, 2}, std::vector<int>{0, 1, 2}, 0) ==
                      std::vector<int>({0, 1, 2}),
                  "repeated roles are repaired at once");
        }
        {
            // Two companions trailing their places, each stepped 30 units past the middle (round a
            // rock): swapping saves 36,000. The old 80-unit penalty (12,800 for the pair) swapped
            // them across the formation; the new one (80,000) keeps them.
            const std::vector<Vec2> walkers{{30, -380}, {-30, -380}}, places{{-150, 0}, {150, 0}};
            Check(AssignNearestRoles(walkers, places, std::vector<int>{0, 1}, 80) == std::vector<int>({1, 0}),
                  "the old penalty swapped sides for a sideways step");
            Check(AssignNearestRoles(walkers, places, std::vector<int>{0, 1}) == std::vector<int>({0, 1}),
                  "a sideways step no longer swaps sides");
        }
        Check(AssignNearestRoles(actors, std::span<const Vec2>(goals).first(9), old).empty(),
              "mismatched input cannot partially assign party");

        actors = {{-90, 30}, {80, -220}, {140, 40}, {-180, -80}, {40, 340}};
        goals = {{-120, 230}, {150, 170}, {-60, -180}, {220, -90}, {30, 60}};
        old.assign(5, -1);
        roles = AssignNearestRoles(actors, goals, old, 0);
        auto score = [&](const auto& mapping) {
            double total = 0;
            for (int i = 0; i < 5; ++i) {
                const double x = actors[i].x - goals[mapping[i]].x, y = actors[i].y - goals[mapping[i]].y;
                total += x * x + y * y;
            }
            return total;
        };
        std::vector<int> permutation{0, 1, 2, 3, 4};
        double best = score(permutation);
        do {
            best = std::min(best, score(permutation));
        } while (std::next_permutation(permutation.begin(), permutation.end()));
        Check(score(roles) == best, "party-wide solution matches exhaustive minimum");
        FormationSlot blended{0, -500};
        const FormationSlot target{200, -100};
        const auto first = BlendRoleOffset(blended, target, .1F);
        Check(std::hypot(first.lateral - blended.lateral, first.longitudinal - blended.longitudinal) <=
                  40.01F,
              "role change limits destination-offset speed");
        for (int i = 0; i < 50; ++i) {
            blended = BlendRoleOffset(blended, target, .1F);
        }
        Check(std::abs(blended.lateral - target.lateral) < .1F &&
                  std::abs(blended.longitudinal - target.longitudinal) < .1F,
              "blended role reaches new region");
        Check(blended.longitudinal < 0, "Rear offset blending stays behind player");
    }
    std::set<std::uint32_t> packages, aliases;
    for (int slot = 0; slot < PARTY_CAPACITY; ++slot) {
        packages.insert(PackageLocal(slot));
        aliases.insert(FollowerAlias(slot));
        aliases.insert(MarkerAlias(slot));
        const auto p = NaturalPosition(FormationMode::kDynamic, slot, 12345);
        const auto same = NaturalPosition(FormationMode::kDynamic, slot, 12345);
        const auto base = FormationPosition(FormationMode::kDynamic, slot);
        Check(p.lateral == same.lateral && p.longitudinal == same.longitudinal,
              "personal variation never rerolls within a role");
        Check(std::abs(p.lateral - base.lateral) <= 24 && std::abs(p.longitudinal - base.longitudinal) <= 32,
              "variation stays within the broad 0.2.1 travel regions");
        Check(NaturalPosition(FormationMode::kLead, slot, 12345).longitudinal > 0,
              "Lead chooses space ahead");
        Check(NaturalPosition(FormationMode::kRear, slot, 12345).longitudinal < 0,
              "Rear chooses space behind");
    }
    Check(packages.size() == 10 && !packages.contains(0x805), "ten packages retain the quest ID gap");
    Check(aliases.size() == 20 && FollowerAlias(0) == 0 && MarkerAlias(0) == 5,
          "old alias IDs remain compatible");
    Check(NaturalPosition(FormationMode::kDynamic, 0, 111).lateral !=
              NaturalPosition(FormationMode::kDynamic, 0, 222).lateral,
          "different companions receive varied automatic positions");
    TravelTuning t;
    TravelIntent intent;
    for (int i = 0; i < 80; ++i) {
        intent.Update({}, {0, 106}, .05F, t);
    }
    Check(intent.RoutingLead() > 139, "walking builds a useful forward route extension");
    for (int slot = 0; slot < PARTY_CAPACITY; ++slot) {
        auto rear = intent.GoalAt(FormationMode::kRear, NaturalPosition(FormationMode::kRear, slot, 12345),
                                  false, 1, t);
        Check(intent.RouteGoal(rear, FormationMode::kRear, false).y < 0,
              "routing look-ahead cannot pull Rear ahead of the player");
    }
    const auto extended = intent.RouteGoal({0, 300}, FormationMode::kDynamic, false);
    Check(ChoosePace(106, 245 - 140, TravelPace::kWalk, true) == TravelPace::kWalk,
          "route look-ahead does not trigger catch-up Jog");
    Check(std::abs(DesiredSpeedScale(106, 245 - 140, TravelPace::kWalk, true, t) - 1.06F) < .01F,
          "walking pace matches the player independently of the extended endpoint");
    for (int i = 0; i < 40; ++i) {
        intent.Update({}, {}, .05F, t);
    }
    Check(intent.RouteGoal({0, 300}, FormationMode::kDynamic, false).y == extended.y,
          "stopping freezes route extension instead of pulling companions backwards");
    TravelGoalState route;
    route.Commit({0, 245}, {}, TravelPace::kWalk);
    route.MarkRepathed();
    float minimumGap = 1000;
    int resets = 0, updates = 0;
    for (int i = 1; i <= 400; ++i) {
        const Vec2 actor{0, i * 106 * .15F}, target{0, actor.y + 245};
        route.Tick(actor, .15F, true, t);
        minimumGap = std::min(minimumGap, route.submittedGoal.y - actor.y);
        if (route.ShouldCommit(target, actor, TravelPace::kWalk, t, 106, true)) {
            route.Commit(target, actor, TravelPace::kWalk);
            ++updates;
        }
        if (route.ShouldRepath(actor, 106, true, true, t)) {
            route.Commit(target, actor, TravelPace::kWalk);
            route.MarkRepathed();
            ++resets;
        }
    }
    Check(minimumGap > t.arrivalRadius + 20,
          "submitted walking routes retain arrival headroom over one minute");
    Check(resets < 50 && updates > 300,
          "frequent player-route tracking does not reset the AI on every update");
    TravelGoalState turn;
    turn.Commit({0, 500}, {}, TravelPace::kWalk);
    turn.MarkRepathed();
    turn.Tick({}, .3F, true, t);
    turn.Commit({300, 300}, {}, TravelPace::kWalk);
    Check(turn.ShouldRepath({}, 106, true, true, t),
          "a real turn refreshes promptly without crowd grace timers");
    TravelGoalState lateral;
    lateral.Commit({0, 1000}, {}, TravelPace::kWalk);
    lateral.MarkRepathed();
    lateral.Tick({}, .15F, true, t);
    lateral.Commit({100, 1000}, {}, TravelPace::kWalk);
    Check(!lateral.ShouldRepath({}, 106, true, true, t),
          "spacing changes respect the minimum route lifetime");
    lateral.Tick({}, .15F, true, t);
    lateral.Commit({20, 1000}, {}, TravelPace::kWalk);
    Check(!lateral.ShouldRepath({}, 106, true, true, t), "small sideways jitter preserves the engine route");
    lateral.Commit({60, 1000}, {}, TravelPace::kWalk);
    Check(lateral.ShouldRepath({}, 106, true, true, t),
          "a wider lane refreshes even when the old route is still far ahead");
    std::set<int> counts;
    for (std::uint32_t seed = 0; seed < 60; ++seed) {
        counts.insert(NaturalStragglerCount(6, seed, 250));
        for (int size = 1; size <= 10; ++size) {
            int selected = 0;
            std::vector<FormationSlot> trailing;
            for (int role = 0; role < size; ++role) {
                const auto planned = PartyRole(FormationMode::kDynamic, role, size, seed, 250);
                const auto repeated = PartyRole(FormationMode::kDynamic, role, size, seed, 250);
                Check(planned.straggler == repeated.straggler &&
                          planned.offset.lateral == repeated.offset.lateral,
                      "a travelling pattern remains stable");
                if (planned.straggler) {
                    ++selected;
                    trailing.push_back(planned.offset);
                }
                const auto disabled = PartyRole(FormationMode::kDynamic, role, size, seed, 0, 12345);
                const auto old = NaturalPosition(FormationMode::kDynamic, role, 12345);
                Check(!disabled.straggler && disabled.offset.lateral == old.lateral &&
                          disabled.offset.longitudinal == old.longitudinal,
                      "zero setback restores the original Natural layout");
                for (auto mode : {FormationMode::kLead, FormationMode::kRear, FormationMode::kCompanion,
                                  FormationMode::kSandbox}) {
                    const auto other = PartyRole(mode, role, size, seed, 800, 12345),
                               original = TravelRole{NaturalPosition(mode, role, 12345), false};
                    Check(!other.straggler && other.offset.lateral == original.offset.lateral &&
                              other.offset.longitudinal == original.offset.longitudinal,
                          "stragglers never modify another formation");
                }
            }
            Check(selected == NaturalStragglerCount(size, seed, 250) && selected <= 2 &&
                      size - selected >= std::min(size, 2),
                  "all party sizes preserve a main group and at most two stragglers");
            if (trailing.size() == 2) {
                Check(trailing[0].longitudinal == trailing[1].longitudinal &&
                          std::abs(trailing[0].lateral - trailing[1].lateral) == 230,
                      "two stragglers share a rear line with ordinary mutual spacing");
            }
        }
    }
    Check(counts == std::set<int>({1, 2}), "Natural can choose either one or two stragglers");
    for (float speed : {106.0F, 335.0F}) {
        for (bool interior : {false, true}) {
            for (float spacing : {.6F, 3.0F}) {
                TravelTuning tuning;
                tuning.spacing = spacing;
                TravelIntent moving;
                for (int i = 0; i < 100; ++i) {
                    moving.Update({}, {0, speed}, .05F, tuning);
                }
                const auto left =
                    moving.GoalAt(FormationMode::kDynamic, {-115, -145}, interior, 1, tuning, 250);
                const auto right =
                    moving.GoalAt(FormationMode::kDynamic, {115, -145}, interior, 1, tuning, 250);
                Check(std::abs(std::abs(right.x - left.x) - 230 * spacing * (interior ? .55F : 1)) < .01F,
                      "spacing controls the full requested lateral gap indoors and outdoors");
                const auto farther =
                    moving.GoalAt(FormationMode::kDynamic, {-115, -145}, interior, 1, tuning, 500);
                Check(std::abs(left.x - farther.x) < .01F &&
                          std::abs(left.y - farther.y - 250 * (interior ? .65F : 1)) < .01F,
                      "extra setback changes only rear distance, independently of spacing");
                Check(moving.RouteGoal(left, FormationMode::kRear, interior).y < 0,
                      "walking and running route prediction leave stragglers behind the player");
            }
        }
    }
    std::cout << checks << " natural party / walking checks passed\n";
}

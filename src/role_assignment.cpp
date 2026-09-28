#include "role_assignment.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

namespace Wayfarer
{
    std::vector<int> AssignNearestRoles(std::span<const Vec2> actors, std::span<const Vec2> goals,
                                        std::span<const int> currentRoles, float switchDistance)
    {
        const auto actorCount = actors.size();
        if (!actorCount || actorCount > 10 || goals.size() != actorCount ||
            currentRoles.size() != actorCount) {
            return {};
        }
        for (const auto& points : {actors, goals}) {
            for (auto p : points) {
                if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
                    return {};
                }
            }
        }

        const auto assignmentCount = 1U << actorCount;
        std::array<double, 1U << 10> cost;
        std::array<int, 1U << 10> lastRole{};
        cost.fill(std::numeric_limits<double>::infinity());
        cost[0] = 0;
        const double switchPenalty = std::pow(std::max(0.0F, switchDistance), 2);
        for (unsigned mask = 0; mask < assignmentCount; ++mask) {
            const auto actor = std::popcount(mask);
            if (actor >= static_cast<int>(actorCount)) {
                continue;
            }
            for (unsigned role = 0; role < actorCount; ++role) {
                if (mask & (1U << role)) {
                    continue;
                }
                const double dx = actors[actor].x - goals[role].x, dy = actors[actor].y - goals[role].y;
                const bool changed =
                    currentRoles[actor] >= 0 && currentRoles[actor] != static_cast<int>(role);
                const double candidate = cost[mask] + dx * dx + dy * dy + (changed ? switchPenalty : 0);
                const auto next = mask | (1U << role);
                if (candidate < cost[next]) {
                    cost[next] = candidate;
                    lastRole[next] = static_cast<int>(role);
                }
            }
        }
        std::vector<int> result(actorCount, -1);
        auto mask = assignmentCount - 1;
        for (auto actor = actorCount; actor > 0; --actor) {
            const int role = lastRole[mask];
            result[actor - 1] = role;
            mask ^= 1U << role;
        }
        return result;
    }

    std::vector<int> RoleDebounce::Update(std::span<const int> current, std::span<const int> proposed,
                                          float elapsed, float hold)
    {
        if (proposed.empty() || proposed.size() != current.size() ||
            std::equal(current.begin(), current.end(), proposed.begin(), proposed.end())) {
            Reset();
            return {};
        }
        unsigned seen = 0;
        bool placed = current.size() <= 10;
        for (int role : current) {
            if (!placed || role < 0 || role >= static_cast<int>(current.size()) || (seen & (1U << role))) {
                placed = false;
                break;
            }
            seen |= 1U << role;
        }
        if (placed && !std::equal(pending.begin(), pending.end(), proposed.begin(), proposed.end())) {
            pending.assign(proposed.begin(), proposed.end());
            age = 0;
            return {};
        }
        if (placed) {
            age += std::isfinite(elapsed) ? std::clamp(elapsed, 0.0F, 1.0F) : 0.0F;
            if (age + 1e-4F < hold) {
                return {};
            }
        }
        Reset();
        return {proposed.begin(), proposed.end()};
    }

    FormationSlot BlendRoleOffset(FormationSlot current, FormationSlot target, float dt)
    {
        dt = std::clamp(dt, 0.0F, 0.5F);
        const float dx = target.lateral - current.lateral, dy = target.longitudinal - current.longitudinal;
        const float distance = std::hypot(dx, dy);
        if (distance < .01F) {
            return target;
        }
        const float fraction = std::min(1.0F - std::exp(-6.0F * dt), 400.0F * dt / distance);
        return {current.lateral + dx * fraction, current.longitudinal + dy * fraction};
    }

    std::vector<int> PairNearbyRoles(std::span<const Vec2> actors, std::span<const Vec2> goals,
                                     std::span<const int> roles, std::span<const std::array<int, 2>> pairs,
                                     Vec2 forward)
    {
        const auto actorCount = actors.size();
        if (!actorCount || actorCount > 10 || goals.size() != actorCount || roles.size() != actorCount) {
            return {};
        }
        unsigned seen = 0;
        for (int role : roles) {
            if (role < 0 || role >= static_cast<int>(actorCount) || (seen & (1U << role))) {
                return {};
            }
            seen |= 1U << role;
        }
        for (auto points : {actors, goals}) {
            for (auto p : points) {
                if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
                    return {};
                }
            }
        }
        if (!std::isfinite(forward.x) || !std::isfinite(forward.y) || Length(forward) < .01F) {
            return {};
        }
        forward = Normalize(forward);
        auto result = std::vector<int>(roles.begin(), roles.end());
        std::array<bool, 10> protectedActor{};
        auto areSideBySide = [&](int left, int right) {
            const Vec2 delta{goals[left].x - goals[right].x, goals[left].y - goals[right].y};
            const float along = std::abs(delta.x * forward.x + delta.y * forward.y);
            const float across = std::abs(delta.x * forward.y - delta.y * forward.x);
            return along <= 100.0F && across >= 90.0F && across <= 360.0F;
        };
        auto squaredTravelDistance = [&](int actor, int role) {
            const float x = actors[actor].x - goals[role].x, y = actors[actor].y - goals[role].y;
            return x * x + y * y;
        };
        for (auto pair : pairs) {
            const int a = pair[0], b = pair[1];
            if (a < 0 || b < 0 || a >= static_cast<int>(actorCount) || b >= static_cast<int>(actorCount) ||
                a == b || protectedActor[a] || protectedActor[b]) {
                continue;
            }
            if (!areSideBySide(result[a], result[b])) {
                float best = std::numeric_limits<float>::infinity();
                int moving = -1, swap = -1;
                for (int partner : {a, b}) {
                    for (int other = 0; other < static_cast<int>(actorCount); ++other) {
                        if (other == a || other == b || protectedActor[other]) {
                            continue;
                        }
                        const int staying = partner == a ? b : a;
                        if (!areSideBySide(result[other], result[staying])) {
                            continue;
                        }
                        const float first = squaredTravelDistance(partner, result[other]),
                                    second = squaredTravelDistance(other, result[partner]);
                        if (first > 650.0F * 650.0F || second > 650.0F * 650.0F) {
                            continue;
                        }

                        const float cost = first + second - squaredTravelDistance(partner, result[partner]) -
                                           squaredTravelDistance(other, result[other]);
                        if (cost < best) {
                            best = cost;
                            moving = partner;
                            swap = other;
                        }
                    }
                }
                if (moving >= 0) {
                    std::swap(result[moving], result[swap]);
                }
            }
            protectedActor[a] = protectedActor[b] = true;
        }
        return result;
    }
} // namespace Wayfarer

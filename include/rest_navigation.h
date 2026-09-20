#pragma once
#include "travel_planner.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>
#include <vector>

namespace Wayfarer
{
    inline float GroundDistance(GroundPoint a, GroundPoint b)
    {
        return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
    }
    inline GroundPoint GroundCenter(const GroundTriangle& t)
    {
        return {(t.vertices[0].x + t.vertices[1].x + t.vertices[2].x) / 3,
                (t.vertices[0].y + t.vertices[1].y + t.vertices[2].y) / 3,
                (t.vertices[0].z + t.vertices[1].z + t.vertices[2].z) / 3};
    }
    struct RestRouteLimits
    {
        GroundPoint center;
        float radius{1000}, height{768}, maxLength{4000};
        std::size_t maxNodes{2048};
    };
    struct RestReachability
    {
        std::unordered_map<GroundKey, float> distance;
        bool limited{};
    };

    inline RestReachability ExploreRestGround(GroundPoint origin, GroundKey start,
                                              const RestRouteLimits& limits, const GroundQuery& query)
    {
        RestReachability result;
        if (!limits.maxNodes || limits.radius <= 0 || limits.height <= 0 || limits.maxLength <= 0) {
            return result;
        }
        auto inside = [&](GroundPoint p) {
            return std::hypot(p.x - limits.center.x, p.y - limits.center.y) <= limits.radius &&
                   std::abs(p.z - limits.center.z) <= limits.height;
        };
        const auto first = query(start);
        if (!first || !inside(origin)) {
            return result;
        }
        using Entry = std::pair<float, GroundKey>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pending;
        std::unordered_map<GroundKey, float> best;
        const auto firstCenter = GroundCenter(*first);
        if (!inside(firstCenter)) {
            return result;
        }
        const float initial = GroundDistance(origin, firstCenter);
        if (initial > limits.maxLength) {
            return result;
        }
        pending.push({initial, start});
        best[start] = initial;
        while (!pending.empty()) {
            const auto [distance, key] = pending.top();
            pending.pop();
            if (result.distance.contains(key) || distance > best.at(key)) {
                continue;
            }
            if (result.distance.size() >= limits.maxNodes) {
                result.limited = true;
                break;
            }
            const auto current = query(key);
            if (!current) {
                continue;
            }
            result.distance.emplace(key, distance);
            const auto center = GroundCenter(*current);
            for (auto next : current->neighbors) {
                if (next && !result.distance.contains(*next)) {
                    const auto triangle = query(*next);
                    if (!triangle) {
                        continue;
                    }
                    const auto point = GroundCenter(*triangle);
                    if (!inside(point)) {
                        continue;
                    }
                    const float candidate = distance + GroundDistance(center, point);
                    if (candidate > limits.maxLength) {
                        continue;
                    }
                    auto found = best.find(*next);
                    if (found != best.end() && candidate >= found->second) {
                        continue;
                    }
                    best[*next] = candidate;
                    pending.push({candidate, *next});
                }
            }
        }
        return result;
    }
    struct RestDestination
    {
        GroundPoint point;
        float routeLength{};
    };
    // Nearby connected formation destinations when a straight projection stops
    // at a bend. Native AI still computes the route through the linked triangles.
    inline std::vector<GroundPoint> ConnectedFormationDestinations(GroundPoint origin, Vec2 goal,
                                                                   Vec2 direction,
                                                                   const RestReachability& area,
                                                                   const GroundQuery& query)
    {
        struct Candidate
        {
            float score;
            GroundKey key;
            GroundPoint point;
        };
        std::vector<Candidate> candidates;
        const auto forward = Normalize(direction);
        for (const auto& [key, distance] : area.distance) {
            auto triangle = query(key);
            if (!triangle) {
                continue;
            }
            const auto point = GroundCenter(*triangle);
            const float advance = (point.x - origin.x) * forward.x + (point.y - origin.y) * forward.y;
            if (advance < 110 || distance > 900 || std::abs(point.z - origin.z) > 192) {
                continue;
            }
            const float score = std::hypot(point.x - goal.x, point.y - goal.y) + distance * .2F;
            candidates.push_back({score, key, point});
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            return a.score == b.score ? a.key < b.key : a.score < b.score;
        });
        std::vector<GroundPoint> result;
        for (const auto& candidate : candidates) {
            result.push_back(candidate.point);
            if (result.size() == 8) {
                break;
            }
        }
        return result;
    }
    inline bool RestArrived(GroundPoint actor, GroundPoint destination)
    {
        return std::hypot(actor.x - destination.x, actor.y - destination.y) < 75 &&
               std::abs(actor.z - destination.z) < 64;
    }
    inline float RestWalkBudget(float routeLength)
    {
        return std::clamp(8.0F + routeLength / 85.0F, 20.0F, 60.0F);
    }
} // namespace Wayfarer

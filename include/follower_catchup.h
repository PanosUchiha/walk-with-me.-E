#pragma once
#include "travel_planner.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <vector>

namespace Wayfarer
{
    struct FollowerCatchup
    {
        // A companion in another cell gets a little longer: the engine's own
        // door following usually brings them through within a second or two.
        static constexpr float delay = 2.0F, separatedDelay = 3.0F;
        static constexpr float recallCooldown = 8.0F, retryCooldown = 2.0F;
        float distantTime{}, cooldown{};
        bool Update(float dt, bool permitted, float distance, float threshold, bool separated = false)
        {
            if (!std::isfinite(dt) || dt <= 0) {
                return false;
            }
            cooldown = std::max(0.0F, cooldown - dt);
            const bool distant =
                separated || (std::isfinite(distance) && std::isfinite(threshold) && distance >= threshold);
            if (!permitted || !distant) {
                distantTime = 0;
                return false;
            }
            distantTime += dt;
            return distantTime >= (separated ? separatedDelay : delay) && cooldown <= 0;
        }
        void Attempt(bool moved)
        {
            distantTime = 0;
            cooldown = moved ? recallCooldown : retryCooldown;
        }
    };

    // Nether's Follower Framework teleports its roster on a timer of its own: every nwsWarpStart
    // seconds it moves anyone beyond range, or unloaded, 512 units behind the player with no ground
    // check. While Walk With Me does the teleporting, that interval is parked out of reach, so the
    // two never race and NFF never pulls a companion out of a Relax camp. NFF's own switch
    // (nwsAllowWarp) is left alone: turning it off also stops NFF's update loop from re-arming.
    struct FrameworkWarpPark
    {
        static constexpr float parked = 1.0e9F;
        static constexpr float fallback = 8.0F; // NFF's default interval.
        float saved{fallback};
        bool active{};

        // The value to write to the interval now; NaN leaves it alone.
        float Update(bool want, float current)
        {
            if (!std::isfinite(current)) {
                return std::numeric_limits<float>::quiet_NaN();
            }
            if (want) {
                active = true;
                if (current == parked) {
                    return std::numeric_limits<float>::quiet_NaN();
                }
                saved = current; // The first park, or the player changed it while it was released.
                return parked;
            }
            active = false;
            // Never leave a park behind, even one an earlier session made.
            return current == parked ? saved : std::numeric_limits<float>::quiet_NaN();
        }

        // Before a save, and while the journal (where NFF's menu lives) is open: the player's own
        // value. The next update parks it again if Walk With Me still wants it. Whoever writes a
        // restored value back also restarts NFF's countdown (warpCDTime), which kept counting
        // while parked and would otherwise fire a warp on NFF's very next tick.
        float Release(float current)
        {
            active = false;
            return current == parked ? saved : std::numeric_limits<float>::quiet_NaN();
        }
    };

    // Walk With Me does NFF's teleporting as well as its own only when NFF's catch-up is on, the
    // player allows the takeover, Walk With Me is directing the party, and it is either teleporting
    // itself or keeping a Relax camp that NFF would otherwise empty.
    inline bool OwnsFrameworkCatchup(bool nffInstalled, bool nffCatchupOn, bool takeoverAllowed,
                                     bool directing, bool teleporting, bool holdingCamp)
    {
        return nffInstalled && nffCatchupOn && takeoverAllowed && directing && (teleporting || holdingCamp);
    }

    // A drop into a ravine or a stranding on a ledge far above counts as distance
    // even when the companion is close on the map.
    inline float CatchupDistance(GroundPoint actor, GroundPoint player)
    {
        const float dz = std::abs(actor.z - player.z);
        return std::max(std::hypot(actor.x - player.x, actor.y - player.y, dz), dz * 2.0F);
    }

    // Where the player has actually stood. Each point was walkable moments ago and is
    // connected to the player by the route they took, so it is a safe landing even on
    // slopes, stairs and around corners where a straight navmesh probe fails.
    class PlayerTrail
    {
      public:
        static constexpr float spacing = 48.0F;
        static constexpr float maxStep = 600.0F; // Fast travel, a load door or a teleport.
        static constexpr float maxRise = 120.0F; // A fall or jump the navmesh may not link.
        static constexpr std::size_t capacity = 96;

        void Record(GroundPoint p, std::uint64_t space, bool grounded)
        {
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
                return;
            }
            if (space != currentSpace) {
                points.clear();
                currentSpace = space;
            }
            if (!grounded) {
                return;
            }
            if (!points.empty()) {
                const auto& last = points.back();
                const float flat = std::hypot(p.x - last.x, p.y - last.y);
                if (flat > maxStep || std::abs(p.z - last.z) > maxRise) {
                    points.clear();
                }
                else if (flat < spacing) {
                    return;
                }
            }
            points.push_back(p);
            if (points.size() > capacity) {
                points.pop_front();
            }
        }
        void Clear()
        {
            points.clear();
            currentSpace = 0;
        }
        [[nodiscard]] std::size_t Size() const { return points.size(); }

        // Points walked back from the player, in route order, whose route distance lies in
        // [minRoute, maxRoute] and straight-line distance in [minDirect, maxDirect].
        [[nodiscard]] std::vector<GroundPoint> Behind(GroundPoint player, std::uint64_t space, float minRoute,
                                                      float maxRoute, float minDirect, float maxDirect) const
        {
            std::vector<GroundPoint> result;
            if (space != currentSpace || points.empty()) {
                return result;
            }
            GroundPoint previous = player;
            float route = 0;
            for (auto point = points.rbegin(); point != points.rend(); ++point) {
                route += std::hypot(point->x - previous.x, point->y - previous.y);
                previous = *point;
                if (route > maxRoute) {
                    break;
                }
                const float direct = std::hypot(point->x - player.x, point->y - player.y);
                if (route >= minRoute && direct >= minDirect && direct <= maxDirect) {
                    result.push_back(*point);
                }
            }
            return result;
        }

      private:
        std::deque<GroundPoint> points;
        std::uint64_t currentSpace{};
    };

    struct LandingCandidate
    {
        GroundPoint point;
        float route{}; // Navmesh route length from the player.
    };

    // Behind the player first, then beside, then ahead; near `ideal` distance; and
    // preferring spots whose route is not much longer than the straight line, which
    // keeps companions off the far side of walls.
    inline std::vector<GroundPoint> RankLandings(std::vector<LandingCandidate> spots, GroundPoint player,
                                                 Vec2 forward, float ideal)
    {
        auto cost = [&](const LandingCandidate& c) {
            const float dx = c.point.x - player.x, dy = c.point.y - player.y;
            const float direct = std::hypot(dx, dy);
            const float ahead = direct > 1 ? (dx * forward.x + dy * forward.y) / direct : 0;
            return std::abs(direct - ideal) + 250.0F * std::max(0.0F, ahead + 0.35F) +
                   0.5F * std::max(0.0F, c.route - direct) + 0.5F * std::abs(c.point.z - player.z);
        };
        std::stable_sort(spots.begin(), spots.end(),
                         [&](const auto& a, const auto& b) { return cost(a) < cost(b); });
        std::vector<GroundPoint> result;
        result.reserve(spots.size());
        for (const auto& spot : spots) {
            result.push_back(spot.point);
        }
        return result;
    }
} // namespace Wayfarer

#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

// Loot scouting rules without game types, so they can be unit tested.
namespace Wayfarer::Loot
{
    struct Tuning
    {
        bool enabled{true};
        float searchRadius{1000};  // From the follower who notices it.
        float leaveDistance{1500}; // Player to loot; beyond this the follower gives up.
        int valuableGold{1000};    // A single item worth more than this turns the icon gold.
        float searchSeconds{12};   // Time spent kneeling at the find.
        float areaDistance{2000};  // After one find, how far the player must move before the next.
        bool containers{true}, bodies{true}, looseItems{true};
        bool icons{true};
        // Homes, inns, temples, shops and halls: someone lives or works there. Off by default.
        bool inhabitedInteriors{false};
        // Only one companion scouts at a time. Empty: whoever is nearest; otherwise this reference key.
        std::string finder, finderName;
    };

    // The designated finder, or anyone when none is chosen.
    [[nodiscard]] inline bool MayScout(const std::string& finder, const std::string& key)
    {
        return finder.empty() || (!key.empty() && key == finder);
    }

    enum class Kind : std::uint8_t
    {
        Container,
        Body,
        Item
    };

    enum class Phase : std::uint8_t
    {
        None,
        Approach,
        Turn,
        Search, // Kneeling or rummaging at the find.
        Rise,   // Standing up again before anything else happens.
        Present
    };

    enum class End : std::uint8_t
    {
        None,
        PlayerLeft,
        Stalled,
        Lost,
        Claimed,
        Emptied,
        Released,
        Presented
    };

    inline constexpr float nearPlayer = 150;  // The player is already standing at it.
    inline constexpr float floorHeight = 200; // Above or below this is another floor or ledge.
    inline constexpr float arrivalSlack = 45; // Beyond arm's length, where the travel package stops.
    inline constexpr float approachLimit = 25;
    inline constexpr float stallSeconds = 4;
    inline constexpr float stallProgress = 24;
    inline constexpr float turnLimit = 1.2F;
    inline constexpr float riseSeconds = 2.2F;
    inline constexpr float presentSeconds = 15.0F; // Rejoin even if the player leaves the find unopened.
    inline constexpr float surveySeconds = 2.0F;   // Look around before choosing, so the best find wins.

    [[nodiscard]] inline bool Valuable(int value, int threshold)
    {
        return value > threshold;
    }

    // One inventory stack as the scanner sees it.
    struct Item
    {
        int value{}, count{};
        bool playable{}, named{};
        bool coins{};      // A pile of septims is judged by its total.
        bool provisions{}; // Ingredients, food and animal parts: not worth a detour on their own.
    };

    struct Contents
    {
        int notable{}, bestValue{};
        long long total{}; // Everything the find holds; ranks finds against each other.
        bool valuable{};
        void Add(const Item& item, int threshold)
        {
            if (!item.playable || !item.named || item.count <= 0 || item.value < 0) {
                return;
            }
            const int count = std::min(item.count, 1'000'000);
            const int worth = item.coins ? item.value * count : item.value;
            bestValue = std::max(bestValue, worth);
            total += item.coins ? worth : static_cast<long long>(item.value) * count;
            const bool precious = Valuable(worth, threshold);
            valuable = valuable || precious;
            if (!item.provisions || precious) {
                ++notable;
            }
        }
        [[nodiscard]] bool Worthwhile() const { return notable > 0; }
    };

    // Loose clutter is everywhere, so a loose item must be valuable to count.
    [[nodiscard]] inline bool WorthPointingOut(Kind kind, const Contents& contents)
    {
        return kind == Kind::Item ? contents.valuable : contents.Worthwhile();
    }

    // The richest find wins; its best single item, then nearness to the player, break ties.
    [[nodiscard]] inline bool Richer(const Contents& a, float aPlayerDistance, const Contents& b,
                                     float bPlayerDistance)
    {
        if (a.total != b.total) {
            return a.total > b.total;
        }
        if (a.bestValue != b.bestValue) {
            return a.bestValue > b.bestValue;
        }
        return aPlayerDistance < bPlayerDistance;
    }

    struct Placement
    {
        float followerDistance{}, playerDistance{}, height{};
    };
    [[nodiscard]] inline bool InReach(const Placement& p, const Tuning& t)
    {
        return std::isfinite(p.followerDistance) && std::isfinite(p.playerDistance) &&
               std::isfinite(p.height) && p.followerDistance <= t.searchRadius &&
               p.playerDistance >= nearPlayer && p.playerDistance <= t.leaveDistance * .8F &&
               std::abs(p.height) <= floorHeight;
    }

    // Walking distance over connected navmesh to a standing spot beside the find.
    struct Route
    {
        bool known{}; // False when there was no navmesh to judge by.
        float length{std::numeric_limits<float>::infinity()};
        [[nodiscard]] bool Disconnected() const { return known && !std::isfinite(length); }
    };
    // Seen is not enough: the finder must be able to walk there without a long way round, such as
    // through the next room to reach a chest just behind a wall.
    [[nodiscard]] inline bool Walkable(const Route& route, float straightDistance, const Tuning& t)
    {
        if (!route.known) {
            return true; // The approach's stall check still gives up on an unreachable find.
        }
        return std::isfinite(route.length) && route.length <= t.searchRadius * 1.5F &&
               route.length <= std::max(straightDistance, 0.0F) * 2 + 300;
    }

    // Waits a moment after the first sighting so a richer find further in can still win.
    struct Survey
    {
        float time{};
        bool Update(float dt, bool anythingInView)
        {
            time = anythingInView && std::isfinite(dt) && dt > 0 ? time + dt : 0.0F;
            return time >= surveySeconds;
        }
    };

    // One find per area: after the finder reaches a find, the player has to move on first.
    struct Area
    {
        bool done{};
        float x{}, y{};
        void Finish(float atX, float atY)
        {
            done = true;
            x = atX;
            y = atY;
        }
        // True while scouting stays paused; reopens once the player is far enough away.
        bool Holds(float playerX, float playerY, float distance)
        {
            if (done && (!std::isfinite(playerX) || !std::isfinite(playerY) ||
                         std::hypot(playerX - x, playerY - y) > distance)) {
                done = false;
            }
            return done;
        }
    };

    // One follower's trip to a find: walk over, turn to it, kneel and search, stand, then wait.
    struct Visit
    {
        Phase phase{Phase::None};
        float elapsed{}, phaseTime{}, bestDistance{}, sinceProgress{};
        bool valuable{}, reached{}, searchPending{}, risePending{};
        End pendingEnd{End::None};

        [[nodiscard]] bool Active() const { return phase != Phase::None; }
        void Start(bool isValuable)
        {
            *this = {};
            phase = Phase::Approach;
            valuable = isValuable;
            bestDistance = std::numeric_limits<float>::infinity();
        }
        void Stop() { *this = {}; }
        // Each is true once: start the search animation, then the stand-up animation.
        bool ConsumeSearch() { return std::exchange(searchPending, false); }
        bool ConsumeRise() { return std::exchange(risePending, false); }
        // `external` is a reason found by the caller (opened, emptied, gone). Ending while kneeling
        // first stands the follower up; the reason is returned once that has had time to play.
        End Tick(float dt, float goalDistance, float playerDistance, float leaveDistance, bool facing,
                 float searchSeconds, End external = End::None)
        {
            if (!Active()) {
                return End::None;
            }
            if (!std::isfinite(dt) || dt < 0 || !std::isfinite(goalDistance) ||
                !std::isfinite(playerDistance)) {
                return End::Lost;
            }
            elapsed += dt;
            phaseTime += dt;
            if (external == End::None && playerDistance > leaveDistance) {
                external = End::PlayerLeft;
            }
            if (external != End::None && pendingEnd == End::None) {
                if (phase == Phase::Search) {
                    Enter(Phase::Rise);
                    risePending = true;
                }
                else if (phase != Phase::Rise) {
                    return external;
                }
                pendingEnd = external;
            }
            switch (phase) {
            case Phase::Approach:
                if (goalDistance <= 0) {
                    reached = true;
                    Enter(Phase::Turn);
                    break;
                }
                if (goalDistance < bestDistance - stallProgress) {
                    bestDistance = goalDistance;
                    sinceProgress = 0;
                }
                else {
                    sinceProgress += dt;
                }
                if (sinceProgress >= stallSeconds || phaseTime >= approachLimit) {
                    return End::Stalled;
                }
                break;
            case Phase::Turn:
                if (facing || phaseTime >= turnLimit) {
                    Enter(Phase::Search);
                    searchPending = true;
                }
                break;
            case Phase::Search:
                if (phaseTime >= std::max(0.0F, searchSeconds)) {
                    Enter(Phase::Rise);
                    risePending = true;
                }
                break;
            case Phase::Rise:
                if (phaseTime >= riseSeconds) {
                    if (pendingEnd != End::None) {
                        return pendingEnd;
                    }
                    Enter(Phase::Present);
                }
                break;
            case Phase::Present:
                if (phaseTime >= presentSeconds) {
                    return End::Presented;
                }
                break;
            default:
                break;
            }
            return End::None;
        }

      private:
        void Enter(Phase next)
        {
            phase = next;
            phaseTime = 0;
        }
    };

    [[nodiscard]] inline float Cooldown(End reason)
    {
        return reason == End::Stalled ? 12.0F : 5.0F;
    }

    [[nodiscard]] inline const char* EndName(End reason)
    {
        switch (reason) {
        case End::PlayerLeft:
            return "player moved on";
        case End::Stalled:
            return "could not reach it";
        case End::Lost:
            return "target gone";
        case End::Claimed:
            return "player opened it";
        case End::Emptied:
            return "emptied";
        case End::Released:
            return "released";
        case End::Presented:
            return "finished showing the find";
        default:
            return "none";
        }
    }

    // Seconds to fade the overhead icon in.
    [[nodiscard]] inline float IconAlpha(float elapsed)
    {
        return std::isfinite(elapsed) ? std::clamp(elapsed / .35F, 0.0F, 1.0F) : 1.0F;
    }
} // namespace Wayfarer::Loot

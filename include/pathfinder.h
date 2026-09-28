#pragma once
#include "formation_math.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

// Pathfinder: while the party travels in Natural, Lead, Companion or Custom, a companion who sees
// a place the player has never found (an undiscovered map marker) runs out toward it, turns to
// catch the player's eye with a wave, and points it out. Then they lead the way: whenever they
// are far enough ahead they stop and wave the player on, and they walk on once the player comes
// close. If the player goes another way, or finds the place, they come back to the group.
// Pure timing and geometry; the controller supplies positions, facing and the gestures.
namespace Wayfarer::Pathfinder
{
    constexpr float spotMin = 1500.0F;   // Closer than this, the compass has already shown it.
    constexpr float arrival = 110.0F;    // Close enough to the running point.
    constexpr float scanSeconds = 1.5F;
    constexpr float standoff = 450.0F;   // They stop this far short of the place itself.
    constexpr float minRun = 200.0F;     // Any shorter and there is nothing to see them do.

    // Settings > Roles > Pathfinder.
    struct Tuning
    {
        bool enabled{true};
        std::string companion, companionName; // Reference key; empty = anyone, a curious one first.
        float range{7000.0F};    // How far away a place may be noticed.
        float run{1500.0F};      // How far ahead of the player, toward the place, they get before stopping.
        float resume{600.0F};    // How close the player comes before they lead on.
        float cooldown{75.0F};   // Seconds between sightings.
        bool wave{true};         // Wave to the player before pointing, and to wave them on.
        bool notice{true};       // Name what they saw in a notification.
        bool lead{true};         // Lead the way there after pointing it out.
        bool icon{true};         // Float the pathfinder emblem over them for the whole trip.
        bool operator==(const Tuning&) const = default;
    };
    // They give up rather than end up further from the player than this.
    inline float Leash(const Tuning& t) { return std::max(750.0F, t.run + 400.0F); }
    // A leg aims this far past where they stop, so a player walking behind never keeps the
    // point out of reach: the leg ends on the distance from the player instead.
    constexpr float overshoot = 300.0F;
    constexpr float farEnough = 100.0F; // A leg ends this close to the stopping distance.

    // How far toward a place `distance` away they run: the configured distance, but never closer
    // to it than `standoff`.
    inline float RunLength(float distance, float run)
    {
        return std::isfinite(distance) ? std::clamp(distance - standoff, 0.0F, run) : 0.0F;
    }

    // Where to run: `length` from the player straight at the place, or `degrees` either side of
    // it (positive = clockwise) when the straight line is blocked.
    inline Vec2 RunPoint(Vec2 player, Vec2 travel, Vec2 toPlace, float length, float degrees = 0.0F)
    {
        const auto dir = Normalize(toPlace, Normalize(travel));
        const float a = degrees * 3.14159265F / 180.0F, c = std::cos(a), s = std::sin(a);
        return {player.x + (dir.x * c + dir.y * s) * length, player.y + (dir.y * c - dir.x * s) * length};
    }

    // The run ends when they arrive, stop gaining ground, or run out of time. The time allowed
    // is generous (their package can take a second or two to engage) and grows with the
    // distance, since the run can now cover thousands of units.
    constexpr float retargetDistance = 300.0F; // The player walked on this far: so does the running point.
    constexpr float stallSeconds = 3.0F; // This long without gaining ground and they stop.
    constexpr float progressStep = 40.0F;
    constexpr float maxDashSeconds = 20.0F;
    inline float DashSeconds(float distance)
    {
        return std::clamp(3.0F + (std::isfinite(distance) ? distance : 0.0F) / 250.0F, 4.5F, maxDashSeconds);
    }
    constexpr float turnSeconds = 1.0F;  // At most this long turning before each gesture.
    constexpr float waveSeconds = 1.8F;
    constexpr float pointSeconds = 2.4F;

    // Leading the way.
    constexpr float nearPlace = standoff + 250.0F; // A stop this close to the place is the last.
    constexpr float minLeg = 300.0F;       // A leg that gains less on the place than this is the last.
    constexpr float beckonSeconds = 7.0F;  // While waiting, they wave again this often.
    constexpr float patienceSeconds = 90.0F; // They stop waiting after this long.
    constexpr float awayMargin = 500.0F;   // At a stop: the player walked this much further from them.
    constexpr float strayMargin = 800.0F;  // The player is this much further from the place than at best.

    enum class Phase : std::uint8_t
    {
        Idle,
        Dash,  // Running toward the place.
        Wave,  // Turned to the player, waving.
        Point, // Turned to the place, pointing.
        Wait   // Facing the player, waiting for them to come closer.
    };
    enum class End : std::uint8_t
    {
        None,
        Done,
        FellBehind,
        Interrupted,
        WentAnotherWay,
        StoppedFollowing,
        Found
    };
    inline const char* EndName(End end)
    {
        switch (end) {
        case End::Done:
            return "pointed it out";
        case End::FellBehind:
            return "too far from you";
        case End::Interrupted:
            return "interrupted";
        case End::WentAnotherWay:
            return "you went another way";
        case End::StoppedFollowing:
            return "you stopped following";
        case End::Found:
            return "you found it";
        default:
            return "still going";
        }
    }

    // Only a place roughly the way the party is heading, or off to one side: never one behind,
    // which would mean running back past the player.
    inline bool WorthPointing(Vec2 travel, Vec2 toPlace, float distance, float range)
    {
        if (!std::isfinite(distance) || distance < spotMin || distance > std::max(spotMin, range)) {
            return false;
        }
        const auto a = Normalize(travel), b = Normalize(toPlace);
        return a.x * b.x + a.y * b.y > -.2F; // Within about 100 degrees of the heading.
    }

    // A distant structure is "seen" if the sight line reaches it or strikes something close to
    // it, which is usually the structure itself.
    inline bool Visible(float hitFraction, float length)
    {
        if (!std::isfinite(hitFraction) || hitFraction >= 1) {
            return true;
        }
        return length * (1 - hitFraction) < std::max(450.0F, length * .15F);
    }

    // What the controller measures each frame.
    struct Sense
    {
        float toGoal{};        // From the companion to the running point.
        float fromPlayer{};    // From the companion to the player.
        float toPlace{};       // From the companion to the place.
        float playerToPlace{}; // From the player to the place.
        bool facing{};         // Whether they face what this phase turns them to: the place when
                               // pointing, otherwise the player.
    };
    struct Rules
    {
        float leash{750.0F};
        float stopAt{1500.0F}; // A leg ends this far from the player.
        float resume{600.0F};  // They lead on once the player is this close.
        bool wave{true};
        bool lead{true};
    };
    // Waiting for the player to come within half the stopping distance at most, so every leg
    // covers ground: were the two close, they would stop the moment they set off.
    inline Rules RulesFor(const Tuning& t)
    {
        return {Leash(t), t.run, std::min(t.resume, t.run * .5F), t.wave, t.lead};
    }
    // What to do this frame.
    struct Cue
    {
        bool wave{}, point{};
        bool leadOn{}; // The player came close: the controller calls Lead or Close.
    };

    // Run out, wave, point, then (when leading) wait and lead on until the last stop, where they
    // point once more when the player arrives.
    struct State
    {
        Phase phase{Phase::Idle};
        float elapsed{}, inPhase{};
        float closest{-1}, stalled{}, dashLimit{}; // Run progress: best distance to the point so far.
        float legFrom{};      // How far from the place this leg started.
        float nearest{};      // The closest the player has come during this wait.
        float playerBest{};   // The closest the player has been to the place.
        float beckon{};
        int stops{};
        bool gestured{};
        bool pointed{};       // The place has been pointed out.
        bool last{};          // This stop is as close as they get.
        bool closing{};       // Pointing on arrival: done after this.
        [[nodiscard]] bool Active() const { return phase != Phase::Idle; }
        // `toGoal`: how far they start from the running point; `toPlace`: from the place.
        void Start(float toGoal, float toPlace = 0.0F)
        {
            *this = {};
            playerBest = std::numeric_limits<float>::infinity();
            Lead(toGoal, toPlace);
        }
        // The next leg, from a stop.
        void Lead(float toGoal, float toPlace)
        {
            Enter(Phase::Dash);
            closest = -1;
            stalled = 0;
            dashLimit = DashSeconds(toGoal);
            legFrom = toPlace;
        }
        // No leg would gain on the place: point it out from here.
        void Close()
        {
            last = closing = true;
            Enter(Phase::Point);
        }
        // The running point moved on with the player: progress counts afresh, with time to cover
        // the new distance, up to the overall limit.
        void Retarget(float toGoal)
        {
            if (phase != Phase::Dash) {
                return;
            }
            closest = -1;
            stalled = 0;
            dashLimit = std::min(maxDashSeconds, std::max(dashLimit, inPhase + DashSeconds(toGoal)));
        }
        void Stop() { *this = {}; }
        // One step. Sets a cue on the frame a wave or point should play, or when the player has
        // come close enough to lead on. Without `wave` they skip the waves.
        End Tick(float dt, const Sense& s, const Rules& r, Cue& cue)
        {
            cue = {};
            if (!Active()) {
                return End::None;
            }
            dt = std::clamp(std::isfinite(dt) ? dt : 0.0F, 0.0F, .25F);
            elapsed += dt;
            inPhase += dt;
            if (!std::isfinite(s.fromPlayer) || s.fromPlayer > r.leash) {
                return End::FellBehind;
            }
            if (r.lead && std::isfinite(s.playerToPlace)) {
                playerBest = std::min(playerBest, s.playerToPlace);
                if (s.playerToPlace > playerBest + strayMargin) {
                    return End::WentAnotherWay;
                }
            }
            switch (phase) {
            case Phase::Dash: {
                if (closest < 0 || (std::isfinite(s.toGoal) && s.toGoal < closest - progressStep)) {
                    closest = std::isfinite(s.toGoal) ? s.toGoal : 0.0F;
                    stalled = 0;
                }
                else {
                    stalled += dt;
                }
                const bool ahead = s.fromPlayer >= r.stopAt - farEnough;
                if (s.toGoal <= arrival || ahead || stalled >= stallSeconds || inPhase >= dashLimit) {
                    // The first stop only announces the place; after that, a leg that barely
                    // gained on it means they can get no closer.
                    last = s.toPlace <= nearPlace || (stops > 0 && legFrom - s.toPlace < minLeg);
                    ++stops;
                    Enter(r.wave ? Phase::Wave : pointed ? Phase::Wait : Phase::Point);
                }
                return End::None;
            }
            case Phase::Wave:
                if (!gestured && (s.facing || inPhase >= turnSeconds)) {
                    gestured = cue.wave = true;
                    inPhase = 0; // The gesture's own time starts now.
                }
                if (gestured && !cue.wave && inPhase >= waveSeconds) {
                    Enter(pointed ? Phase::Wait : Phase::Point);
                }
                return End::None;
            case Phase::Point:
                if (!gestured && (s.facing || inPhase >= turnSeconds)) {
                    gestured = cue.point = pointed = true;
                    inPhase = 0;
                }
                if (gestured && !cue.point && inPhase >= pointSeconds) {
                    if (!r.lead || closing) {
                        return End::Done;
                    }
                    Enter(Phase::Wait);
                }
                return End::None;
            case Phase::Wait:
                nearest = std::min(nearest, s.fromPlayer);
                if (s.fromPlayer <= r.resume) {
                    if (last) {
                        closing = true;
                        Enter(Phase::Point);
                    }
                    else {
                        cue.leadOn = true;
                    }
                    return End::None;
                }
                if (s.fromPlayer > nearest + awayMargin) {
                    return End::WentAnotherWay;
                }
                if (inPhase >= patienceSeconds) {
                    return End::StoppedFollowing;
                }
                if (r.wave && (beckon += dt) >= beckonSeconds) {
                    beckon = 0;
                    cue.wave = true;
                }
                return End::None;
            default:
                return End::None;
            }
        }

      private:
        void Enter(Phase next)
        {
            phase = next;
            inPhase = beckon = 0;
            gestured = false;
            nearest = std::numeric_limits<float>::infinity();
        }
    };

    // What the place is, in a few words; `type` is the map marker's MARKER_TYPE.
    inline const char* Noun(int type)
    {
        switch (type) {
        case 1:
            return "a city";
        case 2:
            return "a town";
        case 3:
        case 54:
            return "a settlement";
        case 4:
            return "a cave";
        case 5:
            return "a camp";
        case 6:
            return "a fort";
        case 7:
            return "a Nordic ruin";
        case 8:
            return "a Dwemer ruin";
        case 9:
            return "a shipwreck";
        case 10:
            return "a grove";
        case 12:
            return "a dragon lair";
        case 13:
            return "a farm";
        case 14:
        case 19:
            return "a mill";
        case 15:
            return "a mine";
        case 16:
        case 17:
            return "a war camp";
        case 18:
        case 55:
            return "a standing stone";
        case 20:
            return "a smelter";
        case 21:
            return "a stable";
        case 22:
        case 31:
        case 56:
            return "a tower";
        case 23:
            return "a clearing";
        case 24:
            return "a mountain pass";
        case 25:
        case 34:
        case 53:
            return "a shrine";
        case 27:
            return "a lighthouse";
        case 28:
            return "a stronghold";
        case 29:
            return "a giant camp";
        case 30:
            return "a shack";
        case 32:
            return "a dwelling";
        case 33:
            return "a dock";
        default:
            return type >= 35 && type <= 52 ? "a hold's seat" : "something";
        }
    }
    // Markers that name a place (not doors, quest targets or the player's own marker).
    inline bool IsPlace(int type) { return type >= 1 && type <= 59; }

    // A compass bearing; Skyrim's +y is north and +x east.
    inline const char* Bearing(Vec2 d)
    {
        constexpr const char* names[]{"north", "north-east", "east", "south-east",
                                      "south", "south-west", "west", "north-west"};
        const float angle = std::atan2(d.x, d.y); // 0 = north, clockwise.
        const int sector = static_cast<int>(std::lround(angle / (3.14159265F / 4))) & 7;
        return names[sector];
    }
} // namespace Wayfarer::Pathfinder

#pragma once

#include "formation_math.h"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>

namespace Wayfarer
{
    enum class TravelPhase
    {
        kIdle,
        kStarting,
        kMoving,
        kSettling
    };
    enum class TravelPace
    {
        kWalk = 0,
        kJog = 1,
        kRun = 2
    };

    struct TravelTuning
    {
        float startSpeed{24.0F};
        float stopSpeed{12.0F};
        float startDelay{0.30F};
        float stopDelay{0.45F};
        float idleRelease{8.0F};
        float directionSharpness{4.0F};
        float turnDelay{.22F};
        float individuality{1.0F};
        float lookAhead{0.85F};
        float maxPrediction{280.0F};
        float spacing{1.0F};
        float naturalStragglerDistance{250.0F};
        float goalThreshold{85.0F};
        float refreshSeconds{0.65F};
        float arrivalRadius{65.0F};
        float stuckSeconds{4.5F};
        float recoverySeconds{3.0F};
        float catchUpSeconds{2.5F};
        float catchUpBonus{150.0F};
        float maxSpeedScale{1.5F};
    };

    class TravelIntent
    {
      public:
        void Update(Vec2 a_position, Vec2 a_velocity, float a_dt, const TravelTuning& a_tuning);
        void Reset();
        void BeginOrder(Vec2 a_position, Vec2 a_direction);
        void MaintainOrder()
        {
            if (!IsMoving()) {
                phase = TravelPhase::kSettling;
                stoppedTime = 0;
            }
        }
        [[nodiscard]] bool IsMoving() const { return phase == TravelPhase::kMoving; }
        [[nodiscard]] bool CanTravel() const
        {
            return phase == TravelPhase::kMoving || phase == TravelPhase::kSettling;
        }
        [[nodiscard]] Vec2 Goal(FormationMode a_mode, int a_slot, bool a_interior, int a_side,
                                const TravelTuning& a_tuning) const;
        [[nodiscard]] Vec2 GoalAt(FormationMode a_mode, FormationSlot a_slot, bool a_interior, int a_side,
                                  const TravelTuning& a_tuning, float setback = 0.0F,
                                  float rearBlend = -1.0F) const;
        [[nodiscard]] Vec2 Direction() const { return direction; }
        [[nodiscard]] TravelIntent WithDirection(Vec2 heading) const
        {
            auto result = *this;
            result.direction = Normalize(heading, direction);
            return result;
        }
        [[nodiscard]] float Speed() const { return speed; }
        [[nodiscard]] float RoutingLead() const { return routingLead; }
        [[nodiscard]] Vec2 RouteGoal(Vec2 original, FormationMode mode, bool interior) const;
        [[nodiscard]] TravelPhase Phase() const { return phase; }

      private:
        TravelPhase phase{TravelPhase::kIdle};
        Vec2 direction{0.0F, 1.0F};
        Vec2 acceptedDirection{0.0F, 1.0F}, turnCandidate{0.0F, 1.0F};
        float turnTime{};
        Vec2 anchor{};
        float speed{0.0F};
        float lead{0.0F};
        float routingLead{0.0F};
        float movingTime{0.0F};
        float stoppedTime{0.0F};
    };

    [[nodiscard]] FormationSlot FormationPosition(FormationMode a_mode, int a_slot);
    [[nodiscard]] FormationSlot NaturalPosition(FormationMode mode, int slot, std::uint32_t actorID);

    struct TravelRole
    {
        FormationSlot offset;
        bool straggler{};
    };
    int NaturalStragglerCount(int partySize, std::uint32_t seed, float distance);
    TravelRole PartyRole(FormationMode mode, int role, int partySize, std::uint32_t seed, float distance,
                         std::uint32_t actorID = 0);

    struct TravelGoalState
    {
        Vec2 goal{};
        Vec2 lastActor{};
        float sinceCommit{100.0F};
        float stalledTime{0.0F};
        float recoveryTime{0.0F};
        TravelPace pace{TravelPace::kWalk};
        bool active{false};
        Vec2 submittedGoal{};
        float sinceRepath{100.0F};
        bool hasRoute{};
        TravelPace submittedPace{TravelPace::kWalk};

        void Tick(Vec2 a_actor, float a_dt, bool a_ownsPackage, const TravelTuning& a_tuning);
        [[nodiscard]] bool ShouldCommit(Vec2 a_candidate, Vec2 a_actor, TravelPace a_pace,
                                        const TravelTuning& a_tuning, float a_playerSpeed = 0.0F,
                                        bool a_moving = false) const;
        void Commit(Vec2 a_candidate, Vec2 a_actor, TravelPace a_pace);
        void Release();
        [[nodiscard]] bool ShouldRepath(Vec2 actor, float playerSpeed, bool moving, bool ownsPackage,
                                        const TravelTuning& tuning) const;
        void MarkRepathed();
    };

    [[nodiscard]] TravelPace ChoosePace(float a_playerSpeed, float a_goalDistance, TravelPace a_current,
                                        bool a_moving);
    [[nodiscard]] float DesiredSpeedScale(float a_playerSpeed, float a_goalDistance, TravelPace a_pace,
                                          bool a_moving, const TravelTuning& a_tuning,
                                          float a_headingAlignment = 1.0F);
    [[nodiscard]] float SmoothSpeedScale(float a_current, float a_target, float a_dt);

    struct GroundPoint
    {
        float x{}, y{}, z{};
    };

    [[nodiscard]] std::optional<std::array<float, 3>> GroundWeights(
        GroundPoint a_point, const std::array<GroundPoint, 3>& a_triangle);

    using GroundKey = std::uint64_t;
    struct GroundTriangle
    {
        std::array<GroundPoint, 3> vertices;
        std::array<std::optional<GroundKey>, 3> neighbors;
    };
    using GroundQuery = std::function<std::optional<GroundTriangle>(GroundKey)>;
    [[nodiscard]] std::optional<GroundPoint> TraceGroundCorridor(GroundPoint a_origin, Vec2 a_goal,
                                                                 GroundKey a_start,
                                                                 const GroundQuery& a_query);

    [[nodiscard]] std::optional<GroundPoint> TraceGroundStep(GroundPoint origin, GroundPoint goal,
                                                             GroundKey start, const GroundQuery& query);
    // The ground at `goal`, reached by a straight line over connected navmesh, at any height.
    [[nodiscard]] std::optional<GroundPoint> TraceGroundExact(GroundPoint origin, Vec2 goal, GroundKey start,
                                                              const GroundQuery& query);
} // namespace Wayfarer

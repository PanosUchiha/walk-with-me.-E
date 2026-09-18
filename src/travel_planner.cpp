#include "travel_planner.h"
#include <algorithm>
#include <cmath>

namespace Wayfarer
{
    namespace
    {
        float Distance(Vec2 a, Vec2 b)
        {
            return Length({a.x - b.x, a.y - b.y});
        }
    } // namespace

    void TravelIntent::Reset()
    {
        *this = TravelIntent{};
    }

    void TravelIntent::BeginOrder(Vec2 position, Vec2 facing)
    {
        Reset();
        anchor = position;
        direction = Normalize(facing);
        acceptedDirection = turnCandidate = direction;
        phase = TravelPhase::kSettling;
    }

    void TravelIntent::Update(Vec2 a_position, Vec2 a_velocity, float a_dt, const TravelTuning& t)
    {
        const float dt = std::clamp(a_dt, 0.0F, 0.5F);
        const float rawSpeed = Length(a_velocity);
        const bool moving = rawSpeed >= (IsMoving() ? t.stopSpeed : t.startSpeed);
        if (!moving) {
            turnTime = 0;
            movingTime = 0.0F;
            stoppedTime += dt;

            if (phase == TravelPhase::kStarting) {
                phase = TravelPhase::kIdle;
            }
            if (phase == TravelPhase::kMoving && stoppedTime >= t.stopDelay) {
                phase = TravelPhase::kSettling;
            }
            if (stoppedTime >= t.idleRelease) {
                phase = TravelPhase::kIdle;
            }
            speed += (0.0F - speed) * (1.0F - std::exp(-5.0F * dt));
            return;
        }
        stoppedTime = 0.0F;
        movingTime += dt;
        if (phase == TravelPhase::kIdle) {
            phase = TravelPhase::kStarting;
            direction = Normalize(a_velocity);
            acceptedDirection = turnCandidate = direction;
            turnTime = 0;
            anchor = a_position;
            speed = rawSpeed;
            lead = 0.0F;
        }
        if (phase == TravelPhase::kStarting && movingTime < t.startDelay) {
            return;
        }
        phase = TravelPhase::kMoving;
        const auto incoming = Normalize(a_velocity, direction);
        if (t.turnDelay <= 0) {
            acceptedDirection = turnCandidate = incoming;
            turnTime = 0;
        }
        else {
            auto angle = [](Vec2 a, Vec2 b) {
                return std::abs(std::remainder(std::atan2(a.x, a.y) - std::atan2(b.x, b.y), 6.283185307F));
            };
            const float change = angle(incoming, acceptedDirection);
            if (change < .20944F) {
                turnTime = 0;
                turnCandidate = incoming;
            }
            else {

                if (turnTime <= 0 || angle(incoming, turnCandidate) > .43633F) {
                    turnCandidate = incoming;
                    turnTime = 0;
                }
                else {
                    turnCandidate = SmoothDirection(turnCandidate, incoming, dt, 8);
                }
                turnTime += dt;
                const float delay = change > 2.0944F ? std::min(t.turnDelay, .12F) : t.turnDelay;
                if (turnTime >= delay) {
                    acceptedDirection = turnCandidate;
                    turnTime = 0;
                }
            }
        }

        direction = SmoothDirection(direction, acceptedDirection, dt, t.directionSharpness);
        speed += (rawSpeed - speed) * (1.0F - std::exp(-4.0F * dt));

        const float extra = std::clamp((210.0F - speed) / 85.0F, 0.0F, 1.0F) * 140.0F;
        routingLead += (extra - routingLead) * (1.0F - std::exp(-4.0F * dt));
        const float desiredLead = std::min(speed * t.lookAhead, t.maxPrediction);
        const float rate = desiredLead > lead ? 4.0F : 0.7F;
        lead += (desiredLead - lead) * (1.0F - std::exp(-rate * dt));
        if (t.turnDelay <= 0) {
            anchor = a_position;
        }
        else {

            const auto right = RightFromForward(direction);
            const Vec2 delta{a_position.x - anchor.x, a_position.y - anchor.y};
            const float residual =
                (delta.x * right.x + delta.y * right.y) * std::exp(-dt / (t.turnDelay + .05F));
            const float limit = std::min(60.0F, rawSpeed * .2F);
            const float lateral = std::clamp(residual, -limit, limit);
            anchor = {a_position.x - right.x * lateral, a_position.y - right.y * lateral};
        }
    }

    FormationSlot FormationPosition(FormationMode a_mode, int a_slot)
    {

        static constexpr std::array<FormationSlot, PARTY_CAPACITY> party{{{35, 225},
                                                                          {-115, 135},
                                                                          {135, 100},
                                                                          {-110, -105},
                                                                          {120, -145},
                                                                          {-245, 230},
                                                                          {260, 225},
                                                                          {-265, -65},
                                                                          {275, -125},
                                                                          {25, -345}}};
        static constexpr std::array<FormationSlot, PARTY_CAPACITY> leadParty{{{30, 260},
                                                                              {-120, 190},
                                                                              {130, 150},
                                                                              {-175, 45},
                                                                              {175, 10},
                                                                              {-260, 365},
                                                                              {265, 360},
                                                                              {-340, 175},
                                                                              {350, 170},
                                                                              {20, 450}}};
        static constexpr std::array<FormationSlot, PARTY_CAPACITY> companion{{{105, 35},
                                                                              {-110, 20},
                                                                              {135, -100},
                                                                              {-130, -135},
                                                                              {35, -220},
                                                                              {-265, 55},
                                                                              {275, 45},
                                                                              {-275, -200},
                                                                              {285, -225},
                                                                              {20, -385}}};
        static constexpr std::array<FormationSlot, PARTY_CAPACITY> rear{{{35, -170},
                                                                         {-115, -240},
                                                                         {135, -285},
                                                                         {-110, -375},
                                                                         {120, -420},
                                                                         {-250, -330},
                                                                         {260, -365},
                                                                         {-250, -510},
                                                                         {270, -545},
                                                                         {30, -640}}};
        const auto index = static_cast<std::size_t>(std::clamp(a_slot, 0, PARTY_CAPACITY - 1));
        return a_mode == FormationMode::kRear        ? rear[index]
               : a_mode == FormationMode::kLead      ? leadParty[index]
               : a_mode == FormationMode::kCompanion ? companion[index]
                                                     : party[index];
    }

    Vec2 TravelIntent::Goal(FormationMode a_mode, int a_slot, bool a_interior, int a_side,
                            const TravelTuning& t) const
    {
        return GoalAt(a_mode, FormationPosition(a_mode, a_slot), a_interior, a_side, t);
    }

    FormationSlot NaturalPosition(FormationMode mode, int slot, std::uint32_t actorID)
    {

        auto position = FormationPosition(mode, slot);
        std::uint32_t seed = actorID ^ (0x9E3779B9U * (static_cast<std::uint32_t>(slot) + 1));
        seed ^= seed >> 16;
        seed *= 0x7FEB352DU;
        seed ^= seed >> 15;
        seed *= 0x846CA68BU;
        seed ^= seed >> 16;
        position.lateral += static_cast<float>(seed % 49) - 24.0F;
        position.longitudinal += static_cast<float>((seed >> 8) % 65) - 32.0F;
        if (mode == FormationMode::kLead) {
            position.longitudinal = std::max(20.0F, position.longitudinal);
        }
        if (mode == FormationMode::kRear) {
            position.longitudinal = std::min(-100.0F, position.longitudinal);
        }
        return position;
    }

    int NaturalStragglerCount(int partySize, std::uint32_t seed, float distance)
    {
        partySize = std::clamp(partySize, 0, PARTY_CAPACITY);
        if (distance <= 0 || partySize < 3) {
            return 0;
        }
        seed ^= seed >> 16;
        seed *= 0x7FEB352DU;
        seed ^= seed >> 15;
        return std::min(partySize - 2, seed % 3 == 0 ? 1 : 2);
    }

    TravelRole PartyRole(FormationMode mode, int role, int partySize, std::uint32_t seed, float distance,
                         std::uint32_t actorID)
    {
        auto base = FormationPosition(mode, role);
        auto varied = actorID ? NaturalPosition(mode, role, actorID) : base;
        const int count = NaturalStragglerCount(partySize, seed, distance);
        if (mode != FormationMode::kDynamic || !count) {
            return {varied, false};
        }
        partySize = std::clamp(partySize, 1, PARTY_CAPACITY);
        std::array<int, PARTY_CAPACITY> rearRoles{};
        for (int i = 0; i < partySize; ++i) {
            rearRoles[i] = i;
        }
        std::sort(rearRoles.begin(), rearRoles.begin() + partySize, [&](int a, int b) {
            return FormationPosition(mode, a).longitudinal < FormationPosition(mode, b).longitudinal;
        });
        int index = -1;
        for (int i = 0; i < count; ++i) {
            if (rearRoles[i] == role) {
                index = i;
            }
        }
        if (index < 0) {
            return {varied, false};
        }

        const float rearLine = std::min(-145.0F, FormationPosition(mode, rearRoles[0]).longitudinal);
        const float side = count == 2 ? (index == 0 ? -115.0F : 115.0F) : 15.0F;
        varied.lateral += side - base.lateral;
        varied.longitudinal += rearLine - base.longitudinal;
        return {varied, true};
    }

    Vec2 TravelIntent::RouteGoal(Vec2 original, FormationMode mode, bool interior) const
    {
        float extension = routingLead * (interior ? 0.65F : 1.0F);
        const float forward = (original.x - anchor.x) * direction.x + (original.y - anchor.y) * direction.y;
        if (mode == FormationMode::kRear) {
            extension = std::min(extension, std::max(0.0F, -forward * 0.35F));
        }
        return {original.x + direction.x * extension, original.y + direction.y * extension};
    }

    Vec2 TravelIntent::GoalAt(FormationMode a_mode, FormationSlot slot, bool a_interior, int a_side,
                              const TravelTuning& t, float setback, float rearBlend) const
    {
        slot.lateral *= static_cast<float>(a_side < 0 ? -1 : 1) * t.spacing * (a_interior ? 0.55F : 1.0F);
        slot.longitudinal *= t.spacing * (a_interior ? 0.65F : 1.0F);

        const float forwardPrediction = lead * (a_interior ? 0.45F : 1.0F);
        const float rearPrediction = std::min(forwardPrediction, std::max(0.0F, -slot.longitudinal * 0.35F));
        const float weight = a_mode == FormationMode::kRear ? 1.0F
                             : rearBlend < 0                ? (setback > 0 ? 1.0F : 0.0F)
                                                            : std::clamp(rearBlend, 0.0F, 1.0F);
        const float prediction = forwardPrediction + (rearPrediction - forwardPrediction) * weight;
        slot.longitudinal -= std::max(0.0F, setback) * (a_interior ? .65F : 1.0F);
        const auto right = RightFromForward(direction);
        return {anchor.x + direction.x * (slot.longitudinal + prediction) + right.x * slot.lateral,
                anchor.y + direction.y * (slot.longitudinal + prediction) + right.y * slot.lateral};
    }

    TravelPace ChoosePace(float a_speed, float a_distance, TravelPace a_current, bool a_moving)
    {

        if (a_distance > 350.0F || (a_moving && a_speed > 210.0F)) {
            return TravelPace::kRun;
        }
        if (a_current == TravelPace::kRun && (a_distance > 210.0F || (a_moving && a_speed > 165.0F))) {
            return a_current;
        }
        if (a_distance > 220.0F || (a_moving && a_speed > 125.0F)) {
            return TravelPace::kJog;
        }

        if (a_current == TravelPace::kJog && (a_distance > 185.0F || (a_moving && a_speed > 120.0F))) {
            return a_current;
        }
        return TravelPace::kWalk;
    }

    float DesiredSpeedScale(float playerSpeed, float distance, TravelPace pace, bool moving,
                            const TravelTuning& t, float alignment)
    {
        if (!moving || playerSpeed < t.stopSpeed) {
            return 1.0F;
        }

        const float nominal = pace == TravelPace::kRun ? 335.0F : pace == TravelPace::kJog ? 245.0F : 100.0F;

        const float cruiseBuffer =
            std::max(t.arrivalRadius + 40.0F, playerSpeed * (t.refreshSeconds + 0.20F));
        const float correction =
            std::clamp((distance - cruiseBuffer) / t.catchUpSeconds, -0.15F * playerSpeed, t.catchUpBonus);
        const float scale = std::clamp((playerSpeed + correction) / nominal, 0.65F, t.maxSpeedScale);

        return alignment < 0.5F ? std::min(scale, 1.0F) : scale;
    }

    float SmoothSpeedScale(float current, float target, float dt)
    {
        dt = std::clamp(dt, 0.0F, 0.5F);
        const float delta = (target - current) * (1.0F - std::exp(-4.0F * dt));
        return current + std::clamp(delta, -1.6F * dt, 0.8F * dt);
    }

    void TravelGoalState::Tick(Vec2 a_actor, float a_dt, bool a_ownsPackage, const TravelTuning& t)
    {
        static_cast<void>(a_ownsPackage);
        const float dt = std::clamp(a_dt, 0.0F, 0.5F);
        sinceCommit += dt;
        sinceRepath += dt;
        recoveryTime = std::max(0.0F, recoveryTime - dt);
        if (active && Distance(goal, a_actor) > t.arrivalRadius + 70.0F) {
            if (Distance(a_actor, lastActor) < 8.0F * dt) {
                stalledTime += dt;
            }
            else {
                stalledTime = std::max(0.0F, stalledTime - dt * 2.0F);
            }
            if (stalledTime >= t.stuckSeconds) {
                Release();
                recoveryTime = t.recoverySeconds;
            }
        }
        else {
            stalledTime = 0.0F;
        }
        lastActor = a_actor;
    }

    bool TravelGoalState::ShouldCommit(Vec2 a_candidate, Vec2 a_actor, TravelPace a_pace,
                                       const TravelTuning& t, float playerSpeed, bool moving) const
    {
        if (recoveryTime > 0.0F) {
            return false;
        }
        if (!active) {
            return true;
        }
        if (moving) {

            if (sinceCommit < 0.14F) {
                return false;
            }
            return a_pace != pace ||
                   Distance(goal, a_candidate) >= std::clamp(playerSpeed * 0.10F, 8.0F, 30.0F);
        }
        if (sinceCommit < t.refreshSeconds) {
            return false;
        }
        return Distance(goal, a_candidate) >= t.goalThreshold || a_pace != pace ||
               (Distance(goal, a_actor) <= t.arrivalRadius &&
                Distance(a_candidate, a_actor) > t.arrivalRadius + t.goalThreshold);
    }

    void TravelGoalState::Commit(Vec2 a_candidate, Vec2 a_actor, TravelPace a_pace)
    {
        goal = a_candidate;
        lastActor = a_actor;
        pace = a_pace;
        active = true;
        sinceCommit = 0.0F;
    }

    void TravelGoalState::Release()
    {
        active = false;
        sinceCommit = 100.0F;
        stalledTime = 0.0F;
        hasRoute = false;
        sinceRepath = 100.0F;
    }

    bool TravelGoalState::ShouldRepath(Vec2 actor, float playerSpeed, bool moving, bool ownsPackage,
                                       const TravelTuning& t) const
    {
        if (!active || recoveryTime > 0) {
            return false;
        }
        if (!hasRoute) {
            return true;
        }

        if (sinceRepath < 0.30F) {
            return false;
        }
        if (!ownsPackage) {
            return sinceRepath >= 1.0F;
        }
        if (pace != submittedPace) {
            return true;
        }
        const float change = Distance(goal, submittedGoal);
        if (change < 12.0F) {
            return false;
        }
        if (!moving) {
            return sinceRepath >= t.refreshSeconds && change >= t.goalThreshold;
        }
        const float remaining = Distance(submittedGoal, actor);
        const float braking = t.arrivalRadius + std::max(40.0F, playerSpeed * 0.45F);
        if (remaining <= braking && change >= 25.0F) {
            return true;
        }
        const auto oldDirection = Normalize({submittedGoal.x - actor.x, submittedGoal.y - actor.y});
        const auto newDirection = Normalize({goal.x - actor.x, goal.y - actor.y});
        const float alignment = oldDirection.x * newDirection.x + oldDirection.y * newDirection.y;

        const float sidewaysChange = std::abs((goal.x - submittedGoal.x) * oldDirection.y -
                                              (goal.y - submittedGoal.y) * oldDirection.x);
        return sidewaysChange >= std::max(35.0F, t.goalThreshold * .5F) ||
               (change >= 25.0F && alignment < 0.94F);
    }

    void TravelGoalState::MarkRepathed()
    {
        submittedGoal = goal;
        submittedPace = pace;
        hasRoute = true;
        sinceRepath = 0.0F;
    }

    std::optional<std::array<float, 3>> GroundWeights(GroundPoint p, const std::array<GroundPoint, 3>& v)
    {
        const float det = (v[1].y - v[2].y) * (v[0].x - v[2].x) + (v[2].x - v[1].x) * (v[0].y - v[2].y);
        if (std::abs(det) < 0.01F) {
            return std::nullopt;
        }
        const float a = ((v[1].y - v[2].y) * (p.x - v[2].x) + (v[2].x - v[1].x) * (p.y - v[2].y)) / det;
        const float b = ((v[2].y - v[0].y) * (p.x - v[2].x) + (v[0].x - v[2].x) * (p.y - v[2].y)) / det;
        return std::array<float, 3>{a, b, 1.0F - a - b};
    }

    static std::optional<GroundPoint> TraceGround(GroundPoint origin, Vec2 goal, GroundKey location,
                                                  const GroundQuery& query, bool exact)
    {
        const float distance = Length({goal.x - origin.x, goal.y - origin.y});
        if (!std::isfinite(distance) || !std::isfinite(origin.z) || (!exact && distance < 1.0F)) {
            return std::nullopt;
        }

        const auto initial = query(location);
        if (!initial) {
            return std::nullopt;
        }
        const auto originWeights = GroundWeights(origin, initial->vertices);
        if (!originWeights || (*originWeights)[0] < -.001F || (*originWeights)[1] < -.001F ||
            (*originWeights)[2] < -.001F) {
            return std::nullopt;
        }
        float progress = 0.0F;
        for (int step = 0; step < 128; ++step) {
            const auto tri = query(location);
            if (!tri) {
                return std::nullopt;
            }
            const auto from = GroundWeights(origin, tri->vertices);
            const auto to = GroundWeights({goal.x, goal.y, origin.z}, tri->vertices);
            if (!from || !to) {
                return std::nullopt;
            }
            float crossing = 1.0F;
            int edge = -1;
            for (int i = 0; i < 3; ++i) {
                const float delta = (*to)[i] - (*from)[i];
                if (delta >= -0.00001F || (*to)[i] >= -0.00001F) {
                    continue;
                }
                const float fraction = -(*from)[i] / delta;
                if (fraction >= progress - 0.00001F && fraction < crossing) {
                    crossing = fraction;
                    edge = (i + 1) % 3;
                }
            }
            if (edge < 0) {
                if ((*to)[0] < -0.01F || (*to)[1] < -0.01F || (*to)[2] < -0.01F) {
                    return std::nullopt;
                }
                return GroundPoint{goal.x, goal.y,
                                   (*to)[0] * tri->vertices[0].z + (*to)[1] * tri->vertices[1].z +
                                       (*to)[2] * tri->vertices[2].z};
            }
            if (!tri->neighbors[edge]) {
                if (exact) {
                    return std::nullopt;
                }
                const float safe = crossing - 35.0F / distance;
                if (safe < progress || safe * distance < 80.0F) {
                    return std::nullopt;
                }
                const float x = origin.x + (goal.x - origin.x) * safe;
                const float y = origin.y + (goal.y - origin.y) * safe;
                const auto w = GroundWeights({x, y, origin.z}, tri->vertices);
                return GroundPoint{x, y,
                                   (*w)[0] * tri->vertices[0].z + (*w)[1] * tri->vertices[1].z +
                                       (*w)[2] * tri->vertices[2].z};
            }
            progress = crossing;
            location = *tri->neighbors[edge];
        }
        return std::nullopt;
    }

    std::optional<GroundPoint> TraceGroundCorridor(GroundPoint origin, Vec2 goal, GroundKey start,
                                                   const GroundQuery& query)
    {
        return TraceGround(origin, goal, start, query, false);
    }

    std::optional<GroundPoint> TraceGroundStep(GroundPoint origin, GroundPoint goal, GroundKey start,
                                               const GroundQuery& query)
    {
        if (!std::isfinite(goal.z)) {
            return {};
        }
        const auto floor = TraceGround(origin, {origin.x, origin.y}, start, query, true);
        const auto end = TraceGround(origin, {goal.x, goal.y}, start, query, true);
        if (!floor || !end || std::abs(floor->z - origin.z) > 96 || std::abs(end->z - floor->z) > 32) {
            return {};
        }

        const float height = origin.z + end->z - floor->z;
        if (std::abs(height - goal.z) > 48) {
            return {};
        }
        return GroundPoint{goal.x, goal.y, height};
    }
} // namespace Wayfarer

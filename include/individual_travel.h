#pragma once
#include "travel_planner.h"
#include <algorithm>
#include <cmath>

namespace Wayfarer
{
    inline bool UrgentIndividualTravel(float playerDistance, float nearDistance, bool hasGoal,
                                       float goalDistance, float routeLead)
    {

        return playerDistance > nearDistance && (!hasGoal || goalDistance > 350 + routeLead);
    }
    struct TravelDisposition
    {
        float reaction{}, turnDelay{}, turnSharpness{}, cruise{}, catchGap{}, settleGap{}, catchResponse{},
            period{}, phase{};
    };
    inline TravelDisposition MakeTravelDisposition(std::uint32_t identity)
    {
        auto sample = [seed = identity]() mutable {
            seed += 0x9e3779b9U;
            auto h = seed;
            h = (h ^ (h >> 16)) * 0x7feb352dU;
            h = (h ^ (h >> 15)) * 0x846ca68bU;
            h ^= h >> 16;
            return static_cast<float>(h & 0xffffU) / 65535.0F;
        };
        return {.07F + .38F * sample(),  .05F + .23F * sample(), 2.2F + 4.0F * sample(),
                .925F + .07F * sample(), 65 + 75 * sample(),     18 + 25 * sample(),
                .8F + .45F * sample(),   9 + 13 * sample(),      6.2831853F * sample()};
    }

    class IndividualTravel
    {
      public:
        bool Initialize(std::uint32_t identity)
        {
            if (initialized) {
                return false;
            }
            profile = MakeTravelDisposition(identity);
            cycle = profile.phase;
            initialized = true;
            return true;
        }
        void ResetMotion()
        {
            haveHeading = false;
            wasMoving = false;
            movingAge = turnAge = 0;
            catching = false;
        }
        void Update(Vec2 sharedHeading, float dt, bool moving, bool urgent, bool interior, float strength)
        {
            if (!std::isfinite(dt) || dt <= 0) {
                return;
            }
            dt = std::min(dt, .5F);
            strength = std::clamp(strength, 0.0F, 1.0F);
            if (!haveHeading) {
                heading = targetHeading = Normalize(sharedHeading);
                haveHeading = true;
            }
            if (!moving) {
                movingAge = 0;
                wasMoving = false;
                catching = false;
                return;
            }
            if (!wasMoving) {
                movingAge = turnAge = 0;
                wasMoving = true;
            }
            movingAge += dt;
            cycle = std::fmod(cycle + dt * 6.2831853F / std::max(1.0F, profile.period), 6.2831853F);
            if (strength <= 0 || urgent) {
                heading = targetHeading = sharedHeading;
                turnAge = 0;
                return;
            }
            turnAge += dt;
            if (turnAge >= profile.turnDelay * strength) {
                targetHeading = sharedHeading;
                turnAge = 0;
            }
            heading =
                SmoothDirection(heading, targetHeading, dt, profile.turnSharpness / std::max(.1F, strength));

            const float yaw = std::atan2(sharedHeading.x, sharedHeading.y);
            const float difference = std::remainder(std::atan2(heading.x, heading.y) - yaw, 6.2831853F);
            const float limit = (interior ? .349066F : .785398F) * strength;
            heading = ForwardFromYaw(yaw + std::clamp(difference, -limit, limit));
        }
        bool Ready(bool urgent, float strength) const
        {
            return urgent || !wasMoving || movingAge >= profile.reaction * std::clamp(strength, 0.0F, 1.0F);
        }
        Vec2 Direction() const { return heading; }
        bool CatchingUp() const { return catching; }
        const TravelDisposition& Disposition() const { return profile; }

        float SpeedScale(float speed, float distance, TravelPace pace, bool moving,
                         const TravelTuning& tuning, float alignment, bool urgent)
        {
            const float ordinary = DesiredSpeedScale(speed, distance, pace, moving, tuning, alignment);
            const float strength = std::clamp(tuning.individuality, 0.0F, 1.0F);
            if (!moving || strength <= 0) {
                catching = false;
                return ordinary;
            }
            if (urgent) {
                catching = true;
                return ordinary;
            }
            const float buffer =
                std::max(tuning.arrivalRadius + 40.0F, speed * (tuning.refreshSeconds + .20F));
            const float gap = distance - buffer, paceFactor = std::clamp(speed / 120.0F, .8F, 1.7F);
            if (gap > profile.catchGap * paceFactor) {
                catching = true;
            }
            else if (gap < profile.settleGap * paceFactor) {
                catching = false;
            }
            auto personalTuning = tuning;
            personalTuning.catchUpSeconds =
                std::clamp(tuning.catchUpSeconds * profile.catchResponse, 1.0F, 8.0F);

            const float cruising = profile.cruise + .018F * std::sin(cycle);
            const float personal = DesiredSpeedScale(speed * (catching ? 1.0F : cruising),
                                                     catching ? distance : std::min(distance, buffer), pace,
                                                     moving, personalTuning, alignment);
            return ordinary + (personal - ordinary) * strength;
        }

      private:
        TravelDisposition profile{};
        bool initialized{}, haveHeading{}, wasMoving{}, catching{};
        Vec2 heading{0, 1}, targetHeading{0, 1};
        float movingAge{}, turnAge{}, cycle{};
    };
} // namespace Wayfarer

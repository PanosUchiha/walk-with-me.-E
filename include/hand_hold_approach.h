#pragma once
#include "hand_hold_ik.h"

namespace Wayfarer::HandHolding
{

    inline float SeekSpacing(float connectDistance)
    {
        return std::clamp(connectDistance * .5F, 35.0F, 40.0F);
    }
    // Classic's pair spacing. The 35-40 unit SeekSpacing put the two bodies shoulder to
    // shoulder; the upper bound keeps the shared grip inside both arms' reach.
    inline float ClassicSpacing(float spacing)
    {
        return std::isfinite(spacing) ? std::clamp(spacing, 36.0F, 56.0F) : 46.0F;
    }
    inline int ClassicSide(int preference, float lateral)
    {
        return preference ? (preference < 0 ? -1 : 1) : (lateral < 0 ? -1 : 1);
    }
    inline bool ReadyAtHand(float lateral, float along, int side, float spacing)
    {
        return std::isfinite(lateral) && std::isfinite(along) && std::abs(lateral * side - spacing) <= 5 &&
               std::abs(along) <= 10;
    }

    inline bool IdleContactReady(float playerSpeed, float followerSpeed, float graphSpeed, float headingError)
    {
        return std::isfinite(playerSpeed) && std::isfinite(followerSpeed) && std::isfinite(graphSpeed) &&
               std::isfinite(headingError) && std::abs(playerSpeed) <= 5 && std::abs(followerSpeed) <= 5 &&
               std::abs(graphSpeed) <= 5 && std::abs(headingError) < .2F;
    }
    inline IK::Point SeekPoint(IK::Point player, float heading, int side, float spacing)
    {
        return player + IK::Point{std::cos(heading), -std::sin(heading), 0} * (side * spacing);
    }
    inline float SeekHeading(float current, float target, float dt)
    {
        constexpr float tau = 6.283185307F;
        return std::remainder(
            current + std::clamp(std::remainder(target - current, tau), -tau * dt, tau * dt), tau);
    }

    class SeekGait
    {
      public:
        void Reset() { samples = 0; }
        bool Ready() const { return samples >= 2; }
        bool Observe(float dt, bool graphWritten, float graphSpeed, float writtenSpeed, float nativeDistance)
        {
            if (!std::isfinite(dt) || dt <= 0 || dt > .25F) {
                return Ready();
            }
            const bool freshGraph = std::isfinite(graphSpeed) && std::isfinite(writtenSpeed) &&
                                    graphSpeed > 5 && std::abs(graphSpeed - writtenSpeed) > .1F;
            const bool nativeStep = std::isfinite(nativeDistance) && nativeDistance / dt > 5;
            if (graphWritten && (freshGraph || nativeStep) && samples < 2) {
                ++samples;
            }
            return Ready();
        }

      private:
        unsigned samples{};
    };

    class SeekMotion
    {
      public:
        void Reset() { *this = {}; }
        bool Active() const { return active; }
        bool Integrating() const { return integrating; }
        IK::Point Velocity() const { return velocity; }
        float Heading() const { return heading; }
        bool Settled(IK::Point playerVelocity, float playerHeading) const
        {
            playerVelocity.z = 0;
            return integrating && IK::Length(velocity - playerVelocity) < 25 &&
                   std::abs(std::remainder(heading - playerHeading, 6.283185307F)) < .2F;
        }
        std::optional<IK::Point> Advance(IK::Point follower, IK::Point player, IK::Point target,
                                         IK::Point playerVelocity, IK::Point nativeVelocity,
                                         float actorHeading, float playerHeading, float dt,
                                         bool animationReady)
        {
            if (!IK::Finite(follower) || !IK::Finite(player) || !IK::Finite(target) ||
                !IK::Finite(playerVelocity) || !IK::Finite(nativeVelocity) || !std::isfinite(actorHeading) ||
                !std::isfinite(playerHeading) || !std::isfinite(dt) || dt <= 0 || dt > .25F ||
                std::abs(follower.z - player.z) > 48 ||
                IK::Length(follower - player) > (active ? 160.0F : 120.0F) ||
                IK::Length(target - follower) > (active ? 130.0F : 90.0F)) {
                Reset();
                return {};
            }
            playerVelocity.z = nativeVelocity.z = 0;
            if (!active) {
                active = true;
                planned = follower;
                velocity = nativeVelocity;
                heading = actorHeading;
            }
            age += dt;
            const bool ready = age >= .12F && animationReady;
            if (!integrating) {
                planned = follower;
                if (ready) {
                    integrating = true;
                }
            }
            auto error = target - planned - playerVelocity * dt;
            error.z = 0;
            const float distance = IK::Length(error);
            const auto correction = IK::Unit(error) * std::min(110.0F, distance * 3.0F);
            const auto desired = playerVelocity + correction;
            const auto change = desired - velocity;
            const float amount = IK::Length(change), limit = 420 * dt;
            velocity = velocity + (amount > limit ? change * (limit / amount) : change);
            const float speed = IK::Length(velocity);
            const float travelHeading = speed > 5 ? std::atan2(velocity.x, velocity.y) : playerHeading;
            const float alignment = 1 - std::clamp(distance / 24.0F, 0.0F, 1.0F);
            const float desiredHeading =
                travelHeading + std::remainder(playerHeading - travelHeading, 6.283185307F) * alignment;
            heading = SeekHeading(heading, desiredHeading, dt * .5F);
            if (!ready) {
                return follower;
            }
            planned = planned + velocity * dt;
            planned.z = follower.z;
            auto residual = planned - follower;
            residual.z = 0;
            if (IK::Length(residual) > 24) {
                Reset();
                return {};
            }
            return planned;
        }

      private:
        bool active{}, integrating{};
        float age{}, heading{};
        IK::Point planned{}, velocity{};
    };
} // namespace Wayfarer::HandHolding

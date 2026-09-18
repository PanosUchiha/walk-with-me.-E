#pragma once
#include "hand_hold_ik.h"

namespace Wayfarer::HandHolding
{

    class Attachment
    {
      public:
        void Reset()
        {
            active = false;
            velocity = {};
        }
        bool Capture(IK::Point player, IK::Point follower, float heading, IK::Point initialVelocity = {})
        {
            if (!IK::Finite(player) || !IK::Finite(follower) || !IK::Finite(initialVelocity) ||
                !std::isfinite(heading)) {
                Reset();
                return false;
            }
            yaw = heading;
            lastPlayer = player;
            lastTarget = follower;
            velocity = initialVelocity;
            velocity.z = 0;
            const auto d = follower - player;
            lateral = IK::Dot(d, Right());
            along = IK::Dot(d, Forward());
            active = true;
            return true;
        }
        std::optional<IK::Point> Advance(float dt, IK::Point player, float heading, bool sprinting)
        {
            if (!active) {
                return {};
            }
            if (sprinting || !std::isfinite(dt) || dt <= 0 || dt > .25F || !IK::Finite(player) ||
                !std::isfinite(heading) || IK::Length(player - lastPlayer) > 128) {
                Reset();
                return {};
            }
            constexpr float tau = 6.283185307F;
            const float turn = std::remainder(heading - yaw, tau);
            yaw = std::remainder(yaw + std::clamp(turn, -tau * dt, tau * dt), tau);
            lastPlayer = player;
            const auto target = player + Right() * lateral + Forward() * along;

            velocity = (target - lastTarget) * (1 / dt);
            lastTarget = target;
            return target;
        }
        IK::Point Forward() const { return {std::sin(yaw), std::cos(yaw), 0}; }
        IK::Point Right() const { return {std::cos(yaw), -std::sin(yaw), 0}; }
        float Yaw() const { return yaw; }
        bool Active() const { return active; }
        IK::Point Velocity() const { return velocity; }

      private:
        bool active{};
        float yaw{}, lateral{}, along{};
        IK::Point lastPlayer, lastTarget, velocity;
    };
} // namespace Wayfarer::HandHolding

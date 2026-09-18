#pragma once
#include <algorithm>
#include <cmath>

namespace Wayfarer
{
    struct FollowerCatchup
    {
        float distantTime{}, cooldown{};
        bool Update(float dt, bool permitted, float distance, float threshold)
        {
            if (!std::isfinite(dt) || dt <= 0) {
                return false;
            }
            cooldown = std::max(0.0F, cooldown - dt);
            if (!permitted || !std::isfinite(distance) || !std::isfinite(threshold) || distance < threshold) {
                distantTime = 0;
                return false;
            }
            distantTime += dt;
            return distantTime >= 2.0F && cooldown <= 0;
        }
        void Attempt(bool moved)
        {
            distantTime = 0;
            cooldown = moved ? 15.0F : 2.0F;
        }
    };
} // namespace Wayfarer

#pragma once
#include <algorithm>
#include <cmath>

namespace Wayfarer::HandHolding
{

    class CollisionOwnership
    {
      public:
        void Acquire(bool alreadyDisabled)
        {
            if (!active) {
                active = true;
                introduced = !alreadyDisabled;
            }
        }
        bool Release()
        {
            const bool restore = active && introduced;
            active = introduced = false;
            return restore;
        }

      private:
        bool active{}, introduced{};
    };
    inline float PoseSpeed(float horizontalSpeed)
    {
        return std::isfinite(horizontalSpeed) ? std::clamp(horizontalSpeed, 0.0F, 1000.0F) : 0;
    }
} // namespace Wayfarer::HandHolding

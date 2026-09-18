#pragma once
#include <algorithm>
#include <cmath>
#include <span>

namespace Wayfarer::CommandGesture
{
    struct FollowerBearing
    {
        float right{}, forward{};
    };

    inline float GlanceDegrees(std::span<const FollowerBearing> followers, int preferredSide = 1)
    {
        int count{}, behind{}, left{}, right{}, rearLeft{}, rearRight{};
        float sideSum{}, rearSideSum{};
        for (auto [x, y] : followers) {
            const float distance = std::hypot(x, y);
            if (!std::isfinite(distance) || distance < 20 || distance > 900) {
                continue;
            }
            ++count;
            const float side = x / distance;
            const bool rear = y / distance < -.25F;
            if (rear) {
                ++behind;
                rearSideSum += side;
            }
            sideSum += side;
            if (side < -.12F) {
                ++left;
                if (rear) {
                    ++rearLeft;
                }
            }
            if (side > .12F) {
                ++right;
                if (rear) {
                    ++rearRight;
                }
            }
        }
        if (!count) {
            return 0;
        }
        if (behind * 2 > count) {
            const int side = rearLeft != rearRight          ? (rearRight > rearLeft ? 1 : -1)
                             : std::abs(rearSideSum) > .12F ? (rearSideSum > 0 ? 1 : -1)
                                                            : (preferredSide < 0 ? -1 : 1);
            return 28.0F * side;
        }
        if (left == right && std::abs(sideSum) < .12F) {
            return 0;
        }
        const int side = left != right ? (right > left ? 1 : -1) : (sideSum > 0 ? 1 : -1);
        return side * 8.0F * std::clamp(std::abs(sideSum) / count, 0.0F, 1.0F);
    }

    inline float GlanceWeight(float elapsed, float duration)
    {
        if (!std::isfinite(elapsed) || !std::isfinite(duration) || duration <= .5F) {
            return 0;
        }
        auto smooth = [](float x) {
            x = std::clamp(x, 0.0F, 1.0F);
            return x * x * (3 - 2 * x);
        };
        return smooth((elapsed - .05F) / .3F) * smooth((duration - .05F - elapsed) / .4F);
    }
} // namespace Wayfarer::CommandGesture

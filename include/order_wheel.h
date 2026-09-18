#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace Wayfarer
{
    namespace Wheel
    {
        constexpr float pi = 3.14159265359F;
        constexpr int count = 6;
        inline float Width(float screenWidth, float screenHeight)
        {
            return std::min(680.0F * std::clamp(screenHeight / 1080.0F, .75F, 1.5F),
                            std::min(screenWidth * .86F, screenHeight * .72F));
        }
        constexpr float windowHeight = 1.30F;
        struct DockBox
        {
            float x, y, w, h;
        };
        inline DockBox UtilityBox(int index, float width)
        {
            return {width * (index ? .51F : .07F), width * .94F, width * .42F, width * .12F};
        }
        constexpr float sectorDegrees = 360.0F / count;
        constexpr std::array<int, count> clockwise{1, 2, 3, 5, 4, 0};
        constexpr std::array<int, count> slots{5, 0, 1, 2, 4, 3};
        inline float Angle(int mode)
        {
            return (-90.0F + sectorDegrees * slots[std::clamp(mode, 0, count - 1)]) * pi / 180;
        }
        inline float Difference(float a, float b)
        {
            return std::abs(std::remainder(a - b, 2 * pi));
        }
        inline int Direction(float x, float y)
        {
            int best = 0;
            const float angle = std::atan2(y, x);
            for (int i = 1; i < count; ++i) {
                if (Difference(angle, Angle(i)) + 0.00001F < Difference(angle, Angle(best))) {
                    best = i;
                }
            }
            return best;
        }
        inline int Mouse(float x, float y, float radius)
        {
            const float d = std::hypot(x, y);
            if (d < radius * .51F || d > radius) {
                return -1;
            }
            return Direction(x, y);
        }
        struct Stick
        {
            bool engaged{};
            int Update(float x, float y, int focus)
            {
                if (!std::isfinite(x) || !std::isfinite(y)) {
                    engaged = false;
                    return focus;
                }
                const float magnitude = std::hypot(x, y), threshold = engaged ? .22F : .28F;
                if (magnitude < threshold) {
                    engaged = false;
                    return focus;
                }
                const int candidate = Direction(x, y);
                const float a = std::atan2(y, x);
                const bool change =
                    !engaged || Difference(a, Angle(candidate)) + 8 * pi / 180 < Difference(a, Angle(focus));
                engaged = true;
                return change ? candidate : focus;
            }
        };
        inline float Once(float t, float duration)
        {
            return std::clamp(t / duration, 0.0F, 1.0F);
        }
        inline float Smooth(float t)
        {
            t = std::clamp(t, 0.0F, 1.0F);
            return t * t * (3 - 2 * t);
        }
        inline float EaseOut(float t)
        {
            t = 1 - std::clamp(t, 0.0F, 1.0F);
            return 1 - t * t * t;
        }
        inline float Follow(float value, float target, float dt, float speed = 18)
        {
            return target + (value - target) * std::exp(-speed * std::max(0.0F, dt));
        }
        struct Motion
        {
            float elapsed{}, visibility{}, closeStart{}, closeElapsed{};
            bool closing{};
            std::array<float, count> highlight{};
            std::array<float, count> centerWeight{};
            float angle{-pi / 2};
            void Open(int focus)
            {
                *this = {};
                angle = Angle(focus);
                highlight[focus] = 1;
                centerWeight[focus] = 1;
            }
            void Close()
            {
                if (!closing) {
                    closing = true;
                    closeStart = visibility;
                    closeElapsed = 0;
                }
            }
            void Update(float dt, int focus)
            {
                if (!std::isfinite(dt) || dt <= 0) {
                    return;
                }
                dt = std::min(dt, .1F);
                elapsed += dt;
                if (closing) {
                    closeElapsed += dt;
                    visibility = closeStart * (1 - Smooth(closeElapsed / .18F));
                }
                else {
                    visibility = EaseOut(elapsed / .32F);
                }
                for (int i = 0; i < count; ++i) {
                    highlight[i] = Follow(highlight[i], i == focus ? 1.0F : 0.0F, dt);
                    centerWeight[i] = Follow(centerWeight[i], i == focus ? 1.0F : 0.0F, dt, 22);
                }
                angle += std::remainder(Angle(focus) - angle, 2 * pi) * (1 - std::exp(-22 * dt));
            }
            bool Finished() const { return closing && closeElapsed >= .18F; }
            float Scale() const { return .94F + .06F * visibility; }
        };
    } // namespace Wheel
} // namespace Wayfarer

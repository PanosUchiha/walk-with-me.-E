#pragma once
#include "order_wheel.h"
#include <algorithm>

namespace Wayfarer::CompanionMenu
{
    struct Box
    {
        float x, y, w, h;
    };
    inline float Width(float x, float y)
    {
        return std::min({600.0F * std::clamp(y / 1080.0F, .75F, 1.5F), x * .86F, y * 1.05F});
    }
    constexpr float height = .69F;
    constexpr Box Action(int index, float w)
    {
        return {w * (index ? .515F : .07F), w * .29F, w * .415F, w * .27F};
    }
    constexpr Box Recovery(float w)
    {
        return {w * .07F, w * .59F, w * .415F, w * .075F};
    }
    constexpr Box Cancel(float w)
    {
        return {w * .515F, w * .59F, w * .415F, w * .075F};
    }
    struct ButtonMotion
    {
        float hover{}, press{}, flash{};
        void Update(float dt, bool over, bool held, bool clicked)
        {
            hover = Wheel::Follow(hover, over ? 1.0F : 0.0F, dt, 14);
            press = Wheel::Follow(press, held ? 1.0F : 0.0F, dt, 26);
            flash = clicked ? 1.0F : Wheel::Follow(flash, 0, dt, 9);
        }
    };
    struct Motion
    {
        float elapsed{}, exitElapsed{}, exitStart{1}, visibility{};
        bool closing{};
        std::array<ButtonMotion, 4> buttons{};
        void Close()
        {
            if (!closing) {
                closing = true;
                exitStart = visibility;
            }
        }
        void Update(float dt)
        {
            if (!std::isfinite(dt) || dt <= 0) {
                return;
            }
            dt = std::min(dt, .05F);
            elapsed += dt;
            if (closing) {
                exitElapsed += dt;
            }
            visibility = closing ? exitStart * (1 - Wheel::Smooth(exitElapsed / .20F))
                                 : Wheel::EaseOut(elapsed / .32F);
        }
        float Reveal(float delay) const
        {
            return Wheel::EaseOut((elapsed - delay) / .28F) * (closing ? visibility : 1.0F);
        }
        bool Finished() const { return closing && exitElapsed >= .20F; }
    };
} // namespace Wayfarer::CompanionMenu

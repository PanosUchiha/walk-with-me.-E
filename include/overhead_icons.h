#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace Wayfarer::Overhead
{
    struct Pose
    {
        float scale{1}, rise{}, alpha{.80F};
    };

    // Render-thread state, keyed by actor rather than by position in a marker snapshot.
    // A missing marker is removed immediately: no stale icon over a hidden/departed NPC.
    class Animation
    {
      public:
        void BeginFrame(float dt)
        {
            const float step = std::isfinite(dt) ? std::clamp(dt, 0.0F, .1F) : 0.0F;
            for (auto& [id, state] : states) {
                state.elapsed += step;
                state.seen = false;
            }
        }

        Pose Show(std::uint32_t actor, bool animated = true)
        {
            auto& state = states[actor];
            state.seen = true;
            if (!animated) {
                return {};
            }
            const float t = state.elapsed;
            const float u = std::clamp(t / .42F, 0.0F, 1.0F);
            const float v = u - 1;
            const float pop = 1 + 2.70158F * v * v * v + 1.70158F * v * v;
            const float settle = u * u * (3 - 2 * u);
            const float bob = std::sin(t * 2.2F);
            // Rise is in fractions of icon size, so animation follows HUD resolution scaling.
            return {.scale = .65F + .35F * pop,
                    .rise = .28F * (1 - settle) - .055F * bob * settle,
                    .alpha = std::clamp(t / .18F, 0.0F, 1.0F) * (.80F + .025F * bob)};
        }

        void EndFrame()
        {
            std::erase_if(states, [](const auto& entry) { return !entry.second.seen; });
        }
        void Clear() { states.clear(); }

      private:
        struct State
        {
            float elapsed{};
            bool seen{};
        };
        std::unordered_map<std::uint32_t, State> states;
    };
} // namespace Wayfarer::Overhead

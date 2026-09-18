#pragma once
#include <algorithm>
#include <cstdint>
namespace Wayfarer
{
    struct HudNotice
    {
        std::uint64_t key{};
        bool initialized{};
        float elapsed{};
        void Update(std::uint64_t value, float dt, bool restart = false)
        {
            if (!initialized || value != key || restart) {
                key = value;
                initialized = true;
                elapsed = 0;
            }
            else {
                elapsed += std::max(0.0F, dt);
            }
        }
        float Progress() const
        {
            const float t = std::clamp((elapsed - 5.0F) / 2.0F, 0.0F, 1.0F);
            return t * t * (3 - 2 * t);
        }
        float Alpha() const { return 1 - Progress(); }
        float Slide() const { return 80 * Progress(); }
    };

    struct HudNoticePresentation
    {
        std::uint64_t generation{};
        int mode{};
        bool initialized{}, resting{}, compact{};
        void Update(std::uint64_t nextGeneration, int nextMode, bool nextResting, bool explicitOrder,
                    bool reset = false)
        {
            if (reset || !initialized || generation != nextGeneration || mode != nextMode || explicitOrder) {
                compact = false;
            }
            else if (resting != nextResting) {
                compact = nextMode >= 0 && nextMode < 4;
            }
            generation = nextGeneration;
            mode = nextMode;
            resting = nextResting;
            initialized = true;
        }
    };
} // namespace Wayfarer

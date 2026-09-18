#pragma once
#include "rest_pose.h"
#include <algorithm>

namespace Wayfarer
{

    struct ActivitySearch
    {
        float quiet{}, cooldown{};
        std::uint32_t sequence{};
        void Reset() { quiet = cooldown = 0; }
        bool Ready(float dt, bool plainStanding, std::uint32_t actor)
        {
            dt = std::clamp(dt, 0.0F, .5F);
            cooldown = std::max(0.0F, cooldown - dt);
            if (!plainStanding) {
                quiet = 0;
                return false;
            }
            quiet += dt;
            return cooldown <= 0 && quiet >= 8.0F + RestPose::Mix(actor) % 9;
        }
        void Attempt(std::uint32_t actor)
        {
            quiet = 0;
            cooldown = 35.0F + RestPose::Mix(actor + ++sequence * 7919) % 16;
        }
    };

    struct RestRecovery
    {
        bool walking{};
        float quiet{}, elapsed{}, timeout{20};
        std::uint32_t sequence{};
        void Reset()
        {
            walking = false;
            quiet = elapsed = 0;
            timeout = 20;
        }
        void Finish()
        {
            Reset();
            ++sequence;
        }
        bool Ready(float dt, bool plainStanding, bool expanded, float delay, std::uint32_t actor)
        {
            if (!plainStanding || delay <= 0) {
                quiet = 0;
                return false;
            }
            quiet += std::clamp(dt, 0.0F, .5F);
            return expanded &&
                   quiet >= delay + static_cast<float>(RestPose::Mix(actor + sequence * 7919) % 16);
        }
        void Begin(float budget = 20)
        {
            walking = true;
            elapsed = quiet = 0;
            timeout = std::clamp(budget, 20.0F, 60.0F);
        }
        bool Done(float dt, bool arrived, bool interrupted)
        {
            elapsed += std::clamp(dt, 0.0F, .5F);
            return arrived || interrupted || elapsed >= timeout;
        }
    };
} // namespace Wayfarer

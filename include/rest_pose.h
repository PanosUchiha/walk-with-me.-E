#pragma once
#include <cstdint>

namespace Wayfarer
{
    struct RestPose
    {
        int kind{};
        bool started{}, initialized{};
        float elapsed{}, prepare{}, cooldown{}, duration{};
        std::uint32_t sequence{};
        static std::uint32_t Mix(std::uint32_t x)
        {
            x ^= x >> 16;
            x *= 0x7feb352d;
            x ^= x >> 15;
            return x;
        }
        void Init(std::uint32_t id)
        {
            if (!initialized) {
                initialized = true;
                cooldown = 12.0F + Mix(id) % 13;
            }
        }
        void Begin(int choice, std::uint32_t id)
        {
            kind = choice;
            started = false;
            elapsed = prepare = 0;
            duration = 25.0F + Mix(id + ++sequence * 7919) % 21;
        }
        void End(std::uint32_t id)
        {
            kind = 0;
            started = false;
            elapsed = prepare = 0;
            cooldown = 25.0F + Mix(id + sequence * 3571) % 21;
        }
        bool Tick(float dt, bool ready)
        {
            if (!kind) {
                cooldown -= dt;
                return false;
            }
            if (!started) {
                prepare += dt;
                if (ready) {
                    started = true;
                    elapsed = 0;
                }
                return prepare >= 12 && !started;
            }
            elapsed += dt;
            return !ready || elapsed >= duration;
        }
    };
} // namespace Wayfarer

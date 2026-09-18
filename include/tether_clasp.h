#pragma once
#include <atomic>
#include <cstdint>

namespace Wayfarer::HandHolding::Clasp
{
    class Selection
    {
        std::atomic<std::uint64_t> pair{};

      public:
        void Clear() { pair.store(0); }
        void Set(std::uint32_t player, std::uint32_t follower, bool playerLeft)
        {
            if (!player || !follower || player == follower) {
                Clear();
                return;
            }
            const auto left = playerLeft ? player : follower, right = playerLeft ? follower : player;
            pair.store(static_cast<std::uint64_t>(left) | (static_cast<std::uint64_t>(right) << 32));
        }
        bool Matches(std::uint32_t actor, bool left) const
        {
            const auto snapshot = pair.load();
            return actor && actor == static_cast<std::uint32_t>(left ? snapshot : snapshot >> 32);
        }
    };
    void Register();
    bool Available();
    void Select(std::uint32_t player, std::uint32_t follower, bool playerLeft);
    void Clear();
} // namespace Wayfarer::HandHolding::Clasp

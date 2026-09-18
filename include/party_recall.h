#pragma once
#include <cstdint>

namespace Wayfarer
{
    enum class RecallDispatch
    {
        None,
        Wait,
        Run,
        Cancel
    };
    struct PartyRecallRequest
    {
        bool pending{};
        std::uint64_t generation{}, requestedAt{};
        bool Request(std::uint64_t save, std::uint64_t now)
        {
            if (pending) {
                return false;
            }
            pending = true;
            generation = save;
            requestedAt = now;
            return true;
        }
        RecallDispatch Poll(std::uint64_t save, std::uint64_t now, bool menuOpen, bool safe)
        {
            if (!pending) {
                return RecallDispatch::None;
            }
            if (save != generation || now < requestedAt || now - requestedAt > 5000 || !safe) {
                pending = false;
                return RecallDispatch::Cancel;
            }
            if (menuOpen) {
                return RecallDispatch::Wait;
            }
            pending = false;
            return RecallDispatch::Run;
        }
    };
} // namespace Wayfarer

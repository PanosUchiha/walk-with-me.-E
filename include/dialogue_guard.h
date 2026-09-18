#pragma once
#include <cstdint>

namespace Wayfarer
{

    struct DialogueGuard
    {
        std::uint64_t resumeAt{};
        bool Check(bool active, std::uint64_t now)
        {
            if (active) {
                resumeAt = now + 1500;
            }
            return active || now < resumeAt;
        }
    };
} // namespace Wayfarer

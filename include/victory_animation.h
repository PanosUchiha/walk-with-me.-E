#pragma once
#include <cstdint>

namespace Wayfarer::Victory::Animation
{
    void Register();
    bool Available();
    void Select(std::uint32_t actor, bool selected);
    bool WasObserved(std::uint32_t actor);
    void Clear();
} // namespace Wayfarer::Victory::Animation

#pragma once
#include <cstdint>

namespace Wayfarer::Victory::Animation
{
    void Register();
    bool Available();
    void Select(std::uint32_t actor, bool selected);
    void Clear();
} // namespace Wayfarer::Victory::Animation

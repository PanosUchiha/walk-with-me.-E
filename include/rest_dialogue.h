#pragma once
#include <cstdint>
#include <vector>

namespace Wayfarer::RestDialogue
{
    struct Marker
    {
        float x{}, y{}; // Normalized screen coordinates; no game pointers on the render thread.
        std::uint32_t actor{};
    };
    void Update(float dt);
    void Reset();
    std::vector<Marker> ReadMarkers();
} // namespace Wayfarer::RestDialogue

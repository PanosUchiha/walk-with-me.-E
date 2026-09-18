#pragma once
#include <cstdint>

namespace Wayfarer
{
    constexpr bool CanGrantMovement(bool requested, bool compatible, std::uint32_t actor,
                                    std::uint32_t aliasActor, std::uint32_t marker, std::uint32_t aliasMarker)
    {
        return requested && compatible && actor && marker && actor == aliasActor && marker == aliasMarker;
    }

    // Enrollment outlives a movement request. A displaced package gets one
    // handoff, not an endless cycle of attempts to take control back.
    struct MovementHandoff
    {
        std::uint32_t displacedBy{};
        bool owned{}, yielded{};

        bool Permit(bool requested, bool ownsPackage, std::uint32_t currentPackage)
        {
            if (!requested) {
                Reset();
                return false;
            }
            if (yielded) {
                if (!currentPackage || currentPackage == displacedBy) {
                    return false;
                }
                Reset();
            }
            if (owned && !ownsPackage && currentPackage) {
                displacedBy = currentPackage;
                yielded = true;
                owned = false;
                return false;
            }
            if (currentPackage) {
                owned = ownsPackage;
            }
            return true;
        }
        void Reset()
        {
            displacedBy = 0;
            owned = yielded = false;
        }
    };
} // namespace Wayfarer

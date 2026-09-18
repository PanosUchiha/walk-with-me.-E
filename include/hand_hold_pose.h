#pragma once
#include <array>
#include <cstdint>

namespace Wayfarer::HandHolding
{

    template <class Entry, class CopyOwned>
    void SynchronizePoseCache(Entry* entries, std::uint32_t count, CopyOwned copyOwned)
    {
        if (!entries || count > 4096) {
            return;
        }
        std::array<bool, 4096> changed{};
        for (std::uint32_t i = 0; i < count; ++i) {
            auto& bone = entries[i];
            if (copyOwned(bone)) {
                changed[i] = true;
            }
            else if (bone.parentIndex >= 0 && static_cast<std::uint32_t>(bone.parentIndex) < i &&
                     changed[bone.parentIndex]) {
                bone.world = entries[bone.parentIndex].world * bone.local;
                changed[i] = true;
            }
        }
    }
} // namespace Wayfarer::HandHolding

#pragma once

#include <array>
#include <cstdint>

namespace Wayfarer
{
    struct AliasSyncSnapshot
    {
        std::uint32_t follower{}, marker{}, seat{}, gather{}, social{};
        std::int32_t handoff{};

        bool operator==(const AliasSyncSnapshot&) const = default;
    };

    using AliasSyncSnapshotSet = std::array<AliasSyncSnapshot, 10>;

    // The controller owns synchronization. A slow VM keeps one request outstanding;
    // completion acknowledges its captured state, so later changes stay dirty.
    class AliasSyncQueue
    {
      public:
        static constexpr std::uint16_t AllSlots = 0x03FF;

        [[nodiscard]] std::uint16_t DirtySlots(const AliasSyncSnapshotSet& latest,
                                               std::uint16_t workMask = 0) const
        {
            auto dirty = static_cast<std::uint16_t>(workMask & AllSlots);
            for (std::size_t slot = 0; slot < latest.size(); ++slot) {
                const auto bit = static_cast<std::uint16_t>(1u << slot);
                if (!(validSlots & bit) || latest[slot] != acknowledged[slot]) {
                    dirty |= bit;
                }
            }
            return dirty;
        }

        [[nodiscard]] std::uint64_t Begin(const AliasSyncSnapshotSet& latest, std::uint16_t slots)
        {
            slots &= AllSlots;
            if (Pending() || !slots) {
                return 0;
            }
            captured = latest;
            pendingSlots = slots;
            pendingTicket = NextTicket();
            return pendingTicket;
        }

        // True means this is the outstanding request's completion. An interrupted
        // pass may have changed some aliases, so invalidate all selected slots.
        bool Complete(std::uint64_t ticket, bool success)
        {
            if (!ticket || ticket != pendingTicket) {
                return false;
            }
            if (success) {
                for (std::size_t slot = 0; slot < captured.size(); ++slot) {
                    const auto bit = static_cast<std::uint16_t>(1u << slot);
                    if (pendingSlots & bit) {
                        acknowledged[slot] = captured[slot];
                    }
                }
                validSlots |= pendingSlots;
            }
            else {
                validSlots &= static_cast<std::uint16_t>(~pendingSlots);
            }
            pendingSlots = 0;
            pendingTicket = 0;
            return true;
        }

        void Reset()
        {
            validSlots = 0;
            pendingSlots = 0;
            pendingTicket = 0;
            NextTicket(); // Save-load callbacks cannot complete a new request.
        }

        [[nodiscard]] bool Pending() const { return pendingTicket != 0; }

      private:
        std::uint64_t NextTicket()
        {
            if (++serial == 0) {
                ++serial;
            }
            return serial;
        }

        AliasSyncSnapshotSet acknowledged{};
        AliasSyncSnapshotSet captured{};
        std::uint16_t validSlots{}, pendingSlots{};
        std::uint64_t serial{}, pendingTicket{};
    };
} // namespace Wayfarer

#pragma once
#include "party_recall.h"
#include <limits>

namespace Wayfarer
{
    // A one-shot permission survives menu dismissal, never save/load or a second click.
    struct CompanionPushRequest
    {
        PartyRecallRequest queue;
        std::uint32_t actor{};
        std::int32_t ticket{};
        bool dispatched{};
        std::uint64_t cooldownUntil{};
        bool Busy(std::uint64_t now) const { return queue.pending || dispatched || now < cooldownUntil; }
        bool Request(std::uint32_t id, std::uint64_t save, std::uint64_t now)
        {
            if (!id || Busy(now)) {
                return false;
            }
            ticket = ticket == std::numeric_limits<std::int32_t>::max() ? 1 : ticket + 1;
            actor = id;
            return queue.Request(save, now);
        }
        RecallDispatch Poll(std::uint64_t save, std::uint64_t now, bool menuOpen, bool safe)
        {
            if (dispatched &&
                (save != queue.generation || now < queue.requestedAt || now - queue.requestedAt > 5000)) {
                dispatched = false;
                return RecallDispatch::Cancel;
            }
            const auto result = queue.Poll(save, now, menuOpen, safe);
            if (result == RecallDispatch::Run) {
                dispatched = true;
            }
            return result;
        }
        bool Consume(std::uint32_t id, std::int32_t token, std::uint64_t save, std::uint64_t now, bool safe)
        {
            if (!dispatched || id != actor || token != ticket) {
                return false;
            }
            dispatched = false;
            if (!safe || save != queue.generation || now < queue.requestedAt ||
                now - queue.requestedAt > 5000) {
                return false;
            }
            cooldownUntil = now + 3000;
            return true;
        }
        void Reset()
        {
            queue = {};
            actor = 0;
            dispatched = false;
            cooldownUntil = 0;
        }
    };
} // namespace Wayfarer

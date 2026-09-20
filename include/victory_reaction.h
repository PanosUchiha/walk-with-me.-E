#pragma once
#include <cstdint>
#include <mutex>
#include <unordered_set>

namespace Wayfarer::Victory
{
    enum class Equipment
    {
        Sheathed,
        EmptyHands,
        Sword,
        Greatsword,
        OtherDrawn,
        Unsupported,
        Transition
    };
    constexpr float reactionSeconds = 4.9F;
    constexpr float shoulderSeconds = 6.5F;
    struct IdleChoice
    {
        std::uint32_t form{};
        const char* name{"waiting for compatible equipment"};
        bool armed{};
        bool shoulder{};
        float seconds{reactionSeconds};
    };
    constexpr std::uint32_t PartyVariation(std::uint32_t index, std::uint32_t sequence)
    {
        // Reserve a shoulder slot first, then deal different reactions across
        // the party. Rotate actor ordering per encounter at the call site.
        return index + 3 + sequence * 4;
    }
    constexpr bool WaitForIdleOwnership(float elapsed)
    {
        return elapsed < .5F;
    }
    constexpr bool RetryEarlyIdleLoss(float elapsed, unsigned attempts)
    {
        return elapsed < 1.0F && attempts < 3;
    }
    constexpr bool RequestsShoulder(std::uint32_t variation, bool available)
    {
        return available && variation % 4 == 3;
    }
    constexpr IdleChoice ChooseIdle(Equipment equipment, std::uint32_t variation,
                                    bool shoulderAvailable = false)
    {
        if (equipment == Equipment::Sheathed && RequestsShoulder(variation, shoulderAvailable)) {
            return {0xD8731, "Shoulder recovery", false, true, shoulderSeconds};
        }
        switch (equipment) {
        case Equipment::Sheathed:
        case Equipment::EmptyHands: {
            constexpr IdleChoice applause[]{{0xD8731, "IdleApplaud3 (tavern applause)", false},
                                            {0xD8730, "IdleApplaud2", false},
                                            {0xD8733, "IdleApplaud5", false}};
            switch (variation % 4) {
            case 1:
                return {0x977EC, "IdleWipeBrow (catch breath)", false, false, 2.6F};
            case 2:
                return {0x75C3D, "IdleExamine (check hands)", false, false, 6.3F};
            default:
                return applause[(variation / 4) % 3];
            }
        }
        case Equipment::Sword:
            return {0xF7C8C, "IdleCivilWarCheer (sword)", true};
        case Equipment::Greatsword:
            return {0xF7C8C, "IdleCivilWarCheer (greatsword)", true};
        case Equipment::OtherDrawn:
            return {0xB8C71, "CombatIdleRegular (armed pause)", true, false, 1.6F};
        default:
            return {};
        }
    }
    // A completed sync after queuing releases the old travel aliases and runs
    // their cleanup. Later routine scans must not close that handoff again.
    constexpr bool HandoffComplete(std::uint64_t queuedAt, std::uint64_t completed)
    {
        return completed > queuedAt;
    }
    constexpr float startWindowSeconds = 15.0F;
    constexpr float retrySeconds = .5F;

    // A brief player movement must not discard reactions still waiting for
    // sheathing, speech or alias cleanup. Started idles still stop immediately.
    constexpr bool CancelReaction(bool started, bool partyFighting, bool playerReady)
    {
        return partyFighting || (started && !playerReady);
    }

    // OAR can evaluate on an animation thread. This transient selection has its
    // own short lock, never the formation lock, and is never written to saves.
    class ShoulderSelections
    {
      public:
        void Set(std::uint32_t actor, bool selected)
        {
            std::lock_guard lock(mutex);
            if (selected && actor) {
                actors.insert(actor);
            }
            else {
                actors.erase(actor);
            }
            observed.erase(actor);
        }
        bool Contains(std::uint32_t actor) const
        {
            std::lock_guard lock(mutex);
            return actor && actors.contains(actor);
        }
        void Observe(std::uint32_t actor)
        {
            std::lock_guard lock(mutex);
            if (actors.contains(actor)) {
                observed.insert(actor);
            }
        }
        bool WasObserved(std::uint32_t actor) const
        {
            std::lock_guard lock(mutex);
            return observed.contains(actor);
        }
        void Clear()
        {
            std::lock_guard lock(mutex);
            actors.clear();
            observed.clear();
        }

      private:
        mutable std::mutex mutex;
        std::unordered_set<std::uint32_t> actors;
        std::unordered_set<std::uint32_t> observed;
    };
} // namespace Wayfarer::Victory

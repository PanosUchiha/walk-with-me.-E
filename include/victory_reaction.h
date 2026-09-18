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
    // The authored clip uses a scoped OAR replacement of the applause carrier.
    // Drawn empty hands are distinct from a completed sheathe.
    constexpr bool RequestsShoulder(std::uint32_t variation, bool available)
    {
        return available && variation % 4 == 3;
    }
    constexpr IdleChoice ChooseIdle(Equipment equipment, std::uint32_t variation,
                                    bool shoulderAvailable = false)
    {
        // Keep the selection while weapons are out. Playing a sword cheer
        // first would consume the reaction before this companion can sheathe.
        if (RequestsShoulder(variation, shoulderAvailable)) {
            if (equipment == Equipment::Sheathed) {
                return {0xD8731, "Shoulder recovery", false, true, shoulderSeconds};
            }
            return {};
        }
        switch (equipment) {
        case Equipment::Sheathed:
        case Equipment::EmptyHands: {
            constexpr IdleChoice applause[]{{0xD8731, "IdleApplaud3 (tavern applause)", false},
                                            {0xD8730, "IdleApplaud2", false},
                                            {0xD8733, "IdleApplaud5", false}};
            if (equipment == Equipment::Sheathed && shoulderAvailable) {
                const auto index = variation % 4;
                return applause[index];
            }
            return applause[variation % 3];
        }
        case Equipment::Sword:
            return {0xF7C8C, "IdleCivilWarCheer (sword)", true};
        case Equipment::Greatsword:
            return {0xF7C8C, "IdleCivilWarCheer (greatsword)", true};
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
        }
        bool Contains(std::uint32_t actor) const
        {
            std::lock_guard lock(mutex);
            return actor && actors.contains(actor);
        }
        void Clear()
        {
            std::lock_guard lock(mutex);
            actors.clear();
        }

      private:
        mutable std::mutex mutex;
        std::unordered_set<std::uint32_t> actors;
    };
} // namespace Wayfarer::Victory

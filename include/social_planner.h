#pragma once
#include "formation_math.h"
#include <array>
#include <cstdint>
#include <functional>
#include <vector>
namespace Wayfarer
{
    struct SocialCandidate
    {
        std::uint32_t id;
        Vec2 position;
        bool settled{true};
        std::uint32_t space{};
        int weight{2};
    };

    // How a place colours a conversation: its length, how animated it is, and
    // whether the pair talk over a drink instead of trading gestures.
    struct SocialStyle
    {
        float minDuration{120}, spread{61};
        int gesturePercent{67};
        bool shareDrink{};
    };

    // One pair of resting companions talks at a time. members[0] starts it; when
    // the two are apart, members[0] walks over (gathering) while members[1] waits.
    struct SocialPlanner
    {
        // nearDistance: close enough to talk where they stand. talkDistance: a
        // walking initiator has arrived. meetDistance: farthest partner worth a walk.
        static constexpr float nearDistance = 200, talkDistance = 160, meetDistance = 1800,
                               leaveDistance = 500, gatherTimeout = 45;
        std::array<std::uint32_t, 2> members{};
        std::array<float, 2> movingTime{};
        std::array<bool, 2> pending{};
        bool skippedQuiet{}, gathering{};
        std::uint32_t opportunities{}, sequence{};
        int count{}, turn{};
        // The first pair waits until companions have reached their own activities.
        float elapsed{}, cooldown{20}, duration{150}, turnTime{}, turnLength{5};
        SocialStyle style; // Set by the caller for the current place before Update.
        void Reset() { *this = {}; }
        void ClearGestures() { pending = {}; }
        void Cancel()
        {
            count = 0;
            members = {};
            movingTime = {};
            pending = {};
            gathering = false;
            cooldown = 3;
        }
        int Index(std::uint32_t actor) const;
        bool Update(float dt, const std::vector<SocialCandidate>& candidates,
                    const std::function<bool(std::uint32_t, std::uint32_t)>& canPair = {});
        bool TakeGesture(std::uint32_t actor)
        {
            const int i = Index(actor);
            if (i < 0 || gathering || !pending[i]) {
                return false;
            }
            pending[i] = false;
            return true;
        }
        // Talking partner only; empty while the initiator is still walking over.
        std::uint32_t Listener(std::uint32_t actor) const;
        std::uint32_t Partner(std::uint32_t actor) const;
        std::uint32_t Speaker() const { return count == 2 && !gathering ? members[turn % 2] : 0; }
        std::uint32_t Walker() const { return count == 2 && gathering ? members[0] : 0; }
        // Held in place: both talkers, or the partner waiting for the walker.
        bool Holds(std::uint32_t actor) const
        {
            const int i = Index(actor);
            return i >= 0 && (!gathering || i == 1);
        }
        bool Involves(std::uint32_t actor) const { return Index(actor) >= 0; }

    private:
        void BeginTalk();
    };
} // namespace Wayfarer

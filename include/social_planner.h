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

    struct SocialPlanner
    {
        static constexpr float joinDistance = 420, leaveDistance = 500;
        std::array<std::uint32_t, 2> members{};
        std::array<float, 2> movingTime{};
        std::array<bool, 2> pending{};
        bool skippedQuiet{};
        std::uint32_t opportunities{}, sequence{};
        int count{}, turn{}, previousTurn{-1};
        float elapsed{}, cooldown{3}, duration{24};
        void Reset() { *this = {}; }
        void ClearGestures() { pending = {}; }
        void Cancel()
        {
            count = 0;
            members = {};
            movingTime = {};
            pending = {};
            cooldown = 3;
        }
        int Index(std::uint32_t actor) const;
        bool Update(float dt, const std::vector<SocialCandidate>& candidates,
                    const std::function<bool(std::uint32_t, std::uint32_t)>& canPair = {});
        bool TakeGesture(std::uint32_t actor)
        {
            const int i = Index(actor);
            if (i < 0 || !pending[i]) {
                return false;
            }
            pending[i] = false;
            return true;
        }
        std::uint32_t Listener(std::uint32_t actor) const;
        std::uint32_t Speaker() const { return count == 2 ? members[turn % 2] : 0; }
    };
} // namespace Wayfarer

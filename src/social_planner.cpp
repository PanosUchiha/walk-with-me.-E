#include "social_planner.h"
#include "rest_pose.h"
#include <algorithm>
namespace Wayfarer
{
    int SocialPlanner::Index(std::uint32_t actor) const
    {
        for (int i = 0; i < count; ++i) {
            if (members[i] == actor) {
                return i;
            }
        }
        return -1;
    }
    bool SocialPlanner::Update(float dt, const std::vector<SocialCandidate>& candidates,
                               const std::function<bool(std::uint32_t, std::uint32_t)>& canPair)
    {
        if (count) {
            auto find = [&](std::uint32_t id) {
                return std::find_if(candidates.begin(), candidates.end(),
                                    [&](auto& c) { return c.id == id; });
            };
            auto a = find(members[0]), b = find(members[1]);
            if (a == candidates.end() || b == candidates.end() || a->space != b->space ||
                Length({a->position.x - b->position.x, a->position.y - b->position.y}) > leaveDistance) {
                Cancel();
                return false;
            }
            movingTime[0] = a->settled ? 0 : movingTime[0] + dt;
            movingTime[1] = b->settled ? 0 : movingTime[1] + dt;

            if (movingTime[0] > 1.5F || movingTime[1] > 1.5F) {
                Cancel();
                return false;
            }
            elapsed += dt;
            if (elapsed >= duration) {
                Cancel();
                cooldown = 20 + static_cast<float>(sequence % 4) * 5;
                return false;
            }
            turn = static_cast<int>(elapsed / 5);
            if (turn != previousTurn) {
                previousTurn = turn;
                pending[turn % 2] = true;
                return true;
            }
            return false;
        }
        cooldown -= dt;
        if (cooldown > 0 || candidates.size() < 2) {
            return false;
        }
        int tickets = 0;
        for (const auto& c : candidates) {
            tickets += std::max(1, c.weight);
        }
        auto ticket = RestPose::Mix(opportunities + sequence * 7919) % static_cast<unsigned>(tickets);
        std::size_t start = 0;
        for (; start + 1 < candidates.size(); ++start) {
            if (ticket < static_cast<unsigned>(std::max(1, candidates[start].weight))) {
                break;
            }
            ticket -= std::max(1, candidates[start].weight);
        }
        for (std::size_t step = 0; step < candidates.size(); ++step) {
            const auto& seed = candidates[(start + step) % candidates.size()];
            if (!seed.settled) {
                continue;
            }
            const SocialCandidate* partner = nullptr;
            float nearest = joinDistance;
            for (auto& other : candidates) {
                if (other.id == seed.id || !other.settled || other.space != seed.space) {
                    continue;
                }
                const float distance =
                    Length({seed.position.x - other.position.x, seed.position.y - other.position.y});
                if (distance <= nearest && (!canPair || canPair(seed.id, other.id))) {
                    nearest = distance;
                    partner = &other;
                }
            }
            if (!partner) {
                continue;
            }
            ++opportunities;

            if (sequence && !skippedQuiet && RestPose::Mix(opportunities * 7919 + seed.id) % 4 == 0) {
                skippedQuiet = true;
                cooldown = 8;
                return false;
            }
            skippedQuiet = false;
            members = {seed.id, partner->id};
            count = 2;
            movingTime = {};
            elapsed = 0;
            turn = previousTurn = 0;
            pending = {true, false};
            duration = 18 + static_cast<float>((seed.id + ++sequence * 7) % 13);
            return true;
        }
        cooldown = 3;
        return false;
    }
    std::uint32_t SocialPlanner::Listener(std::uint32_t actor) const
    {
        const int index = Index(actor);
        return index >= 0 ? members[1 - index] : 0;
    }
} // namespace Wayfarer

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
    void SocialPlanner::BeginTalk()
    {
        gathering = false;
        elapsed = turnTime = 0;
        movingTime = {};
        turn = 0;
        turnLength = 4.0F + static_cast<float>(RestPose::Mix(sequence * 31) % 5);
        pending = {true, style.shareDrink};
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
            if (a == candidates.end() || b == candidates.end() || a->space != b->space) {
                Cancel();
                return false;
            }
            const float apart = Length({a->position.x - b->position.x, a->position.y - b->position.y});
            if (gathering) {
                // The walker is expected to move; only the waiting partner must stay put.
                movingTime[1] = b->settled ? 0 : movingTime[1] + dt;
                elapsed += dt;
                if (movingTime[1] > 3 || elapsed >= gatherTimeout || apart > meetDistance + 200) {
                    Cancel();
                    cooldown = 10;
                    return false;
                }
                if (apart > talkDistance) {
                    return false;
                }
                BeginTalk();
                return true;
            }
            if (apart > leaveDistance) {
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
                cooldown = 45 + static_cast<float>(RestPose::Mix(sequence * 7919 + 17) % 46);
                return false;
            }
            turnTime += dt;
            if (turnTime < turnLength) {
                return false;
            }
            turnTime = 0;
            ++turn;
            turnLength = 4.0F + static_cast<float>(RestPose::Mix(sequence * 31 + turn) % 5);
            // Minutes of conversation: not every line needs a hand gesture.
            if (static_cast<int>(RestPose::Mix(sequence * 7919 + turn) % 100) >= style.gesturePercent) {
                return false;
            }
            pending[turn % 2] = true;
            return true;
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
            // Prefer someone already within earshot and in view; otherwise walk
            // over to the nearest companion instead of talking across the area.
            const SocialCandidate* partner = nullptr;
            bool talkNow = false;
            for (int pass = 0; pass < 2 && !partner; ++pass) {
                float nearest = pass ? meetDistance : nearDistance;
                for (auto& other : candidates) {
                    if (other.id == seed.id || !other.settled || other.space != seed.space) {
                        continue;
                    }
                    const float distance =
                        Length({seed.position.x - other.position.x, seed.position.y - other.position.y});
                    if (distance <= nearest && (pass || !canPair || canPair(seed.id, other.id))) {
                        nearest = distance;
                        partner = &other;
                    }
                }
                talkNow = partner && !pass;
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
            pending = {};
            elapsed = turnTime = 0;
            turn = 0;
            // Two to three minutes once they are face to face, by default.
            duration = style.minDuration + static_cast<float>((seed.id + ++sequence * 7) %
                                                              static_cast<std::uint32_t>(std::max(1.0F, style.spread)));
            gathering = !talkNow;
            if (talkNow) {
                BeginTalk();
            }
            return true;
        }
        cooldown = 3;
        return false;
    }
    std::uint32_t SocialPlanner::Listener(std::uint32_t actor) const
    {
        return gathering ? 0 : Partner(actor);
    }
    std::uint32_t SocialPlanner::Partner(std::uint32_t actor) const
    {
        const int index = Index(actor);
        return index >= 0 ? members[1 - index] : 0;
    }
} // namespace Wayfarer

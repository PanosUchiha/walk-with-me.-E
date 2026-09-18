#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>

namespace Wayfarer::Victory
{
    enum class Result
    {
        None,
        Victory,
        Finished
    };

    struct Encounter
    {
        std::unordered_set<std::uint32_t> enemies, defeated, participants;
        bool active{};
        float quiet{};
        static constexpr float settleSeconds = 1.25F;
        static constexpr std::uint32_t minimumEnemies = 3;

        void ObserveEnemy(std::uint32_t id)
        {
            if (!id) {
                return;
            }
            enemies.insert(id);
            active = true;
            quiet = 0;
        }

        void ObserveParticipant(std::uint32_t id)
        {
            if (id) {
                participants.insert(id);
            }
        }

        void MarkDefeated(std::uint32_t id)
        {
            if (enemies.contains(id)) {
                defeated.insert(id);
            }
        }

        Result Tick(float dt, bool partyFighting)
        {
            if (!std::isfinite(dt) || dt <= 0) {
                return Result::None;
            }
            if (partyFighting) {
                active = true;
                quiet = 0;
                return Result::None;
            }
            if (!active) {
                return Result::None;
            }
            quiet += dt;
            if (quiet < settleSeconds) {
                return Result::None;
            }

            active = false;

            const bool allDefeated = std::all_of(enemies.begin(), enemies.end(),
                                                 [this](std::uint32_t id) { return defeated.contains(id); });
            return enemies.size() >= minimumEnemies && allDefeated ? Result::Victory : Result::Finished;
        }

        void Reset()
        {
            enemies.clear();
            defeated.clear();
            participants.clear();
            active = false;
            quiet = 0;
        }
    };
} // namespace Wayfarer::Victory

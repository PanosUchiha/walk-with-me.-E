#pragma once
#include "formation_math.h"
#include "rest_pose.h"
#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>
namespace Wayfarer
{
    enum class Personality
    {
        Balanced,
        Sociable,
        Curious,
        Reserved,
        Watchful
    };
    inline std::uint32_t IdentityHash(std::string_view key)
    {
        std::uint32_t h = 2166136261u;
        for (unsigned char c : key) {
            h = (h ^ c) * 16777619u;
        }
        return h;
    }
    inline Personality ResolvePersonality(std::string_view key, int overrideValue, bool enabled)
    {
        if (!enabled) {
            return Personality::Balanced;
        }
        return overrideValue >= 0 && overrideValue <= 4 ? static_cast<Personality>(overrideValue)
                                                        : static_cast<Personality>(1 + IdentityHash(key) % 4);
    }
    inline const char* PersonalityName(Personality p)
    {
        constexpr const char* names[]{"Balanced", "Sociable", "Curious", "Reserved", "Watchful"};
        return names[std::clamp(static_cast<int>(p), 0, 4)];
    }
    inline int SocialWeight(Personality p)
    {
        return p == Personality::Sociable ? 4 : p == Personality::Reserved ? 1 : 2;
    }
    inline float RecoveryPreference(Personality p)
    {
        return p == Personality::Curious    ? .75F
               : p == Personality::Reserved ? 1.4F
               : p == Personality::Watchful ? 1.2F
                                            : 1.0F;
    }
    inline int WatchWeight(Personality p)
    {
        return p == Personality::Watchful ? 4 : p == Personality::Sociable ? 1 : 2;
    }
    struct WatchCandidate
    {
        std::uint32_t id;
        int weight{2};
    };
    inline bool LookoutPermitted(bool enabled, bool resting, bool expanded, bool outdoorsOrDungeon,
                                 int companions, bool combat)
    {
        return enabled && resting && expanded && outdoorsOrDungeon && companions >= 3 && !combat;
    }
    struct LookoutState
    {
        std::uint32_t actor{}, last{}, sequence{};
        float elapsed{}, cooldown{}, duration{};
        void Reset() { *this = {}; }
        void End()
        {
            last = actor;
            actor = 0;
            elapsed = 0;
            cooldown = 90 + RestPose::Mix(++sequence) % 61;
        }
        void Update(float dt, bool permitted, float restAge, float beginAfter,
                    const std::vector<WatchCandidate>& candidates)
        {
            if (!permitted) {
                if (actor) {
                    End();
                }
                return;
            }
            if (actor) {
                elapsed += dt;
                if (elapsed >= duration || std::none_of(candidates.begin(), candidates.end(),
                                                        [&](auto c) { return c.id == actor; })) {
                    End();
                }
                return;
            }
            cooldown = std::max(0.0F, cooldown - dt);
            if (restAge < beginAfter || cooldown > 0 || candidates.empty()) {
                return;
            }
            const bool alternate =
                std::any_of(candidates.begin(), candidates.end(), [&](auto c) { return c.id != last; });
            int total = 0;
            for (auto c : candidates) {
                if (!alternate || c.id != last) {
                    total += std::max(1, c.weight);
                }
            }
            auto ticket =
                RestPose::Mix(sequence * 7919 + candidates.front().id) % static_cast<unsigned>(total);
            for (auto c : candidates) {
                if (!alternate || c.id != last) {
                    if (ticket < static_cast<unsigned>(std::max(1, c.weight))) {
                        actor = c.id;
                        break;
                    }
                    ticket -= std::max(1, c.weight);
                }
            }
            elapsed = 0;
            duration = 30 + RestPose::Mix(actor + sequence) % 16;
        }
    };
    inline Vec2 NearestOnConversation(Vec2 actor, Vec2 player, Vec2 speaker)
    {
        const Vec2 d{speaker.x - player.x, speaker.y - player.y};
        const float len = d.x * d.x + d.y * d.y;
        const float t =
            len > 1 ? std::clamp(((actor.x - player.x) * d.x + (actor.y - player.y) * d.y) / len, 0.0F, 1.0F)
                    : 0;
        return {player.x + d.x * t, player.y + d.y * t};
    }
    inline bool IntrudesConversation(Vec2 actor, Vec2 player, Vec2 speaker, float radius)
    {
        auto p = NearestOnConversation(actor, player, speaker);
        return Length({actor.x - p.x, actor.y - p.y}) < radius;
    }
    inline Vec2 ConversationExit(Vec2 actor, Vec2 player, Vec2 speaker, float radius, std::uint32_t id)
    {
        auto nearest = NearestOnConversation(actor, player, speaker);
        auto right = RightFromForward(Normalize({speaker.x - player.x, speaker.y - player.y}));
        const float side = (actor.x - nearest.x) * right.x + (actor.y - nearest.y) * right.y;
        const float sign = std::abs(side) > 1 ? (side < 0 ? -1.0F : 1.0F) : (id % 2 ? -1.0F : 1.0F);
        return {nearest.x + right.x * sign * (radius + 100), nearest.y + right.y * sign * (radius + 100)};
    }
} // namespace Wayfarer

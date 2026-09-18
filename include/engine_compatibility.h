#pragma once
#include "RE/M/MenuTopicManager.h"
#include "RE/Skyrim.h"

namespace Wayfarer::Engine
{
    inline std::int32_t FactionRank(RE::Actor& actor, RE::TESFaction* faction)
    {
        if (!faction) {
            return -1;
        }
#ifdef WAYFARER_MODERN_COMMONLIB
        return actor.GetFactionRank(faction, false);
#else
        using Function = std::int32_t (*)(RE::Actor*, RE::TESFaction*, bool);
        static REL::Relocation<Function> function{REL::RelocationID(36668, 37676)};
        return function(&actor, faction, false);
#endif
    }

    inline bool IsKeyPressed(const RE::BSWin32KeyboardDevice& keyboard, std::uint32_t key)
    {

#ifdef WAYFARER_MODERN_COMMONLIB
        const auto& state = keyboard.GetRuntimeData().curState;
#else
        // Legacy CommonLib swaps the current/previous buffer names.
        const auto& state = keyboard.prevState;
#endif
        return key < sizeof(state) && (state[key] & 0x80) != 0;
    }

    template <class Callback> void ForEachHighActor(RE::ProcessLists& lists, Callback callback)
    {
#ifdef WAYFARER_MODERN_COMMONLIB
        lists.ForEachHighActor([&](RE::Actor* actor) {
            return actor ? callback(*actor) : RE::BSContainer::ForEachResult::kContinue;
        });
#else
        lists.ForEachHighActor(callback);
#endif
    }

    template <class Callback>
    void ForEachReferenceInRange(RE::TESObjectCELL& cell, const RE::NiPoint3& center, float radius,
                                 Callback callback)
    {
#ifdef WAYFARER_MODERN_COMMONLIB
        cell.ForEachReferenceInRange(center, radius, [&](RE::TESObjectREFR* reference) {
            return reference ? callback(*reference) : RE::BSContainer::ForEachResult::kContinue;
        });
#else
        cell.ForEachReferenceInRange(center, radius, callback);
#endif
    }

    inline void SetHeading(RE::Actor& actor, float angle)
    {
#ifdef WAYFARER_MODERN_COMMONLIB
        actor.SetHeading(angle);
#else
        actor.SetRotationZ(angle);
#endif
    }

    inline bool IsDialogueTransition(const RE::MenuTopicManager& topics)
    {
#ifdef WAYFARER_MODERN_COMMONLIB
        return topics.isGreetingPlayer || topics.forceGoodbye;
#else
        return topics.isGreetingPlayer || topics.isSayingGoodbye;
#endif
    }

    inline void Notify(const char* text)
    {
#ifdef WAYFARER_MODERN_COMMONLIB
        RE::SendHUDMessage::ShowHUDMessage(text);
#else
        RE::DebugNotification(text);
#endif
    }
} // namespace Wayfarer::Engine

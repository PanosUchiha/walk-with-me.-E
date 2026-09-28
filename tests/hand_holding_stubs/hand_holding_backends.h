#pragma once
// Engine-free declarations for exercising the production dispatcher with fake backends.
#include "hand_hold_state.h"
#include <mutex>

namespace RE
{
    using FormID = std::uint32_t;
    struct Actor
    {
    };
    struct PlayerCharacter : Actor
    {
    };
} // namespace RE
namespace logger
{
    template <class... Args> void info(const char*, Args...) {}
} // namespace logger
namespace Wayfarer
{
    struct TravelNavigation
    {
    };
} // namespace Wayfarer
namespace Wayfarer::HandHolding
{
    struct Approach
    {
        RE::FormID actor{};
    };
    std::string ReferenceKey(RE::Actor&);
    void Install();
    void Reset();
    void OnGameLoaded();
    bool BeginFrame(float, bool, bool, const Tuning&);
    void UpdatePartner(RE::PlayerCharacter&, RE::Actor*, float, const TravelNavigation&);
    Approach GetApproach();
    bool IsAttached(RE::FormID);
    void ReleaseActor(RE::FormID);
    bool OwnsMovement(RE::FormID);
    std::int32_t HandoffTicket(RE::FormID);
    bool HandoffCleanupFinished(RE::FormID, std::int32_t);
    void ConfirmHandoff(RE::FormID, std::int32_t, bool, bool);
    std::int32_t TetherStepStatus(std::int32_t, std::int32_t, RE::Actor*, RE::Actor*);
    float SpeedMultiplier(RE::Actor*);
    namespace Classic
    {
        void Install();
        void Reset();
        bool BeginFrame(float, bool, bool, const Tuning&);
        void UpdatePartner(RE::PlayerCharacter&, RE::Actor*, float, const TravelNavigation&);
        Approach GetApproach();
        bool IsAttached(RE::FormID);
        void ReleaseActor(RE::FormID);
        bool OwnsMovement(RE::FormID);
        std::int32_t HandoffTicket(RE::FormID);
        bool HandoffCleanupFinished(RE::FormID, std::int32_t);
        void ConfirmHandoff(RE::FormID, std::int32_t, bool, bool);
    } // namespace Classic
    namespace Tether
    {
        std::string ReferenceKey(RE::Actor&);
        void Install();
        void Reset();
        void OnGameLoaded();
        bool CleanupPending();
        bool BeginFrame(float, bool, bool, const Tuning&);
        void UpdatePartner(RE::PlayerCharacter&, RE::Actor*, float, const TravelNavigation&);
        Approach GetApproach();
        bool IsAttached(RE::FormID);
        void ReleaseActor(RE::FormID);
        bool OwnsMovement(RE::FormID);
        std::int32_t HandoffTicket(RE::FormID);
        bool HandoffCleanupFinished(RE::FormID, std::int32_t);
        void ConfirmHandoff(RE::FormID, std::int32_t, bool, bool);
        std::int32_t TetherStepStatus(std::int32_t, std::int32_t, RE::Actor*, RE::Actor*);
        float SpeedMultiplier(RE::Actor*);
    } // namespace Tether
} // namespace Wayfarer::HandHolding

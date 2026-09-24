#pragma once
#include "hand_holding.h"

namespace Wayfarer::HandHolding::Classic
{
    std::string ReferenceKey(RE::Actor& actor);
    void Install();
    void Reset();
    void ReleaseActor(RE::FormID actor);
    bool BeginFrame(float dt, bool permitted, bool paused, const Tuning& tuning);
    void UpdatePartner(RE::PlayerCharacter& player, RE::Actor* partner, float dt,
                       const TravelNavigation& navigation);
    bool IsAttached(RE::FormID actor);
    Approach GetApproach();
} // namespace Wayfarer::HandHolding::Classic

namespace Wayfarer::HandHolding::Tether
{
    std::string ReferenceKey(RE::Actor& actor);
    void Install();
    void Reset();
    void OnGameLoaded();
    bool CleanupPending();
    bool OwnsMovement(RE::FormID actor);
    std::int32_t HandoffTicket(RE::FormID actor);
    bool HandoffCleanupFinished(RE::FormID actor, std::int32_t ticket);
    void ConfirmHandoff(RE::FormID actor, std::int32_t ticket, bool aliasEmpty, bool packageReleased);
    std::int32_t TetherStepStatus(std::int32_t ticket, std::int32_t operation, RE::Actor* player,
                                  RE::Actor* follower);
    float SpeedMultiplier(RE::Actor* actor);
    void ReleaseActor(RE::FormID actor);
    bool BeginFrame(float dt, bool permitted, bool paused, const Tuning& tuning);
    void UpdatePartner(RE::PlayerCharacter& player, RE::Actor* partner, float dt,
                       const TravelNavigation& navigation);
    bool IsAttached(RE::FormID actor);
    Approach GetApproach();
} // namespace Wayfarer::HandHolding::Tether

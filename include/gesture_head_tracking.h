#pragma once
namespace Wayfarer::CommandGesture::HeadTracking
{
    void Start(RE::PlayerCharacter& player, float duration, bool authored = false);
    void Update(RE::PlayerCharacter& player, float dt, bool gestureActive);
    void Reset(RE::PlayerCharacter* player);
    std::uint32_t SavedOwnership();
    void RestoreOwnership(std::uint32_t saved);
} // namespace Wayfarer::CommandGesture::HeadTracking

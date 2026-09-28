#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace Wayfarer::HandHolding
{
    // Skyrim yaw wraps at 2*pi. Follow the shortest turn without snapping a live
    // constraint through a large angle on a slow frame. Pitch/roll stay unchanged.
    inline std::optional<float> TetherHeldHeading(float current, float target, float dt)
    {
        if (!std::isfinite(current) || !std::isfinite(target) || !std::isfinite(dt) || dt <= 0 || dt > 1) {
            return {};
        }
        constexpr float circle = 6.283185307F;
        const float error = std::remainder(target - current, circle);
        const float step = 6.0F * std::min(dt, .05F);
        float yaw = std::fmod(current + std::clamp(error, -step, step), circle);
        return yaw < 0 ? yaw + circle : yaw;
    }

    enum class TetherMovementAction
    {
        None,
        RefreshPackage,
        RefreshStall,
        Release
    };

    struct TetherMovement
    {
        std::optional<std::uint32_t> package;
        float grace{}, stalled{};
        bool retriedStall{};

        void Requested(std::uint32_t id)
        {
            // Capture at dispatch: a package change while the VM call is pending
            // still needs another offset once that call has finished.
            package = id;
            grace = .5F;
            stalled = 0;
        }

        void Pause() { stalled = 0; }

        // Called only after offset completion, with no outstanding operation.
        // Speeds come from frame-to-frame positions, not requested gait/velocity.
        TetherMovementAction Observe(std::uint32_t id, float targetError, float playerSpeed,
                                     float followerSpeed, float dt)
        {
            if (!std::isfinite(dt) || dt <= 0 || dt > 1 || !std::isfinite(targetError) ||
                !std::isfinite(playerSpeed) || !std::isfinite(followerSpeed)) {
                stalled = 0;
                return TetherMovementAction::None;
            }
            grace = std::max(0.0F, grace - dt);
            if (grace > 0) {
                return TetherMovementAction::None;
            }
            if (package && *package != id) {
                return TetherMovementAction::RefreshPackage;
            }
            if (playerSpeed <= 15 || followerSpeed >= 5 || targetError <= 20) {
                stalled = 0;
                retriedStall = false;
                return TetherMovementAction::None;
            }
            stalled += dt;
            if (stalled < (retriedStall ? 1.5F : .6F)) {
                return TetherMovementAction::None;
            }
            if (retriedStall) {
                return TetherMovementAction::Release;
            }
            retriedStall = true;
            stalled = 0;
            return TetherMovementAction::RefreshStall;
        }
    };
} // namespace Wayfarer::HandHolding

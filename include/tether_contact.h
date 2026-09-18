#pragma once
#include "hand_hold_ik.h"
#include <array>

namespace Wayfarer::HandHolding
{
    // Finger bases are measured on each actor, so hand size, side and skeleton
    // axes do not share a guessed (0,0,Z) attachment point.
    inline std::optional<IK::Point> PalmCenter(IK::Point wrist, const std::array<IK::Point, 3>& bases)
    {
        if (!IK::Finite(wrist)) {
            return {};
        }
        IK::Point center{};
        for (auto p : bases) {
            const float length = IK::Length(p - wrist);
            if (!IK::Finite(p) || length < 1 || length > 30) {
                return {};
            }
            center = center + p;
        }
        return wrist + (center * (1.0F / 3) - wrist) * .55F;
    }
    inline std::optional<IK::Point> BodyLocalContact(IK::Point point, IK::Point origin,
                                                     const std::array<IK::Point, 3>& axes)
    {
        if (!IK::Finite(point) || !IK::Finite(origin)) {
            return {};
        }
        for (auto axis : axes) {
            if (!IK::Finite(axis) || std::abs(IK::Length(axis) - 1) > .02F) {
                return {};
            }
        }
        if (std::abs(IK::Dot(axes[0], axes[1])) > .02F || std::abs(IK::Dot(axes[0], axes[2])) > .02F ||
            std::abs(IK::Dot(axes[1], axes[2])) > .02F) {
            return {};
        }
        const auto delta = point - origin;
        const IK::Point local{IK::Dot(delta, axes[0]), IK::Dot(delta, axes[1]), IK::Dot(delta, axes[2])};
        return IK::Finite(local) && IK::Length(local) <= 30 ? std::optional{local} : std::nullopt;
    }
} // namespace Wayfarer::HandHolding

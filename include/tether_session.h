#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace Wayfarer::HandHolding
{
    // Serial ownership of physics setup and teardown. A cancellation waits for the
    // outstanding result before removing resources that operation may have created.
    enum class TetherOp
    {
        AddPlayer,
        AddFollower,
        Connect,
        Disconnect,
        RemoveFollower,
        RemovePlayer,
        Offset,
        ClearOffset
    };
    inline bool IsCleanup(TetherOp op)
    {
        return op == TetherOp::Disconnect || op == TetherOp::RemoveFollower || op == TetherOp::RemovePlayer ||
               op == TetherOp::ClearOffset;
    }
    // Connect beside the player while the WWM arrival target holds position.
    // Only offset movement needs the old package and its script cleanup gone.
    // Waiting for that handoff before physics left the pair idle for seconds.
    inline bool NeedsHandoff(TetherOp op)
    {
        return op == TetherOp::Offset;
    }
    inline bool NeedsHeading(TetherOp op)
    {
        return op == TetherOp::AddPlayer || op == TetherOp::AddFollower || op == TetherOp::Connect;
    }
    inline bool NeedsGrip(TetherOp op)
    {
        return op == TetherOp::Connect;
    }
    struct TetherSession
    {
        bool started{}, cancelled{}, player{}, follower{}, constraint{}, connected{}, offset{};
        bool refreshOffset{};
        std::optional<TetherOp> pending;
        void Start()
        {
            *this = {};
            started = true;
        }
        void Cancel()
        {
            cancelled = true;
            connected = false;
        }
        std::optional<TetherOp> Next() const
        {
            if (!started || pending) {
                return {};
            }
            if (cancelled) {
                if (constraint) {
                    return TetherOp::Disconnect;
                }
                if (follower) {
                    return TetherOp::RemoveFollower;
                }
                if (player) {
                    return TetherOp::RemovePlayer;
                }
                if (offset) {
                    return TetherOp::ClearOffset;
                }
                return {};
            }
            if (!player) {
                return TetherOp::AddPlayer;
            }
            if (!follower) {
                return TetherOp::AddFollower;
            }
            if (!constraint) {
                return TetherOp::Connect;
            }
            if (!offset || refreshOffset) {
                return TetherOp::Offset;
            }
            return {};
        }
        bool RefreshOffset()
        {
            if (!started || cancelled || !connected || !offset || pending || refreshOffset) {
                return false;
            }
            // Keep ownership until ClearOffset succeeds, even if renewal fails.
            refreshOffset = true;
            return true;
        }
        bool Begin(TetherOp op)
        {
            if (Next() != op) {
                return false;
            }
            pending = op;
            return true;
        }
        bool Complete(TetherOp op, bool success)
        {
            if (pending != op) {
                return false;
            }
            pending.reset();
            // A failed removal does not relinquish a live resource. The caller may
            // report success when the original actor/skeleton no longer exists.
            if (!success && IsCleanup(op)) {
                Cancel();
                return true;
            }
            switch (op) {
            case TetherOp::AddPlayer:
                player = success;
                break;
            case TetherOp::AddFollower:
                follower = success;
                break;
            case TetherOp::Connect:
                constraint = success;
                connected = success && !cancelled;
                break;
            case TetherOp::Disconnect:
                constraint = false;
                break;
            case TetherOp::RemoveFollower:
                follower = false;
                break;
            case TetherOp::RemovePlayer:
                player = false;
                break;
            case TetherOp::Offset:
                offset = offset || success;
                refreshOffset = false;
                break;
            case TetherOp::ClearOffset:
                offset = false;
                break;
            }
            if (!success) {
                Cancel();
            }
            return true;
        }
        // The VM rechecked proximity after dispatch and performed no mutation.
        // Keep acquired resources and retry on a fresh frame instead of forcing
        // a full teardown, cooldown and approach for a momentary gait change.
        bool Defer(TetherOp op)
        {
            if (pending != op || IsCleanup(op)) {
                return false;
            }
            pending.reset();
            return true;
        }
        bool Done() const
        {
            return started && cancelled && !pending && !player && !follower && !constraint && !offset;
        }
    };

    inline bool TetherStepCurrent(const TetherSession& session, std::int32_t token, std::int32_t ticket,
                                  int operation)
    {
        return token == ticket && session.started && session.pending &&
               static_cast<int>(*session.pending) == operation &&
               (IsCleanup(static_cast<TetherOp>(operation)) || !session.cancelled);
    }

    // Ticketed acknowledgement is issued only at the end of the selected slot's
    // Papyrus alias cleanup, never merely because a package release was requested.
    struct TetherHandoff
    {
        std::uint32_t actor{};
        std::int32_t ticket{};
        bool ready{}, cleanupComplete{};
        void Start(std::uint32_t id, std::int32_t value)
        {
            actor = id;
            ticket = value;
            ready = false;
            cleanupComplete = false;
        }
        bool RecordCleanup(std::uint32_t id, std::int32_t value, bool aliasEmpty)
        {
            if (!actor || actor != id || ticket != value || !aliasEmpty) {
                return false;
            }
            cleanupComplete = true;
            return true;
        }
        bool Confirm(std::uint32_t id, std::int32_t value, bool aliasEmpty, bool packageReleased)
        {
            if (actor == id && ticket == value && !aliasEmpty) {
                cleanupComplete = false;
            }
            if (!cleanupComplete || !actor || actor != id || ticket != value || !aliasEmpty ||
                !packageReleased) {
                return false;
            }
            ready = true;
            return true;
        }
    };

    inline bool TetherRetainsApproach(const TetherSession& session, const TetherHandoff& handoff)
    {
        return (!session.started || !session.cancelled) &&
               (!handoff.actor || (session.connected && !session.offset && !handoff.cleanupComplete));
    }

    inline bool TetherNear(float distance, float height, bool visible, float limit)
    {
        return std::isfinite(distance) && std::isfinite(height) && distance <= limit &&
               std::abs(height) <= 40 && visible;
    }

    inline bool CanCorrectTetherFacing(const TetherSession& session)
    {
        return session.started && !session.cancelled && !session.pending && !session.player &&
               !session.follower && !session.constraint;
    }

    inline bool TetherFacingReady(float headingError, float lateral, int side, float followerSpeed)
    {
        return std::isfinite(headingError) && std::abs(headingError) <= .2F && std::isfinite(lateral) &&
               lateral * side >= 15 && std::isfinite(followerSpeed) && followerSpeed <= 130;
    }
    inline bool TetherGripReady(float distance, float height, float wristGap, bool visible, float limit,
                                float wristLimit)
    {
        return TetherNear(distance, height, visible, std::min(limit, 80.0F)) && std::isfinite(wristGap) &&
               std::isfinite(wristLimit) && wristGap <= wristLimit;
    }

    // The follower is turned into place before any physics body exists. Settling
    // is therefore its own gate rather than a side effect of the grip test.
    inline bool TetherHeadingSettled(float headingError, float lateral, int side, float followerSpeed)
    {
        return std::isfinite(headingError) && std::abs(headingError) <= .2F && std::isfinite(lateral) &&
               lateral * side >= 15 && std::isfinite(followerSpeed) && followerSpeed <= 20;
    }

    // Arrival starts physics setup at the current spot, without a dwell or a
    // second positioning leg. Actual facing and wrist contact are checked again
    // before a body/constraint can be created.
    struct TetherArrival
    {
        float elapsed{};
        bool failed{};
        void Fail()
        {
            failed = true;
            elapsed = 0;
        }
        bool Observe(bool contact, float distance, float playerSpeed, float followerSpeed, float lateral,
                     float along, float headingError, float dt)
        {
            if (std::isfinite(distance) && distance > 150) {
                failed = false;
            }
            const bool settled = !failed && contact && std::isfinite(playerSpeed) &&
                                 std::isfinite(followerSpeed) && std::isfinite(lateral) &&
                                 std::isfinite(along) && std::isfinite(headingError) && playerSpeed <= 15 &&
                                 followerSpeed <= 130 && std::abs(lateral) >= 20 && std::abs(lateral) <= 65 &&
                                 std::hypot(std::abs(lateral) - 40, along + 10) <= 28;
            if (!settled || !std::isfinite(dt) || dt <= 0 || dt > 1) {
                elapsed = 0;
                return false;
            }
            return true;
        }
    };

    inline bool TetherAlignedGrip(float distance, float height, float wristGap, bool visible, float limit,
                                  float lateral, int side, float headingError, float playerSpeed,
                                  float followerSpeed, float wristLimit)
    {
        return TetherGripReady(distance, height, wristGap, visible, limit, wristLimit) &&
               std::isfinite(lateral) && lateral * side >= 15 && std::isfinite(headingError) &&
               std::abs(headingError) <= .35F && std::isfinite(playerSpeed) && playerSpeed <= 15 &&
               std::isfinite(followerSpeed) && followerSpeed <= 20;
    }

    inline int TetherApproachSide(float lateral, int previous)
    {
        return std::isfinite(lateral) && std::abs(lateral) > 12 ? (lateral < 0 ? -1 : 1) : previous;
    }
    inline float TetherFollowerSpeed(float distance, bool connected)
    {
        // Offset movement already manages arrival; a second catch-up controller
        // caused overshoot in r14. Retain native gait speed in every phase.
        (void)distance;
        (void)connected;
        return 1;
    }
    // A short lag must not drop the grip at the first frame above the configured
    // soft limit. The hard cap still releases immediately to prevent a long pull.
    struct TetherSeparation
    {
        float elapsed{};
        bool Release(float distance, float limit, float dt)
        {
            if (!std::isfinite(distance) || !std::isfinite(limit)) {
                return true;
            }
            if (distance > limit + 45) {
                return true;
            }
            if (distance > limit) {
                if (std::isfinite(dt) && dt > 0) {
                    elapsed += dt;
                }
            }
            else {
                elapsed = 0;
            }
            return elapsed >= .4F;
        }
    };

    // Adapted from KEI997's TETHER GetSpeedMultiplierFor (MIT).
    // Copyright (c) 2026 KEI997. See package/ThirdParty/TETHER-LICENSE.txt.
    inline float TetherPlayerSpeed(float distance, float ideal, float slack)
    {
        if (!std::isfinite(distance) || !std::isfinite(ideal) || !std::isfinite(slack)) {
            return 1;
        }
        const auto tension = std::clamp((distance - ideal) / std::max(.001F, slack - ideal), 0.0F, 1.0F);
        return std::lerp(1.0F, .55F, tension);
    }
} // namespace Wayfarer::HandHolding

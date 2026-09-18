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
    // Acquisition gates, narrowest first. Only the offset fights WWM's own
    // package, and only the constraint depends on where the hands are, so the
    // ragdoll bodies are created while the pair is still turning into place
    // instead of costing three more Papyrus round trips after contact.
    inline bool NeedsHandoff(TetherOp op)
    {
        return !IsCleanup(op);
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
            if (!offset) {
                return TetherOp::Offset;
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
            return {};
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
                offset = success;
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

    inline bool TetherNear(float distance, float height, bool visible, float limit)
    {
        return std::isfinite(distance) && std::isfinite(height) && distance <= limit &&
               std::abs(height) <= 40 && visible;
    }
    inline bool TetherGripReady(float distance, float height, float wristGap, bool visible, float limit,
                                float wristLimit)
    {
        return TetherNear(distance, height, visible, std::min(limit, 80.0F)) && std::isfinite(wristGap) &&
               std::isfinite(wristLimit) && wristGap <= wristLimit;
    }

    // The ball-and-socket constraint pulls the two palm anchors onto one another
    // the instant it is created, so a grip formed across a wide gap reads as a
    // lunge instead of a clasp. Hold out for a close gap first and widen the
    // requirement only if the pair cannot reach it, so acquisition still
    // completes on stairs, uneven ground and with mismatched body heights.
    inline constexpr float tetherGripGapRelaxed = 45.0F;
    // Short, because an unreachable accurate gap must not cost more time than
    // the wide gate used to. The acquisition trace prints the measured gap
    // against the limit every second, so fGripWristGap can be set from a log
    // rather than guessed for a given body and hand mesh.
    inline constexpr float tetherGripGapPatience = 1.5F;
    inline float TetherGripGap(float elapsed, float tight, float relaxed, float patience)
    {
        if (!std::isfinite(tight)) {
            return 0;
        }
        if (!std::isfinite(elapsed) || elapsed <= 0 || !std::isfinite(relaxed) || relaxed <= tight ||
            !std::isfinite(patience) || patience <= 0) {
            return tight;
        }
        return tight + (relaxed - tight) * std::clamp(elapsed / patience, 0.0F, 1.0F);
    }

    // The follower is turned into place before any physics body exists. Settling
    // is therefore its own gate rather than a side effect of the grip test.
    inline bool TetherHeadingSettled(float headingError, float lateral, int side, float followerSpeed)
    {
        return std::isfinite(headingError) && std::abs(headingError) <= .2F && std::isfinite(lateral) &&
               lateral * side >= 15 && std::isfinite(followerSpeed) && followerSpeed <= 20;
    }

    // Arrival hands movement to offset positioning; it does not attach physics.
    // Facing and hand contact are checked AFTER offset positioning, avoiding the
    // r15 requirement to reach an aligned grip before alignment can even begin.
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
            elapsed += dt;
            return elapsed >= .2F;
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

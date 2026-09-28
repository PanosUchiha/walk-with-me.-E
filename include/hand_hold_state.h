#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace Wayfarer::HandHolding
{
    enum class Method
    {
        Tether,
        Classic
    };

    struct Tuning
    {
        bool enabled{};
        Method method{Method::Classic};
        std::string partner, partnerName;
        float delay{1.0F}, connectDistance{90.0F}, releaseDistance{120.0F};
        // Palm anchor in hand-local game units. The skeleton's hands are mirrored,
        // so one triple describes the same surface point on both palms. Exposed
        // because the correct value depends on the body and hand meshes in use.
        float palmX{0.0F}, palmY{-2.35F}, palmZ{5.0F};
        // Wrist separation the constraint is allowed to close when it is created.
        float gripGap{45.0F};
        // Classic only: distance between the two actors' centres while holding, and the
        // companion's side (0 = whichever side they are already nearer, -1 left, 1 right).
        float spacing{46.0F};
        int side{};
    };

    enum class Phase
    {
        Waiting,
        Searching,
        Holding,
        Releasing
    };

    inline bool TravelRequested(bool ordinaryTravel, bool companionMode, const Tuning& tuning,
                                std::uint32_t actor, std::uint32_t approachActor)
    {
        return ordinaryTravel || (companionMode && tuning.enabled && !tuning.partner.empty() && actor != 0 &&
                                  actor == approachActor);
    }

    class State
    {
      public:
        void Reset()
        {
            phase = Phase::Waiting;
            elapsed = weight = 0;
            actor = 0;
        }
        void Release(bool immediate = false)
        {
            elapsed = 0;
            if (immediate || weight <= 0) {
                Reset();
            }
            else {
                phase = Phase::Releasing;
            }
        }
        void Tick(float dt, bool permitted, bool paused, const Tuning& tuning)
        {
            if (selection != tuning.partner || delay != tuning.delay || enabled != tuning.enabled) {
                Reset();
                selection = tuning.partner;
                delay = tuning.delay;
                enabled = tuning.enabled;
            }
            if (!std::isfinite(dt) || dt <= 0 || dt > 1) {
                return;
            }
            if (!permitted || !enabled || selection.empty()) {
                Reset();
                return;
            }
            if (paused) {
                return;
            }
            if (phase == Phase::Releasing) {
                weight = std::max(0.0F, weight - dt / .35F);
                if (weight == 0) {
                    Reset();
                }
                return;
            }
            if (phase == Phase::Holding) {
                weight = std::min(1.0F, weight + dt / .65F);
                return;
            }
            if (phase == Phase::Waiting) {
                elapsed += dt;
                if (elapsed >= delay) {
                    phase = Phase::Searching;
                }
            }
        }
        bool Connect(std::uint32_t id, float distance, bool reachable, const Tuning& tuning)
        {
            if (phase != Phase::Searching || !id || !reachable || !std::isfinite(distance) ||
                distance > tuning.connectDistance) {
                return false;
            }
            actor = id;
            weight = 0;
            phase = Phase::Holding;
            return true;
        }
        bool Maintain(std::uint32_t id, float distance, bool reachable, const Tuning& tuning)
        {
            if (phase != Phase::Holding) {
                return false;
            }
            if (id != actor || !reachable || !std::isfinite(distance) || distance > tuning.releaseDistance) {
                Release();
                return false;
            }
            return true;
        }
        Phase GetPhase() const { return phase; }
        bool Searching() const { return phase == Phase::Searching; }
        std::uint32_t Partner() const { return actor; }
        float Weight() const
        {
            const float t = std::clamp(weight, 0.0F, 1.0F);
            return t * t * (3 - 2 * t);
        }

      private:
        Phase phase{Phase::Waiting};
        float elapsed{}, weight{}, delay{-1};
        bool enabled{};
        std::uint32_t actor{};
        std::string selection;
    };
} // namespace Wayfarer::HandHolding

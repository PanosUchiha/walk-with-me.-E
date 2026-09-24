#include "hand_holding_backends.h"

namespace Wayfarer::HandHolding
{
    namespace
    {
        std::recursive_mutex mutex;
        Method active{Method::Classic};
    } // namespace

    std::string ReferenceKey(RE::Actor& actor)
    {
        return Tether::ReferenceKey(actor);
    }

    void Install()
    {
        // Install once; Classic's hooks pass straight through while its state is idle.
        Classic::Install();
        Tether::Install();
    }

    void Reset()
    {
        std::lock_guard lock(mutex);
        Classic::Reset();
        Tether::Reset();
    }

    void OnGameLoaded()
    {
        std::lock_guard lock(mutex);
        Classic::Reset();
        Tether::OnGameLoaded();
    }

    bool BeginFrame(float dt, bool permitted, bool paused, const Tuning& tuning)
    {
        std::lock_guard lock(mutex);
        if (active != tuning.method) {
            if (active == Method::Classic) {
                Classic::Reset();
            }
            else {
                // Keep pumping asynchronous physics teardown before Classic can touch
                // either actor. Failed or outstanding callbacks must retain ownership.
                auto disabled = tuning;
                disabled.enabled = false;
                Tether::BeginFrame(dt, false, paused, disabled);
                if (Tether::CleanupPending()) {
                    return false;
                }
            }
            active = tuning.method;
            logger::info("[HandHolding] Method: {}", active == Method::Classic ? "Classic" : "TETHER");
        }
        return active == Method::Classic ? Classic::BeginFrame(dt, permitted, paused, tuning)
                                         : Tether::BeginFrame(dt, permitted, paused, tuning);
    }

    void UpdatePartner(RE::PlayerCharacter& player, RE::Actor* partner, float dt,
                       const TravelNavigation& navigation)
    {
        std::lock_guard lock(mutex);
        if (active == Method::Classic) {
            Classic::UpdatePartner(player, partner, dt, navigation);
        }
        else {
            Tether::UpdatePartner(player, partner, dt, navigation);
        }
    }

    Approach GetApproach()
    {
        std::lock_guard lock(mutex);
        return active == Method::Classic ? Classic::GetApproach() : Tether::GetApproach();
    }

    bool IsAttached(RE::FormID actor)
    {
        std::lock_guard lock(mutex);
        return active == Method::Classic ? Classic::IsAttached(actor) : Tether::IsAttached(actor);
    }

    void ReleaseActor(RE::FormID actor)
    {
        std::lock_guard lock(mutex);
        Classic::ReleaseActor(actor);
        Tether::ReleaseActor(actor);
    }

    // Classic retains the native travel package, as in the original implementation.
    // TETHER owns these endpoints even while a method change is waiting for cleanup.
    bool OwnsMovement(RE::FormID actor)
    {
        return Tether::OwnsMovement(actor);
    }
    std::int32_t HandoffTicket(RE::FormID actor)
    {
        return Tether::HandoffTicket(actor);
    }
    bool HandoffCleanupFinished(RE::FormID actor, std::int32_t ticket)
    {
        return Tether::HandoffCleanupFinished(actor, ticket);
    }
    void ConfirmHandoff(RE::FormID actor, std::int32_t ticket, bool aliasEmpty, bool packageReleased)
    {
        Tether::ConfirmHandoff(actor, ticket, aliasEmpty, packageReleased);
    }
    std::int32_t TetherStepStatus(std::int32_t ticket, std::int32_t operation, RE::Actor* player,
                                  RE::Actor* follower)
    {
        return Tether::TetherStepStatus(ticket, operation, player, follower);
    }
    float SpeedMultiplier(RE::Actor* actor)
    {
        return Tether::SpeedMultiplier(actor);
    }
} // namespace Wayfarer::HandHolding

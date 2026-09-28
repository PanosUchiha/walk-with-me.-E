#include "travel_speed.h"
#include "hand_holding.h"
#include <bit>

namespace Wayfarer
{
    void TravelSpeed::Install()
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data) {
            return;
        }
        for (std::uint32_t slot = 0; slot < packages.size(); ++slot) {
            auto* package = data->LookupForm<RE::TESPackage>(PackageLocal(slot), "Wayfarer.esp");
            if (!package) {
                logger::error("[Speed] Missing travel package; speed adjustment disabled");
                return;
            }
            packages[slot] = package->GetFormID();
            auto* sandbox = data->LookupForm<RE::TESPackage>(SandboxPackageLocal(slot), "Wayfarer.esp");
            if (!sandbox) {
                logger::error("[Speed] Missing sandbox package");
                return;
            }
            sandboxPackages[slot] = sandbox->GetFormID();
        }
        // Speed-factor call in Actor::SetMaximumMovementSpeed. The engine consumes the
        // adjusted factor through its normal locomotion pipeline; actor values are untouched.
        // Call-site reference: https://github.com/Kei-BWV997/TETHER/blob/main/src/SpeedHook.cpp
        // The call must still go to GetScale. A target outside the game's code is another
        // plugin's hook on the same call, which chains safely.
        REL::Relocation<std::uintptr_t> site{RELOCATION_ID(37013, 37943), REL::Relocate(0x1A, 0x51)};
        REL::Relocation<std::uintptr_t> getScale{RELOCATION_ID(19238, 19664)};
        const auto target = site.address() + 5 + *reinterpret_cast<const std::int32_t*>(site.address() + 1);
        const auto text = REL::Module::get().segment(REL::Segment::textx);
        const bool gameCode = target >= text.address() && target < text.address() + text.size();
        if (*reinterpret_cast<const std::uint8_t*>(site.address()) != 0xE8 ||
            (gameCode && target != getScale.address())) {
            logger::error("[Speed] Unexpected hook instruction; retaining native gait speeds");
            return;
        }
        SKSE::AllocTrampoline(64);
        original = SKSE::GetTrampoline().write_call<5>(site.address(), GetMultiplier);
        logger::info("[Speed] Temporary travel speed adjustment installed");
    }

    void TravelSpeed::Set(std::uint32_t slot, RE::FormID actor, float scale, float nearCap,
                          float nearDistance)
    {
        if (slot >= leases.size()) {
            return;
        }
        nearCaps[slot].store(nearCap, std::memory_order_relaxed);
        nearDistances[slot].store(nearDistance, std::memory_order_relaxed);
        const auto bits = std::bit_cast<std::uint32_t>(std::clamp(scale, 0.65F, 3.5F));
        leases[slot].store((static_cast<std::uint64_t>(actor) << 32) | bits, std::memory_order_release);
    }

    void TravelSpeed::Clear(std::uint32_t slot)
    {
        if (slot < leases.size()) {
            leases[slot].store(0, std::memory_order_release);
        }
    }

    float TravelSpeed::GetMultiplier(RE::Actor* actor)
    {
        const float base = original(actor);
        const float tetherScale = HandHolding::SpeedMultiplier(actor);
        if (tetherScale != 1.0F) {
            return base * tetherScale;
        }
        if (!actor || actor->IsPlayerRef() || actor->IsDead() || actor->IsInCombat() || actor->IsSneaking() ||
            actor->IsOnMount() || actor->GetCurrentScene()) {
            return base;
        }
        const auto* state = actor->AsActorState();
        if (!state || state->IsSwimming() || state->IsWeaponDrawn()) {
            return base;
        }
        const auto* package = actor->GetCurrentPackage();
        if (!package) {
            return base;
        }
        for (std::size_t slot = 0; slot < leases.size(); ++slot) {
            const auto lease = leases[slot].load(std::memory_order_acquire);
            if (lease && static_cast<RE::FormID>(lease >> 32) == actor->GetFormID() &&
                (package->GetFormID() == packages[slot] || package->GetFormID() == sandboxPackages[slot])) {
                float scale = std::bit_cast<float>(static_cast<std::uint32_t>(lease));
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    const auto a = actor->GetPosition(), p = player->GetPosition();
                    if (std::hypot(a.x - p.x, a.y - p.y) <=
                        nearDistances[slot].load(std::memory_order_relaxed)) {
                        scale = std::min(scale, nearCaps[slot].load(std::memory_order_relaxed));
                    }
                }
                return base * scale;
            }
        }
        return base;
    }
} // namespace Wayfarer

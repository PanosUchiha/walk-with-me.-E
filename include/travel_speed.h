#pragma once
#include "party_constants.h"

namespace Wayfarer
{
    class TravelSpeed
    {
      public:
        static void Install();
        static void Set(std::uint32_t a_slot, RE::FormID a_actor, float a_scale, float nearCap = 1.0F,
                        float nearDistance = 500.0F);
        static void Clear(std::uint32_t a_slot);

      private:
        static float GetMultiplier(RE::Actor* a_actor);
        static inline REL::Relocation<decltype(GetMultiplier)> original;

        static inline std::array<std::atomic<std::uint64_t>, PARTY_CAPACITY> leases{};
        static inline std::array<std::atomic<float>, PARTY_CAPACITY> nearCaps{}, nearDistances{};
        static inline std::array<RE::FormID, PARTY_CAPACITY> packages{};
        static inline std::array<RE::FormID, PARTY_CAPACITY> sandboxPackages{};
    };
} // namespace Wayfarer

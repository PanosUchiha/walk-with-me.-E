#pragma once
#include <cstdint>
namespace Wayfarer
{
    inline constexpr int PARTY_CAPACITY = 10;

    constexpr std::uint32_t PackageLocal(int slot)
    {
        return 0x800 + slot + (slot >= 5 ? 1 : 0);
    }
    constexpr std::uint32_t FollowerAlias(int slot)
    {
        return slot < 5 ? slot : slot + 5;
    }
    constexpr std::uint32_t MarkerAlias(int slot)
    {
        return slot < 5 ? slot + 5 : slot + 10;
    }
    constexpr std::uint32_t SandboxPackageLocal(int slot)
    {
        return 0x810 + slot;
    }
    constexpr std::uint32_t SocialPackageLocal(int slot)
    {
        return 0x820 + slot;
    }
    constexpr std::uint32_t SocialGlobalLocal(int slot)
    {
        return 0x830 + slot;
    }
    constexpr std::uint32_t PosePackageLocal(int slot)
    {
        return 0x840 + slot;
    }
    constexpr std::uint32_t SeatPackageLocal(int slot)
    {
        return 0x850 + slot;
    }
    constexpr std::uint32_t PoseGlobalLocal(int slot)
    {
        return 0x860 + slot;
    }
    constexpr std::uint32_t SeatAlias(int slot)
    {
        return 20 + slot;
    }
    constexpr std::uint32_t GatherAlias(int slot)
    {
        return 30 + slot;
    }
    constexpr std::uint32_t GatherPackageLocal(int slot)
    {
        return 0x870 + slot;
    }
    inline constexpr std::uint32_t SANDBOX_GLOBAL = 0x80B;
} // namespace Wayfarer

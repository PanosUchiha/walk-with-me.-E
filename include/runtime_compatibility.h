#pragma once
#include <array>
#include <cstdint>

namespace Wayfarer::RuntimeCompatibility
{
    using Version = std::array<std::uint16_t, 4>;

    // SKSE reports the store in the last field (0 Steam, 1 GOG). GOG 1.6.1170 shares Steam's
    // number and would load Steam's Address Library, so only GOG's own 659 and 1179 pass.
    constexpr std::uint16_t GOG = 1;

    constexpr bool Supports(Version version, bool modernLibrary)
    {
        if (version[0] != 1) {
            return false;
        }
        if (version[3] == GOG) {
            return version[1] == 6 && (version[2] == 659 || version[2] == 1179);
        }
        if (version[3] != 0) {
            return false;
        }
        if (version[1] == 5) {
            return version[2] == 97;
        }
        if (version[1] == 6) {
            for (const auto patch : {318, 323, 342, 353, 629, 640, 659, 1130, 1170, 1179}) {
                if (version[2] == patch) {
                    return true;
                }
            }
        }
        return modernLibrary && version[1] == 7 && (version[2] == 99 || version[2] == 104);
    }
} // namespace Wayfarer::RuntimeCompatibility

#pragma once
#include <array>
#include <cstdint>

namespace Wayfarer::RuntimeCompatibility
{
    using Version = std::array<std::uint16_t, 4>;
    constexpr bool Supports(Version version, bool modernLibrary)
    {
        if (version[0] != 1 || version[3] != 0) {
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

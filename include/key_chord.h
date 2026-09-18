#pragma once
#include <cstdint>
#include <string>

namespace Wayfarer::Hotkeys
{
    constexpr std::uint32_t unbound = 0xFFFFFFFF;
    constexpr std::uint32_t shift = 1, ctrl = 2, alt = 4, allModifiers = shift | ctrl | alt;

    constexpr std::uint32_t ModifierForKey(std::uint32_t key)
    {
        switch (key) {
        case 0x2A:
        case 0x36:
            return shift;
        case 0x1D:
        case 0x9D:
            return ctrl;
        case 0x38:
        case 0xB8:
            return alt;
        default:
            return 0;
        }
    }

    template <class IsPressed> std::uint32_t ReadModifiers(IsPressed pressed)
    {
        return ((pressed(0x2A) || pressed(0x36)) ? shift : 0) |
               ((pressed(0x1D) || pressed(0x9D)) ? ctrl : 0) | ((pressed(0x38) || pressed(0xB8)) ? alt : 0);
    }

    struct Chord
    {
        std::uint32_t key{unbound}, modifiers{};

        [[nodiscard]] bool Matches(std::uint32_t pressedKey, std::uint32_t heldModifiers) const
        {

            return key != unbound && key == pressedKey && (heldModifiers & ~ModifierForKey(key)) == modifiers;
        }
        bool operator==(const Chord&) const = default;
    };

    constexpr std::uint32_t NormalizeModifiers(std::uint32_t key, long modifiers)
    {
        return key == unbound || modifiers < 0 || modifiers > allModifiers
                   ? 0
                   : static_cast<std::uint32_t>(modifiers) & ~ModifierForKey(key);
    }

    inline int Capture(std::uint32_t key, std::uint32_t modifiers, bool down, bool up)
    {
        if (key > 255) {
            return -1;
        }
        if (ModifierForKey(key)) {
            if (!up || (modifiers & ~ModifierForKey(key))) {
                return -1;
            }
        }
        else if (!down) {
            return -1;
        }
        return static_cast<int>(key | (NormalizeModifiers(key, modifiers) << 8));
    }

    inline Chord CapturedChord(int value)
    {
        const auto key = static_cast<std::uint32_t>(value) & 255;
        return key == 211 ? Chord{} : Chord{key, static_cast<std::uint32_t>(value) >> 8};
    }

    inline std::string ModifierPrefix(std::uint32_t modifiers)
    {
        std::string result;
        if (modifiers & ctrl) {
            result += "Ctrl + ";
        }
        if (modifiers & shift) {
            result += "Shift + ";
        }
        if (modifiers & alt) {
            result += "Alt + ";
        }
        return result;
    }
} // namespace Wayfarer::Hotkeys

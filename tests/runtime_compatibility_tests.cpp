#include "runtime_compatibility.h"
#include <iostream>

int main()
{
    using Wayfarer::RuntimeCompatibility::Supports;
    int failures = 0;
    auto check = [&](bool result) {
        if (!result) {
            ++failures;
        }
    };
    for (bool modern : {false, true}) {
        check(Supports({1, 5, 97, 0}, modern));
        for (auto patch : {318, 323, 342, 353, 629, 640, 659, 1130, 1170, 1179}) {
            check(Supports({1, 6, static_cast<std::uint16_t>(patch), 0}, modern));
        }
        check(!Supports({1, 4, 15, 0}, modern));
        check(!Supports({1, 6, 678, 0}, modern));
        check(!Supports({1, 6, 1170, 1}, modern));
        check(!Supports({1, 7, 200, 0}, modern));
        check(!Supports({2, 0, 0, 0}, modern));
    }
    // GOG ships 1.6.1179, which is an ordinary post-629 AE runtime as far as both
    // libraries are concerned; nothing about it needs the NG build specifically.
    check(Supports({1, 6, 1179, 0}, false) && Supports({1, 6, 1179, 0}, true));
    for (auto patch : {99, 104}) {
        check(!Supports({1, 7, static_cast<std::uint16_t>(patch), 0}, false));
        check(Supports({1, 7, static_cast<std::uint16_t>(patch), 0}, true));
    }
    if (failures) {
        std::cerr << failures << " runtime compatibility failures\n";
    }
    return failures ? 1 : 0;
}

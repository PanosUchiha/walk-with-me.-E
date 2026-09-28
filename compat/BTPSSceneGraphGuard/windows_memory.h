#pragma once
#include "traversal.h"
#include <Windows.h>
#include <array>
#include <cstring>
#include <span>

namespace Guard
{
inline int ReadException(unsigned code) noexcept
{
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ?
        EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
// This boundary contains only a read and trivial locals. It cannot abandon
// engine locks, C++ ownership objects or a partially incremented reference count.
__declspec(noinline) inline bool SafeRead(Address source, void* destination, std::size_t size) noexcept
{
    if (!source || source >= 0x0000800000000000 || size > 0x0000800000000000 - source) return false;
    __try {
        std::memcpy(destination, reinterpret_cast<const void*>(source), size);
        return true;
    } __except (ReadException(GetExceptionCode())) {
        return false;
    }
}
// Caller verifies the complete DLL identity as well as this live function's
// bytes. Called once during SKSE PostPostLoad, before any game is loaded.
inline bool InstallJump(void* address, Address replacement, std::span<const unsigned char> expected)
{
    if (expected.size() < 14 || !replacement) return false;
    std::array<unsigned char, 256> actual{};
    if (expected.size() > actual.size() || !SafeRead(reinterpret_cast<Address>(address), actual.data(), expected.size()) ||
        std::memcmp(actual.data(), expected.data(), expected.size())) return false;
    std::array<unsigned char, 14> jump{0xFF, 0x25, 0, 0, 0, 0};
    std::memcpy(jump.data() + 6, &replacement, sizeof(replacement));
    DWORD previous{};
    if (!VirtualProtect(address, jump.size(), PAGE_EXECUTE_READWRITE, &previous)) return false;
    std::memcpy(address, jump.data(), jump.size());
    FlushInstructionCache(GetCurrentProcess(), address, jump.size());
    DWORD unused{};
    VirtualProtect(address, jump.size(), previous, &unused);
    return true;
}
}

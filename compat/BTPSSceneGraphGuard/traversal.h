#pragma once
#include <cstddef>
#include <cstdint>

namespace Guard
{
using Address = std::uintptr_t;
using Reader = bool (*)(Address, void*, std::size_t) noexcept;
inline bool AddressValid(Address value) noexcept
{
    return value >= 0x10000 && value < 0x0000800000000000 && (value & 7) == 0;
}
struct Context
{
    Reader read;
    Address nodeAsNode;
    Address leafAsNode;
    unsigned budget{2048};
    bool invalid{};
    bool unsupported{};
};
template <class T>
bool Read(Context& context, Address address, T& value) noexcept
{
    if (!context.read(address, &value, sizeof(value))) {
        context.invalid = true;
        return false;
    }
    return true;
}

// Skyrim 1.5.97 NiTArray fields following the array's vtable. Deliberately plain
// values: reading a bad child must never increment an intrusive reference count.
struct Children
{
    Address data;
    std::uint16_t capacity, freeIndex, size, growth;
};
static_assert(sizeof(Children) == 16);

// Equivalent depth and userdata matching to BTPS FindChildObjectREFR. We inspect
// the known AsNode slot instead of calling through a possibly stale vtable.
// Unsupported overrides and unreadable branches are skipped, never selected.
inline bool FindChild(Context& context, Address node, Address target, int depth = 0) noexcept
{
    if (!node || !target) return false;
    if (!AddressValid(node) || !AddressValid(target) || depth < 0 || depth > 3 || !context.budget) {
        context.invalid = true;
        return false;
    }
    --context.budget;
    Address table{}, asNode{}, user{};
    if (!Read(context, node, table) || !AddressValid(table) ||
        !Read(context, table + 3 * sizeof(Address), asNode)) {
        context.invalid = true;
        return false;
    }
    if (asNode != context.nodeAsNode && asNode != context.leafAsNode) {
        context.unsupported = true;
        return false;
    }
    if (!Read(context, node + 0xF8, user)) return false;
    if (user == target) return true;
    if (asNode != context.nodeAsNode || depth > 2) return false;

    Children children{};
    if (!Read(context, node + 0x118, children)) return false;
    if (children.size > children.capacity || children.freeIndex > children.capacity ||
        children.capacity > 4096 || (children.capacity && !AddressValid(children.data))) {
        context.invalid = true;
        return false;
    }
    for (unsigned index = 0; index < children.capacity; ++index) {
        if (!context.budget) {
            context.invalid = true;
            return false;
        }
        --context.budget;
        Address child{};
        if (!Read(context, children.data + index * sizeof(Address), child)) return false;
        if (child && FindChild(context, child, target, depth + 1)) return true;
    }
    return false;
}
}

#pragma once
#include <windows.h>

// Fight With Me is a separate mod that gives companions battle orders. When its DLL is
// loaded, the party actions grid opens its battle plan, and the party is left to it for as
// long as it has companions fighting; otherwise the tile stays dimmed and Walk With Me works
// exactly as it does on its own. Nothing here links against it: the exports are looked up by
// name, and only once one is found is the answer kept.
namespace Wayfarer::FightWithMe
{
    using OpenFunction = bool (*)();
    using InBattleFunction = bool (*)();

    template <class Function>
    Function Export(Function& a_cached, const char* a_name)
    {
        if (!a_cached) {
            if (const HMODULE module = GetModuleHandleW(L"FightWithMe.dll")) {
                a_cached = reinterpret_cast<Function>(GetProcAddress(module, a_name));
            }
        }
        return a_cached;
    }

    inline OpenFunction OpenExport()
    {
        static OpenFunction open = nullptr;
        return Export(open, "FightWithMe_OpenBattlePlan");
    }

    inline bool Available() { return OpenExport() != nullptr; }

    inline bool OpenBattlePlan()
    {
        const auto open = OpenExport();
        return open && open();
    }

    // Fight With Me has companions in a fight, or has just called them into one. Older versions
    // without the export answer false, and the usual combat checks decide alone.
    inline bool InBattle()
    {
        static InBattleFunction inBattle = nullptr;
        const auto call = Export(inBattle, "FightWithMe_InBattle");
        return call && call();
    }
} // namespace Wayfarer::FightWithMe

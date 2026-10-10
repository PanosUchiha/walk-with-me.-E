#pragma once
#include <windows.h>

// Fight With Me is a separate mod that gives companions battle orders. When its DLL is
// loaded, the party actions grid opens its battle plan; otherwise the tile stays dimmed
// and Walk With Me works exactly as it does on its own. Nothing here links against it:
// the export is looked up by name, and only once it is found is the answer kept.
namespace Wayfarer::FightWithMe
{
    using OpenFunction = bool (*)();

    inline OpenFunction OpenExport()
    {
        static OpenFunction open = nullptr;
        if (!open) {
            if (const HMODULE module = GetModuleHandleW(L"FightWithMe.dll")) {
                open = reinterpret_cast<OpenFunction>(GetProcAddress(module, "FightWithMe_OpenBattlePlan"));
            }
        }
        return open;
    }

    inline bool Available() { return OpenExport() != nullptr; }

    inline bool OpenBattlePlan()
    {
        const auto open = OpenExport();
        return open && open();
    }
} // namespace Wayfarer::FightWithMe

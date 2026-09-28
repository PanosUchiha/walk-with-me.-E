#include "hotkey_settings.h"
#include <iostream>

int main()
{
    using namespace Wayfarer;
    using namespace Wayfarer::Hotkeys;
    using Chord = Wayfarer::Hotkeys::Chord;
    int failures = 0;
    auto check = [&](bool result, const char* message) {
        if (!result) {
            ++failures;
            std::cerr << message << '\n';
        }
    };
    constexpr std::uint32_t g = 0x22;
    std::array<bool, 256> keyboard{};
    auto held = [&] { return ReadModifiers([&](std::uint32_t key) { return keyboard[key]; }); };
    for (auto pair : {std::pair{0x2AU, 0x36U}, {0x1DU, 0x9DU}, {0x38U, 0xB8U}}) {
        keyboard.fill(false);
        keyboard[pair.first] = true;
        check(held() == ModifierForKey(pair.first), "left modifier recognized");
        keyboard[pair.second] = true;
        keyboard[pair.first] = false;
        check(held() == ModifierForKey(pair.second), "releasing left preserves held right modifier");
        keyboard.fill(false);
        check(held() == 0, "cleared keyboard snapshot cannot retain a stuck modifier");
    }
    for (std::uint32_t mask = 0; mask <= allModifiers; ++mask) {
        keyboard.fill(false);
        keyboard[0x36] = (mask & shift) != 0;
        keyboard[0x9D] = (mask & ctrl) != 0;
        keyboard[0xB8] = (mask & alt) != 0;
        check(held() == mask, "combined snapshot recognized without prior events");
        const Chord chord{g, mask};
        for (std::uint32_t actual = 0; actual <= allModifiers; ++actual) {
            check(chord.Matches(g, actual) == (actual == mask),
                  "only exact chord activates, including plain keys");
            check(!chord.Matches(g + 1, actual), "different main key does not activate");
        }
        check(CapturedChord(Capture(g, mask, true, false)) == chord, "capture preserves complete chord");
        check(Capture(g, mask, false, false) == -1 && Capture(g, mask, false, true) == -1,
              "held and released main key cannot recapture or repeat an action");
        check((Capture(1, mask, true, false) & 255) == 1, "Escape cancels even with modifiers");
        check(CapturedChord(Capture(211, mask, true, false)) == Chord{},
              "Delete clears both key and modifiers");

        SettingsData original;
        for (std::size_t row = 0; row < hotkeyBindings.size(); ++row) {
            hotkeyBindings[row].Set(original, {static_cast<std::uint32_t>(g + row), mask});
        }
        CSimpleIniA ini;
        ini.SetBoolValue("HandHolding", "bEnabled", true);
        SaveHotkeys(ini, original);
        std::string serialized;
        check(ini.Save(serialized) >= 0, "INI serialization succeeds");
        CSimpleIniA reread;
        check(reread.LoadData(serialized) >= 0, "INI parses after serialization");
        SettingsData restored;
        LoadHotkeys(reread, restored);
        for (const auto& binding : hotkeyBindings) {
            check(binding.Get(original) == binding.Get(restored),
                  "every action retains its key and modifier after INI roundtrip");
        }
        check(reread.GetBoolValue("HandHolding", "bEnabled", false),
              "saving hotkeys preserves unrelated INI settings");
    }
    for (auto key : {0x2AU, 0x36U, 0x1DU, 0x9DU, 0x38U, 0xB8U}) {
        check(Capture(key, ModifierForKey(key), true, false) == -1, "modifier-down waits for the main key");
        check(CapturedChord(Capture(key, 0, false, true)) == Chord{key, 0},
              "standalone modifier can still be rebound on release");
        check(Chord{key, 0}.Matches(key, ModifierForKey(key)),
              "existing modifier-only INI binding remains usable");
    }
    check(Capture(0x2A, ctrl, false, true) == -1,
          "releasing one of several modifiers does not finish capture");
    check(!Chord{}.Matches(unbound, 0), "unbound never triggers");
    check(Capture(256, 0, true, false) == -1, "non-keyboard scan code cannot corrupt packed capture");
    check(ModifierPrefix(ctrl | shift | alt) == "Ctrl + Shift + Alt + ",
          "display includes each selected modifier");

    SettingsData settings;
    RebindHotkey(settings, 0, {g, 0});
    RebindHotkey(settings, 1, {g, ctrl});
    RebindHotkey(settings, 2, {g, shift});
    RebindHotkey(settings, 3, {g, ctrl | shift});
    check(hotkeyBindings[0].Get(settings) == Chord{g, 0} &&
              hotkeyBindings[1].Get(settings) == Chord{g, ctrl} &&
              hotkeyBindings[2].Get(settings) == Chord{g, shift},
          "same base key supports independent actions");
    RebindHotkey(settings, 4, {g, ctrl});
    check(hotkeyBindings[1].Get(settings) == Chord{} && hotkeyBindings[4].Get(settings) == Chord{g, ctrl},
          "rebinding an identical chord moves it to one action");
    check(hotkeyBindings[0].Get(settings) == Chord{g, 0} &&
              hotkeyBindings[2].Get(settings) == Chord{g, shift} &&
              hotkeyBindings[3].Get(settings) == Chord{g, ctrl | shift},
          "moving a chord preserves other combinations");
    RebindHotkey(settings, 4, {});
    check(hotkeyBindings[4].Get(settings) == Chord{}, "Clear removes modifiers too");

    CSimpleIniA old;
    old.LoadData("[Hotkeys]\niCommandKey=34\niScoutKey=-1\n");
    SettingsData legacy;
    LoadHotkeys(old, legacy);
    check(legacy.companionKey == unbound, "New companion default does not steal an existing G binding");
    check(SettingsData{}.handHoldKey == unbound && legacy.handHoldKey == unbound &&
              std::string_view{hotkeyBindings[handHoldHotkeyRow].label} == "Hold hands" &&
              std::string_view{hotkeyBindings[companionHotkeyRow].iniKey} == "iCompanionKey",
          "Hold hands ships unbound and the named rows match their actions");
    check(legacy.commandKey == g && legacy.scoutKey == unbound && legacy.toggleKey == 0x44 &&
              legacy.reloadKey == 0x58,
          "old INI preserves customized keys, unbound entries and shipped defaults");
    for (const auto& binding : hotkeyBindings) {
        check(legacy.*binding.modifiers == 0, "old INI defaults to no modifiers");
    }
    SaveHotkeys(old, legacy);
    check(old.GetLongValue("Hotkeys", "iScoutKey", 0) == -1, "unbound serializes as signed -1");
    for (auto invalid : {-1L, 8L, 999L}) {
        old.SetLongValue("Hotkeys", "iCommandModifiers", invalid);
        LoadHotkeys(old, legacy);
        check(legacy.commandModifiers == 0, "invalid INI modifiers fall back to none");
    }
    old.SetLongValue("Hotkeys", "iScoutModifiers", ctrl);
    LoadHotkeys(old, legacy);
    check(legacy.scoutModifiers == 0, "unbound INI entry cannot retain hidden modifiers");
    std::cout << "Keyboard chords: " << failures
              << " failures (capture, exact matching, rebinding, keyboard snapshots and INI "
                 "migration/roundtrip).\n";
    return failures ? 1 : 0;
}

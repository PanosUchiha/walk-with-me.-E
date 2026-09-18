#include "config.h"
#include "follower_dialogue.h"
#include <iostream>

int main()
{
    using Wayfarer::FollowerDialogueRank;
    int failures = 0;
    auto check = [&](bool ok, const char* reason) {
        if (!ok) {
            ++failures;
            std::cerr << reason << '\n';
        }
    };
    const Wayfarer::SettingsData defaults;
    check(!defaults.autoDiscover, "Fresh and missing-INI defaults require explicit enrollment");
    check(FollowerDialogueRank(false, false, false, false, true, true) == 0, "Unmanaged follower offers add");
    check(FollowerDialogueRank(false, true, false, false, true, true) == 1,
          "Automatic management hides both topics");
    check(FollowerDialogueRank(true, true, false, false, true, true) == 2,
          "Manual origin survives automatic eligibility");
    check(FollowerDialogueRank(true, false, false, false, false, true) == 2,
          "Dismissed or unloaded manual follower retains removal");
    check(FollowerDialogueRank(true, false, false, false, true, false) == 2,
          "Removal remains possible while globally disabled");
    check(FollowerDialogueRank(false, true, false, true, true, true) == 0,
          "Removal suppresses stale roster membership until deferred release");
    check(FollowerDialogueRank(false, false, true, false, true, true) == 1,
          "An API integration owns its own registration");
    check(FollowerDialogueRank(false, false, false, false, false, true) == -1,
          "Unrecruited NPC gets neither option");
    check(FollowerDialogueRank(false, false, false, false, true, false) == -1,
          "Disabled controller cannot offer an ineffective add");
    return failures ? 1 : 0;
}

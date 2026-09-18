#include "custom_followers.h"
#include "follower_eligibility.h"
#include "follower_settings.h"
#include <iostream>
#include <limits>

int main()
{
    using namespace Wayfarer;
    int failures = 0;
    auto check = [&](bool ok, const char* reason) {
        if (!ok) {
            ++failures;
            std::cerr << reason << '\n';
        }
    };
    check(KnownCustomFollower("018auri.esp", 0xD63), "Auri is custom even while in vanilla follower faction");
    check(KnownCustomFollower("dawnguard.esm", 0x2B6C), "Serana uses her own framework");
    check(!KnownCustomFollower("dawnguard.esm", 0x336A), "Other Dawnguard NPCs are not Serana");
    check(!KnownCustomFollower("skyrim.esm", 0x2B6C), "Base ID alone does not identify Serana");
    check(!UsesCustomFollowerAI("skyrim.esm", 0xB9982, true, true),
          "Vanilla hireling keeps ordinary discovery");
    check(UsesCustomFollowerAI("3dnpc.esp", 0x123, true, true),
          "Other modded followers also honor the custom toggle");
    check(!UsesCustomFollowerAI("3dnpc.esp", 0x123, false, true),
          "Mod origin alone does not recruit an actor");
    check(!UsesCustomFollowerAI("skyrim.esm", 0xA2C8E, true, false),
          "Inactive Lydia must not acquire custom AI through a leftover teammate flag");
    check(!UsesCustomFollowerAI("hearthfires.esm", 0x123, true, false),
          "Inactive Hearthfire housecarl must not acquire custom AI");
    check(!UsesCustomFollowerAI("housecarl-replacer.esp", 0x123, true, false),
          "Mod origin and teammate flag do not prove recruitment");
    check(UsesCustomFollowerAI("dawnguard.esm", 0x2B6C, true, false),
          "Serana retains her explicit independent-follower exception");

    FollowerRecruitmentState housecarl{true, false, false, true, 0, 0};
    auto eligible = [](const auto& state) { return RecruitmentRejection(state) == nullptr; };
    check(!eligible(housecarl),
          "NFF roster rejects inactive housecarls even with both stale teammate and follower faction state");
    check(RecruitmentRejection(housecarl, false) != nullptr,
          "Disabling teammate requirement cannot bypass NFF roster");
    housecarl.nffMember = true;
    check(eligible(housecarl), "Recruitment into an NFF slot makes the housecarl eligible");
    housecarl.waiting = 1;
    check(eligible(housecarl), "Waiting NFF followers remain roster members; movement guards enforce wait");
    housecarl.waiting = 0;
    housecarl.teammate = false;
    check(eligible(housecarl), "An active NFF slot remains authoritative if teammate state changes");
    housecarl.nffMember = false;
    housecarl.currentFollowerRank = -1;
    check(!eligible(housecarl), "NFF dismissal releases automatic and dialogue registrations");
    housecarl.teammate = true;
    check(!eligible(housecarl), "A later teammate flag cannot reacquire the dismissed housecarl");

    FollowerRecruitmentState vanilla{false, false, false, true, 0, 0};
    check(eligible(vanilla), "Recruited vanilla follower works without NFF");
    vanilla.currentFollowerRank = -1;
    check(!eligible(vanilla), "Negative faction rank rejects dismissed vanilla followers");
    check(RecruitmentRejection(vanilla, false) != nullptr,
          "Relaxing teammate requirement still requires active faction rank");
    vanilla.currentFollowerRank = 0;
    vanilla.teammate = false;
    check(!eligible(vanilla), "Faction evidence alone cannot enable Party/dialogue Add");
    check(RecruitmentRejection(vanilla, false) == nullptr,
          "Explicit legacy teammate override still works without NFF");

    FollowerRecruitmentState independent{true, false, true, true, -1, 0};
    check(eligible(independent), "Known independent companion need not be imported into NFF");
    independent.teammate = false;
    check(!eligible(independent), "Unrecruited independent companion is rejected");
    independent.teammate = true;
    independent.waiting = -1;
    check(!eligible(independent), "Dismissed Inigo cannot bypass the shared enrollment guard");
    independent.waiting = std::numeric_limits<float>::quiet_NaN();
    check(!eligible(independent), "Unknown recruitment state cannot enroll through dialogue or discovery");
    independent.waiting = 0;
    independent.knownIndependent = false;
    check(!eligible(independent),
          "Unknown private follower requires an integration, not an inferred NFF exemption");
    check(HasCustomFollowState(true, 0), "Recruited custom teammate is discoverable");
    check(!HasCustomFollowState(false, 0), "Unrecruited follower is not acquired");
    check(!HasCustomFollowState(true, -1), "Dismissed Inigo stays a teammate but must not be reacquired");
    check(HasCustomFollowState(true, 1) && HasCustomFollowState(true, 2),
          "Waiting and resting companions remain in the roster");
    check(!HasCustomFollowState(true, std::numeric_limits<float>::quiet_NaN()),
          "Unknown follow state is rejected");
    std::unordered_set<std::string> old;
    for (auto name : legacyCustomExclusions) {
        old.emplace(name);
    }
    check(IsLegacyCustomBlocklist(old), "Exact shipped defaults can migrate");
    old.erase("018auri.esp");
    check(!IsLegacyCustomBlocklist(old), "User-edited blocklist must survive");
    old.emplace("018auri.esp");
    old.emplace("myfollower.esp");
    check(!IsLegacyCustomBlocklist(old), "Additional user exclusions must survive");
    check(!IsLegacyCustomBlocklist({}), "Empty defaults need no migration");
    CSimpleIniA ini;
    ini.LoadData("[General]\nbAutoDiscover=false\n[Compatibility]\nbEnforceCustomFollowers="
                 "false\nsExcludedPlugins=myfollower.esp\n");
    check(MigrateFollowerSettings(ini) && !ini.GetBoolValue("General", "bAutoDiscover"),
          "Old settings migrate to explicit enrollment");
    check(!ini.GetBoolValue("Compatibility", "bEnforceCustomFollowers"),
          "Upgrade preserves custom enforcement preference");
    check(std::string(ini.GetValue("Compatibility", "sExcludedPlugins")) == "myfollower.esp",
          "Upgrade preserves exclusions");
    ini.SetBoolValue("General", "bAutoDiscover", true);
    std::string saved;
    ini.Save(saved);
    CSimpleIniA reloaded;
    reloaded.LoadData(saved.c_str());
    check(!MigrateFollowerSettings(reloaded) && reloaded.GetBoolValue("General", "bAutoDiscover"),
          "Opting into automatic enrollment survives reload");
    CSimpleIniA fresh;
    check(MigrateFollowerSettings(fresh) && !fresh.GetBoolValue("General", "bAutoDiscover"),
          "Missing enrollment settings require explicit additions");
    CSimpleIniA versionOne;
    versionOne.LoadData("[General]\nbAutoDiscover=true\niEnrollmentVersion=1\n");
    check(MigrateFollowerSettings(versionOne) && !versionOne.GetBoolValue("General", "bAutoDiscover") &&
              versionOne.GetLongValue("General", "iEnrollmentVersion") == 2,
          "Previous forced automatic migration is reversed once");
    return failures ? 1 : 0;
}

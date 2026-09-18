#include "companion_menu.h"
#include "companion_recovery.h"
#include "follower_dialogue.h"
#include "follower_eligibility.h"
#include "package_control.h"
#include "travel_space.h"
#include <iostream>
#include <limits>

int main()
{
    using namespace Wayfarer;
    int failures = 0;
    auto check = [&](bool ok, const char* why) {
        if (!ok) {
            ++failures;
            std::cerr << why << '\n';
        }
    };
    struct Cell
    {
        bool interior{};
        bool IsInteriorCell() const { return interior; }
    };
    struct Reference
    {
        Cell* cell{};
        const int* world{};
        Cell* GetParentCell() const { return cell; }
        const int* GetWorldspace() const { return world; }
    };
    Cell exteriorA, exteriorB, roomA{true}, roomB{true};
    int tamriel{}, otherWorld{};
    auto sameSpace = [&](Reference actor, Reference player) { return InSameTravelSpace(actor, player); };
    check(sameSpace({&exteriorA, &tamriel}, {&exteriorB, &tamriel}),
          "A nearby companion across an exterior cell boundary can be imported");
    check(sameSpace({&roomA, nullptr}, {&roomA, nullptr}), "Companions in the same interior can be imported");
    check(!sameSpace({&roomA, nullptr}, {&roomB, nullptr}), "Separate interiors remain isolated");
    check(!sameSpace({&roomA, &tamriel}, {&exteriorA, &tamriel}),
          "Interior/exterior boundaries remain isolated");
    check(!sameSpace({&exteriorA, &tamriel}, {&exteriorB, &otherWorld}),
          "Separate worldspaces remain isolated");
    check(!sameSpace({&exteriorA, nullptr}, {&exteriorB, nullptr}),
          "Missing worldspaces cannot join distinct cells");
    check(!sameSpace({}, {}) && !sameSpace({}, {&exteriorA, &tamriel}) &&
              !sameSpace({&exteriorA, &tamriel}, {}),
          "Missing cells never count as a shared area");
    const auto privateFollower = PackageControlFlags(false, false);
    for (const auto viewport :
         {std::pair{1280.0F, 720.0F}, std::pair{1920.0F, 1080.0F}, std::pair{3440.0F, 1440.0F}}) {
        const auto w = CompanionMenu::Width(viewport.first, viewport.second);
        const auto left = CompanionMenu::Action(0, w), right = CompanionMenu::Action(1, w);
        check(w < viewport.first && w * CompanionMenu::height < viewport.second,
              "Companion popup fits the viewport");
        check(left.x + left.w < right.x && right.x + right.w < w &&
                  right.y + right.h < w * CompanionMenu::height,
              "Companion actions are separate and within the panel");
        const auto cancel = CompanionMenu::Cancel(w);
        const auto recovery = CompanionMenu::Recovery(w);
        check(cancel.y > left.y + left.h && cancel.x > 0 && cancel.x + cancel.w < w &&
                  cancel.y + cancel.h < w * CompanionMenu::height,
              "Cancel is separate from actions and remains inside the panel");
        check(recovery.x + recovery.w < cancel.x && recovery.y > left.y + left.h,
              "Unstick cannot overlap Cancel or Travel together");
    }
    for (float rate : {30.0F, 60.0F, 144.0F}) {
        CompanionMenu::Motion motion;
        for (int i = 0; i < static_cast<int>(rate); ++i) {
            motion.Update(1 / rate);
        }
        check(motion.visibility == 1 && motion.Reveal(.18F) == 1,
              "All panel layers settle at each frame rate");
        motion.Close();
        for (int i = 0; i < static_cast<int>(rate * .25F); ++i) {
            const float before = motion.visibility;
            motion.Close();
            motion.Update(1 / rate);
            check(motion.visibility <= before, "Repeated cancel does not restart or brighten the exit");
        }
        check(motion.Finished() && motion.visibility == 0,
              "Exit finishes and can release the pause at each frame rate");
        motion = {};
        motion.Update(1 / rate);
        const float interrupted = motion.visibility;
        motion.Close();
        motion.Update(1 / rate);
        check(motion.visibility <= interrupted, "Cancel during entrance exits from the current opacity");
        motion = {};
        check(!motion.closing && !motion.Finished(), "Reopen resets completed exit");
    }
    CompanionPushRequest push;
    check(push.Request(42, 7, 100), "A targeted push can be queued");
    const auto firstTicket = push.ticket;
    check(!push.Request(43, 7, 101), "Repeated clicks cannot replace the targeted companion");
    check(push.Poll(7, 200, true, true) == RecallDispatch::Wait, "Push waits for menu and pause cleanup");
    check(!push.Consume(42, firstTicket, 7, 210, true), "Script cannot push before dispatch");
    check(push.Poll(7, 300, false, true) == RecallDispatch::Run, "Push dispatches once gameplay resumes");
    check(!push.Consume(43, firstTicket, 7, 310, true), "Different actor cannot consume the permission");
    check(!push.Consume(42, firstTicket + 1, 7, 310, true), "Stale ticket cannot consume the permission");
    check(push.Consume(42, firstTicket, 7, 310, true), "Correct script call consumes the permission");
    check(!push.Consume(42, firstTicket, 7, 311, true), "Duplicate script call cannot push twice");
    check(!push.Request(42, 7, 3309) && push.Request(42, 7, 3310), "Cooldown blocks rapid repeated pushes");
    check(push.Poll(8, 3400, false, true) == RecallDispatch::Cancel,
          "Loading another save cancels a queued push");
    push.Reset();
    check(push.Request(42, 8, 3500), "Push is available after reset");
    const auto newTicket = push.ticket;
    check(newTicket != firstTicket, "Reset does not reuse an old script ticket");
    check(push.Poll(8, 3510, false, true) == RecallDispatch::Run, "New save can dispatch its own push");
    check(!push.Consume(42, newTicket, 8, 3520, false),
          "Combat or unsafe state after dispatch blocks the push");
    check(!push.Consume(42, newTicket, 8, 3530, true), "Rejected script permission cannot be retried");
    push.Reset();
    push.Request(42, 8, 4000);
    push.Poll(8, 4010, false, true);
    check(push.Poll(8, 9001, false, true) == RecallDispatch::Cancel && !push.Busy(9001),
          "Missing script completion expires and releases the busy state");
    check(!push.Consume(42, push.ticket, 8, 9002, true), "Late scripts cannot push after timeout");
    push.Reset();
    push.Request(42, 8, 10000);
    check(push.Poll(8, 10001, false, false) == RecallDispatch::Cancel,
          "Unsafe targets cancel before dispatch");
    push.Reset();
    push.Request(42, 8, 11000);
    push.Poll(8, 11001, false, true);
    check(!push.Consume(42, push.ticket, 9, 11002, true),
          "Save change between dispatch and script prevents the push");
    const auto teammate = PackageControlFlags(false, true);
    const auto nff = PackageControlFlags(true, true);
    check(RecruitmentRejection({true, false, false, true, 0, 0}) != nullptr,
          "Popup support must not widen automatic enrollment of inactive NFF housecarls");
    check(RecruitmentRejection({false, false, false, true, -1, 0}) != nullptr,
          "Unknown private followers remain ineligible for automatic discovery");
    check(!PackageControlRejection(privateFollower, false, false, 0),
          "Explicit control supports a private follower with no public recruitment flags");
    check(!PackageControlRejection(teammate, false, true, 0),
          "Explicitly imported custom teammate can travel");
    check(PackageControlRejection(teammate, false, false, 0) != nullptr,
          "Losing recruitment releases control");
    check(!PackageControlRejection(nff, true, false, 0),
          "NFF's slot takes precedence over a changed teammate flag");
    check(PackageControlRejection(nff, false, true, 0) != nullptr,
          "NFF dismissal cannot fall back to stale teammate state");
    check(!PackageControlRejection(teammate, true, false, 0),
          "Joining NFF can promote a previously independent import");
    check(!PackageControlRejection(privateFollower, false, false, 1),
          "Wait state preserves registration; existing movement guards enforce waiting");
    for (const auto flags : {privateFollower, teammate, nff}) {
        check(ValidPackageControlFlags(flags), "Valid ownership state survives serialization validation");
        check(PackageControlRejection(flags, true, true, -1) != nullptr,
              "Dismissed custom state overrides an import");
        check(PackageControlRejection(flags, true, true, std::numeric_limits<float>::quiet_NaN()) != nullptr,
              "Unknown state rejects control");
        check(PackageControlRejection(flags, true, true, std::numeric_limits<float>::infinity()) != nullptr,
              "Infinite state rejects control");
    }
    for (const auto flags : {0U, 2U, 4U, 6U, 7U, 0xFFFFFFFFU}) {
        check(!ValidPackageControlFlags(flags), "Malformed saved ownership must be discarded");
        check(PackageControlRejection(flags, true, true, 0) != nullptr,
              "Malformed state cannot bypass validation through NFF");
    }
    check(FollowerDialogueRank(false, false, true, false, false, true) == 1,
          "Imported package ownership must suppress a duplicate dialogue Add");
    return failures ? 1 : 0;
}

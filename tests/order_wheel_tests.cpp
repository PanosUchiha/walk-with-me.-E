#include "gesture_playback.h"
#include "gesture_profile.h"
#include "hud_notice.h"
#include "order_wheel.h"
#include <iostream>
int main()
{
    using namespace Wayfarer;
    int failures = 0;
    auto check = [&](bool value, const char* what) {
        if (!value) {
            ++failures;
            std::cerr << what << '\n';
        }
    };
    // Seven wedges, clockwise from the top: Lead, Companion, Rear, Custom, Vanilla, Relax, Natural.
    check(Wheel::Direction(0, -1) == 1, "up = Lead");
    check(Wheel::Direction(.78F, -.62F) == 2, "upper right = Companion");
    check(Wheel::Direction(1, .2F) == 3, "right = Rear");
    check(Wheel::Direction(.45F, .9F) == 6, "lower right = Custom");
    check(Wheel::Direction(-.45F, .9F) == 5, "lower left = Vanilla");
    check(Wheel::Direction(-1, .2F) == 4, "left = Relax");
    check(Wheel::Direction(-.78F, -.62F) == 0, "upper left = Natural");
    for (int i = 0; i < Wheel::count; ++i) {
        check(Wheel::clockwise[Wheel::slots[i]] == i, "display-to-logical mapping");
        check(Wheel::Direction(std::cos(Wheel::Angle(i)), std::sin(Wheel::Angle(i))) == i,
              "each slice center selects its command");
    }
    check(Wheel::Mouse(0, 0, 100) == -1, "center cancels selection");
    check(Wheel::Mouse(101, 0, 100) == -1, "outside is not a choice");
    Wheel::Stick s;
    check(s.Update(.1F, .1F, 1) == 1, "drift ignored");
    check(s.Update(.78F, -.62F, 1) == 2, "stick selects companion");
    check(s.Update(0, 0, 2) == 2, "neutral retains choice");
    int focus = s.Update(1, .2F, 2);
    check(focus == 3, "rear selected");
    // The Rear/Custom boundary is at 38.6 degrees; the stick holds its choice for 8 more.
    check(s.Update(std::cos(41 * Wheel::pi / 180), std::sin(41 * Wheel::pi / 180), focus) == 3,
          "boundary jitter retained");
    check(s.Update(std::cos(50 * Wheel::pi / 180), std::sin(50 * Wheel::pi / 180), focus) == 6,
          "deliberate crossing selects Custom");
    HudNotice notice;
    notice.Update(1, 0);
    notice.Update(1, 4.9F);
    check(notice.Alpha() == 1, "five-second hold");
    notice.Update(1, 2.2F);
    check(notice.Alpha() == 0 && notice.Slide() == 80, "fade/slide completes");
    notice.Update(1, 0, true);
    check(notice.elapsed == 0, "repeated explicit order replays HUD");
    check(Wheel::Once(20, 3) == 1, "animation clamps instead of looping");
    HudNoticePresentation hud;
    hud.Update(1, 2, false, false);
    check(!hud.compact, "initial formation uses full badge");
    hud.Update(1, 2, true, false);
    check(hud.compact, "automatic temporary rest uses small fire");
    hud.Update(1, 2, true, false);
    check(hud.compact, "small presentation persists throughout notice");
    hud.Update(1, 2, false, false);
    check(hud.compact, "automatic resume uses small formation icon");
    hud.Update(1, 2, false, true);
    check(!hud.compact, "reselecting same formation uses full badge");
    hud.Update(1, 2, true, false);
    hud.Update(1, 3, false, true);
    check(!hud.compact, "selected formation while resting uses full badge");
    hud.Update(1, 4, false, true);
    hud.Update(1, 4, true, false);
    check(!hud.compact, "explicit Relax entering sandbox retains full badge");
    hud.Update(1, 1, false, true);
    hud.Update(1, 1, true, false);
    hud.Update(2, 1, false, false);
    check(!hud.compact, "save generation resets automatic notice presentation");
    hud.Update(2, 1, true, false);
    hud.Update(2, 1, true, false, true);
    check(!hud.compact, "HUD preview or re-enable resets presentation");
    using Playback = CommandGesture::Playback;
    using Action = Playback::Action;
    Playback gesture;
    gesture.Start();
    check(gesture.armType == 1, "command uses right-arm mask");
    check(gesture.Update(1, true, true, false) == Action::None, "wave continues during travel");
    check(gesture.Update(.5F, true, true, false) == Action::Stop, "wave requests stop after 1.5 seconds");
    check(gesture.owned, "stop event retains selector ownership");
    check(gesture.Update(.34F, true, false, false) == Action::None && gesture.owned,
          "selector survives blend-out even if active flag clears early");
    check(gesture.Update(.02F, true, false, false) == Action::Release && !gesture.owned,
          "selector released only after inactive blend-out");
    gesture.Start();
    check(gesture.Update(.01F, true, true, true) == Action::Stop,
          "combat/dialogue/settings interruption stops wave");
    check(gesture.Update(.5F, true, true, false) == Action::None && gesture.owned,
          "active graph keeps its animation selector");
    check(gesture.Update(.5F, true, true, false) == Action::RetryStop, "missed stop retried once");
    check(gesture.Update(1, true, true, false) == Action::Stalled && gesture.owned,
          "stalled graph retains mask instead of changing a playing animation");
    check(gesture.Update(1, true, true, false) == Action::None, "stalled graph does not spam events");
    check(gesture.Update(.01F, true, false, false) == Action::Release,
          "eventual graph exit releases reservation");
    gesture.Start();
    check(gesture.Update(.1F, false, true, true) == Action::Abandon && !gesture.owned,
          "another mod taking channel cancels ownership without a stop or reset");
    gesture.Start(0);
    gesture.Stop();
    check(gesture.armType == 0 && gesture.Update(.36F, true, false, false) == Action::Release,
          "legacy saved upper-body gesture cleans up with its original mask");
    for (int mode = 0; mode < 5; ++mode) {
        const auto profile = CommandGesture::ForMode(mode);
        const auto restored = CommandGesture::Decode(CommandGesture::Encode(profile));
        check(restored && restored->selector == profile.selector && restored->arm == profile.arm,
              "saved gesture restores exact selector and arm");
        gesture.Start(profile.arm, profile.duration);
        check(gesture.Update(1, true, true, false) == Action::None, "each signal plays through first second");
        check(gesture.Update(profile.duration - 1 + .001F, true, true, false) == Action::Stop,
              "each signal uses its own clip duration");
        check(gesture.Update(.36F, true, false, false) == Action::Release,
              "every signal retains blend-out cleanup");
    }
    check(CommandGesture::ForMode(1).selector == 20 && CommandGesture::ForMode(1).arm == 2,
          "Lead uses left-arm forward reach");
    check(CommandGesture::ForMode(3).selector == 10 && CommandGesture::ForMode(3).arm == 1,
          "Rear uses a distinct right-arm sweep");
    check(CommandGesture::ForMode(2).selector == 140 && CommandGesture::ForMode(4).selector == 140,
          "other modes retain known-good wave");
    check(CommandGesture::Decode(1)->arm == 0 && CommandGesture::Decode(2)->arm == 1,
          "both shipped co-save formats remain supported");
    check(!CommandGesture::Decode(0) && !CommandGesture::Decode(0xffffffff) &&
              !CommandGesture::Decode(0x10000U | (20U << 4) | 1),
          "unknown selectors and mismatched masks are rejected");
    const auto point = CommandGesture::SelectProfile(CommandGesture::ForMode(1), true, true, false);
    check(point.selector == 901 && point.arm == 2,
          "optional Lead point uses its own selector and left-arm mask");
    check(CommandGesture::SelectProfile(CommandGesture::ForMode(1), true, true, true).selector == 20,
          "first person retains existing reach");
    check(CommandGesture::SelectProfile(CommandGesture::ForMode(1), true, false, false).selector == 20,
          "missing point pack retains reach");
    check(CommandGesture::SelectProfile(CommandGesture::ForMode(1), false, true, false).selector == 20,
          "point toggle restores reach");
    check(CommandGesture::SelectProfile(CommandGesture::ForMode(3), true, true, false).selector == 10,
          "point pack leaves Rear unchanged");
    check(CommandGesture::SelectProfile(CommandGesture::ForMode(0), true, true, false).selector == 140,
          "point pack leaves wave unchanged");
    const auto savedPoint = CommandGesture::Decode(CommandGesture::Encode(point));
    check(savedPoint && savedPoint->selector == 901 && savedPoint->arm == 2,
          "point co-save restores exact channel ownership");
    check(!CommandGesture::Decode(CommandGesture::Encode(point) ^ 3), "wrong point arm rejected on load");
    gesture.Start(point.arm, point.duration);
    check(gesture.Update(1, true, true, false) == Action::None && gesture.armType == 2,
          "point retains selected arm mask until stop");
    check(gesture.Update(.81F, true, true, false) == Action::Stop,
          "point stops before its 2.167-second source clip repeats");
    check(gesture.Update(.36F, true, false, false) == Action::Release,
          "point clears selector only after blend-out");
    const auto invitation =
        CommandGesture::SelectCompanionProfile(2, CommandGesture::ForMode(2), true, true, false);
    check(invitation.selector == 902 && invitation.arm == 0,
          "Companion invitation selects both arms through UpperBody");
    check(CommandGesture::SelectCompanionProfile(2, CommandGesture::ForMode(2), true, true, true).selector ==
              140,
          "Companion retains first-person wave");
    check(
        CommandGesture::SelectCompanionProfile(2, CommandGesture::ForMode(2), true, false, false).selector ==
            140,
        "missing invitation assets retain wave");
    check(
        CommandGesture::SelectCompanionProfile(2, CommandGesture::ForMode(2), false, true, false).selector ==
            140,
        "disabling invitation restores wave");
    for (int mode : {0, 4}) {
        check(CommandGesture::SelectCompanionProfile(mode, CommandGesture::ForMode(mode), true, true, false)
                      .selector == 140,
              "other wave orders do not become invitations");
    }
    check(CommandGesture::SelectCompanionProfile(1, point, true, true, false).selector == 901,
          "Companion selection preserves Lead point");
    const auto restoredInvitation = CommandGesture::Decode(CommandGesture::Encode(invitation));
    check(restoredInvitation && restoredInvitation->selector == 902 && restoredInvitation->arm == 0,
          "invitation save cleanup restores exact ownership");
    check(!CommandGesture::Decode(CommandGesture::Encode(invitation) ^ 3),
          "wrong invitation arm rejected on load");
    check(CommandGesture::Decode(0x10000U | (902U << 4) | 1)->arm == 1,
          "previous one-arm invitation save remains readable");
    check(CommandGesture::Decode(0x10000U | (70U << 4) | 1)->selector == 70,
          "previous Rear save remains readable");
    check(CommandGesture::Encode(invitation) != 1, "two-arm invitation never serializes as the legacy wave");
    gesture.Start(invitation.arm, invitation.duration);
    check(gesture.Update(1, true, true, false) == Action::None,
          "invitation stays active during first second");
    check(gesture.Update(.81F, true, true, false) == Action::Stop,
          "invitation stops before source animation repeats");
    check(gesture.Update(.36F, true, false, false) == Action::Release,
          "invitation retains blend-out cleanup");
    for (int mode : {1, 2, 3}) {
        const auto fallback = CommandGesture::ForMode(mode);
        const auto selected = CommandGesture::SelectPrototypeProfile(mode, fallback, true, true, false);
        check(selected.selector == 910 + mode, "each prototype has a distinct selector");
        check(selected.arm == (mode == 1   ? 2
                               : mode == 2 ? 0
                                           : 1),
              "prototypes preserve the intended arm mask");
        const auto saved = CommandGesture::Decode(CommandGesture::Encode(selected));
        check(saved && saved->arm == selected.arm && saved->selector == selected.selector,
              "prototype co-save preserves exact ownership");
        check(!CommandGesture::Decode(CommandGesture::Encode(selected) ^ 3),
              "prototype cleanup rejects incorrect masks");
        for (int excluded = 0; excluded < 3; ++excluded) {
            const auto result = CommandGesture::SelectPrototypeProfile(mode, fallback, excluded != 0,
                                                                       excluded != 1, excluded == 2);
            check(result.selector == fallback.selector && result.duration == fallback.duration,
                  "missing, disabled and first-person prototypes preserve fallback and timing");
        }
        gesture.Start(selected.arm, selected.duration);
        check(gesture.Update(1, true, true, false) == Action::None &&
                  gesture.Update(1, true, true, false) == Action::None,
              "prototype completes both motions before stopping");
        check(gesture.Update(.36F, true, true, false) == Action::Stop && selected.duration < 2.4F,
              "prototype stops before the clip loops");
        check(gesture.Update(.36F, true, false, false) == Action::Release,
              "prototype retains its mask through blend-out");
    }
    for (int mode : {0, 4, 5}) {
        check(!CommandGesture::PrototypeForMode(mode), "other orders retain existing gestures");
    }
    for (int mode : {0, 1, 2, 3}) {
        const auto p = *CommandGesture::ApprovedForMode(mode), fallback = CommandGesture::ForMode(mode);
        check(p.selector == (mode ? 920 + mode : 924) && p.arm == (mode == 1 ? 2 : 3),
              "approved clips use locomotion-preserving overlays");
        check(CommandGesture::SelectApprovedProfile(mode, fallback, true, true, false).selector == p.selector,
              "shared reference selects when installed without a sex gate");
        for (int excluded = 0; excluded < 3; ++excluded) {
            check(CommandGesture::SelectApprovedProfile(mode, fallback, excluded != 0, excluded != 1,
                                                        excluded == 2)
                          .selector == fallback.selector,
                  "disabled, missing and first-person paths retain fallback");
        }
        const auto saved = CommandGesture::Decode(CommandGesture::Encode(p));
        check(saved && saved->arm == p.arm && saved->selector == p.selector,
              "approved reference ownership survives save/load");
        if (mode != 1) {
            auto old = p;
            old.arm = 0;
            const auto previous = CommandGesture::Decode(CommandGesture::Encode(old));
            check(previous && previous->arm == 0,
                  "previous upper-body ownership is restored with its original mask");
        }
        check(CommandGesture::ThirdPersonOnly(p) && CommandGesture::AuthoredGlance(p) == (mode != 1),
              "camera cleanup and authored glance match each reference");
        gesture.Start(p.arm, p.duration);
        gesture.Update(1, true, true, false);
        gesture.Update(1, true, true, false);
        if (mode == 0) {
            check(gesture.Update(.4F, true, true, false) == Action::None,
                  "Natural keeps playing its three-second reference");
        }
        check(gesture.Update(mode == 0 ? .56F : .36F, true, true, false) == Action::Stop,
              "shared clip stops before looping");
    }
    return failures ? 1 : 0;
}

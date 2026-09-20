#include "command_gesture.h"
#include "SKSEMenuFramework.h"
#include "config.h"
#include "gesture_head_tracking.h"
#include "gesture_playback.h"
#include "gesture_profile.h"
#include <fstream>

namespace Wayfarer::CommandGesture
{
    namespace
    {
        Playback playback;
        Profile gesture, requestedGesture;
        int requestedMode{};
        bool pending{};
        std::uint32_t restoreSelector{};
        float cooldown{}, wait{};

        RE::TESGlobal* Trigger()
        {
            auto* data = RE::TESDataHandler::GetSingleton();
            return data ? data->LookupForm<RE::TESGlobal>(0x802, "FirstPersonInteractions.esp") : nullptr;
        }
        bool SignalAssetsAvailable(Profile profile)
        {
            if (profile.arm == 3) {

                static const bool installed = []() {
                    std::ifstream stream("Data/meshes/actors/character/behaviors/0_Master.hkx",
                                         std::ios::binary);
                    if (!stream) {
                        logger::warn("[Gesture] Dedicated walking layer unavailable: cannot read "
                                     "0_Master.hkx; approved body gestures will use legacy fallback");
                        return false;
                    }
                    const std::string bytes((std::istreambuf_iterator<char>(stream)),
                                            std::istreambuf_iterator<char>());
                    const bool found = bytes.find("WWM_CommandUpperBody_Locomotion") != std::string::npos;
                    if (found) {
                        logger::info(
                            "[Gesture] Dedicated walking layer detected; approved body gestures use state 3");
                    }
                    else {
                        logger::warn("[Gesture] Dedicated walking layer missing from 0_Master.hkx; approved "
                                     "body gestures will use legacy fallback. Regenerate with Walk With Me's "
                                     "GPMA source patch enabled");
                    }
                    return found;
                }();
                if (!installed) {
                    return false;
                }
            }
            const auto root =
                std::filesystem::path("Data/meshes/OpenAnimationReplacer/Walk With Me Signals") /
                profile.folder;
            const bool thirdPerson =
                std::filesystem::exists(root / "config.json") &&
                std::filesystem::exists(root / "Actors/Character/Animations/GPMAOffsetAnimation.hkx");

            const bool bundledPrototype = ThirdPersonOnly(profile);
            return thirdPerson &&
                   (bundledPrototype ||
                    std::filesystem::exists(
                        root / "Actors/Character/_1stPerson/Animations/GPMAOffsetAnimation_IC_.hkx"));
        }
        bool Busy(RE::PlayerCharacter& p)
        {
            const auto* state = p.AsActorState();
            if (!state) {
                return true;
            }
            bool driven = false;
            p.GetGraphVariableBool("bAnimationDriven", driven);
            auto* ui = RE::UI::GetSingleton();
            return p.IsDead() || p.IsInCombat() || state->IsWeaponDrawn() || p.GetCurrentScene() ||
                   state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal || p.IsOnMount() ||
                   state->IsSwimming() || driven || (ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME));
        }
        bool ChannelOurs(RE::PlayerCharacter& p)
        {
            auto* trigger = Trigger();
            int type = -1;
            return playback.owned && trigger && trigger->value == gesture.selector &&
                   p.GetGraphVariableInt("iGPMAOffsetType", type) && type == playback.armType;
        }
        void Release(RE::PlayerCharacter& p)
        {

            Trigger()->value = 0;
            if (playback.armType != 0) {
                p.SetGraphVariableInt("iGPMAOffsetType", 0);
            }
            playback = {};
            logger::debug("[Gesture] offset released after blend-out");
        }
    } // namespace

    bool Available()
    {
        auto* p = RE::PlayerCharacter::GetSingleton();
        bool active = false;
        return GetModuleHandleA("OpenAnimationReplacer.dll") && p && Trigger() &&
               p->GetGraphVariableBool("bOffsetGPMA", active) &&
               std::filesystem::exists("Data/meshes/OpenAnimationReplacer/FP Interact/Wave "
                                       "Children/Actors/Character/Animations/GPMAOffsetAnimation.hkx");
    }
    void Request(FormationMode mode)
    {
        if (mode == FormationMode::kVanilla) {
            pending = false;
            return;
        }
        const auto& settings = Settings::GetSingleton().Get();
        if (settings.enabled && settings.commandGestures && !playback.owned && cooldown <= 0) {
            requestedMode = static_cast<int>(mode);
            requestedGesture = ForMode(requestedMode);
            pending = true;
            wait = 0;
        }
    }
    void Reset()
    {

        auto* p = RE::PlayerCharacter::GetSingleton();
        if (p && p->Is3DLoaded() && ChannelOurs(*p)) {
            p->NotifyAnimationGraph("OffsetGPMAStop");
            Release(*p);
        }
        HeadTracking::Reset(p);
        playback = {};
        pending = false;
        cooldown = wait = 0;
    }
    std::uint32_t SavedOwnership()
    {
        auto* p = RE::PlayerCharacter::GetSingleton();
        return p && ChannelOurs(*p) ? Encode(gesture) : 0U;
    }
    void RestoreSelector(std::uint32_t saved)
    {
        restoreSelector = Decode(saved) ? saved : 0;
    }
    void Update(float dt)
    {
        if (!std::isfinite(dt) || dt <= 0 || dt > 1) {
            return;
        }
        auto* p = RE::PlayerCharacter::GetSingleton();
        auto* ui = RE::UI::GetSingleton();

        if (!p || !p->Is3DLoaded()) {
            HeadTracking::Reset(p);
            pending = false;
            return;
        }
        if (restoreSelector) {
            gesture = *Decode(restoreSelector);
            restoreSelector = 0;
            playback.Start(gesture.arm, gesture.duration);
            if (ChannelOurs(*p)) {
                playback.Stop();
                p->NotifyAnimationGraph("OffsetGPMAStop");
            }
            else {
                playback = {};
            }
        }
        if (ui && ui->GameIsPaused()) {
            return;
        }
        cooldown = std::max(0.0F, cooldown - dt);
        if (playback.owned) {
            const bool ours = ChannelOurs(*p);
            bool active = true;
            p->GetGraphVariableBool("bOffsetGPMA", active);
            const auto& settings = Settings::GetSingleton().Get();
            const auto* camera = RE::PlayerCamera::GetSingleton();
            const bool prototypeCameraExit =
                ThirdPersonOnly(gesture) && (!camera || !camera->IsInThirdPerson());
            switch (playback.Update(dt, ours, active,
                                    Busy(*p) || !settings.commandGestures || !settings.enabled ||
                                        prototypeCameraExit ||
                                        ((gesture.selector == LeadPoint().selector ||
                                          gesture.selector == 911 || gesture.selector == 921) &&
                                         !settings.pointingSignal) ||
                                        ((gesture.selector == CompanionInvite().selector ||
                                          gesture.selector == 912 || gesture.selector == 922) &&
                                         !settings.companionSignal))) {
            case Playback::Action::Stop:
            case Playback::Action::RetryStop:
                p->NotifyAnimationGraph("OffsetGPMAStop");
                logger::debug("[Gesture] stopping command signal; waiting for blend-out");
                break;
            case Playback::Action::Release:
                Release(*p);
                break;
            case Playback::Action::Abandon:

                logger::debug("[Gesture] animation channel handed to another interaction");
                break;
            case Playback::Action::Stalled:
                logger::warn(
                    "[Gesture] offset still active after stop; retaining selector until graph exits");
                break;
            default:
                break;
            }
        }
        HeadTracking::Update(*p, dt, playback.owned && !playback.stopping && ChannelOurs(*p) && !Busy(*p));
        if (!pending) {
            return;
        }
        wait += dt;
        const auto& settings = Settings::GetSingleton().Get();
        if (wait > 2 || !settings.enabled || !settings.commandGestures) {
            pending = false;
            return;
        }
        if (SKSEMenuFramework::IsAnyBlockingWindowOpened()) {
            return;
        }
        pending = false;
        auto* trigger = Trigger();
        bool active = true;
        int type = -1;
        if (playback.owned || !Available() || Busy(*p) || !trigger || trigger->value != 0 ||
            !p->GetGraphVariableBool("bOffsetGPMA", active) || active ||
            !p->GetGraphVariableInt("iGPMAOffsetType", type) || type != 0) {
            logger::debug("[Gesture] skipped: unavailable or animation channel busy");
            return;
        }
        gesture = requestedGesture;
        const auto path = std::filesystem::path("Data/meshes/OpenAnimationReplacer/FP Interact") /
                          gesture.folder / "Actors/Character";
        if (!std::filesystem::exists(path / "Animations/GPMAOffsetAnimation.hkx") ||
            !std::filesystem::exists(path / "_1stPerson/Animations/GPMAOffsetAnimation_IC_.hkx")) {
            gesture = {};
        }
        auto* camera = RE::PlayerCamera::GetSingleton();
        const bool firstPerson = !camera || !camera->IsInThirdPerson();
        gesture =
            SelectProfile(gesture, settings.pointingSignal, SignalAssetsAvailable(LeadPoint()), firstPerson);
        gesture = SelectCompanionProfile(requestedMode, gesture, settings.companionSignal,
                                         SignalAssetsAvailable(CompanionInvite()), firstPerson);
        const auto prototype = PrototypeForMode(requestedMode);
        const bool prototypeEnabled = requestedMode == 1   ? settings.pointingSignal
                                      : requestedMode == 2 ? settings.companionSignal
                                                           : true;
        gesture = SelectPrototypeProfile(requestedMode, gesture, prototypeEnabled,
                                         prototype && SignalAssetsAvailable(*prototype), firstPerson);
        const auto approvedProfile = ApprovedForMode(requestedMode);
        gesture =
            SelectApprovedProfile(requestedMode, gesture, prototypeEnabled,
                                  approvedProfile && SignalAssetsAvailable(*approvedProfile), firstPerson);

        if (!p->SetGraphVariableInt("iGPMAOffsetType", gesture.arm)) {
            return;
        }
        trigger->value = static_cast<float>(gesture.selector);
        playback.Start(gesture.arm, gesture.duration);
        cooldown = 3;
        if (!p->NotifyAnimationGraph("OffsetGPMA")) {
            playback.Stop();
            p->NotifyAnimationGraph("OffsetGPMAStop");
            logger::warn("[Gesture] offset event rejected");
        }
        else {
            HeadTracking::Start(*p, gesture.duration, AuthoredGlance(gesture));
            logger::info("[Gesture] {} signal started (selector={}, arm={})", gesture.label, gesture.selector,
                         gesture.arm);
        }
    }
} // namespace Wayfarer::CommandGesture

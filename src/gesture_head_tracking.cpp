#include "gesture_head_tracking.h"
#include "TDMHeadTrackingAPI.h"
#include "config.h"
#include "formation_controller.h"
#include "gesture_awareness.h"

namespace Wayfarer::CommandGesture::HeadTracking
{
    namespace
    {
        struct GraphFlag
        {
            const char* name;
            bool previous{}, written{}, valid{};
            bool Capture(RE::PlayerCharacter& p, bool value)
            {
                valid = p.GetGraphVariableBool(name, previous);
                written = value;
                return valid;
            }
            void Apply(RE::PlayerCharacter& p) const
            {
                if (valid) {
                    p.SetGraphVariableBool(name, written);
                }
            }
            void Restore(RE::PlayerCharacter& p) const
            {
                bool current{};
                if (valid && p.GetGraphVariableBool(name, current) && current == written) {
                    p.SetGraphVariableBool(name, previous);
                }
            }
        };
        struct State
        {
            bool active{}, tdmOwned{}, previousTracking{}, authored{};
            float degrees{}, elapsed{}, duration{};
            RE::AIProcess* process{};
            // Held by reference, not by address. A raw pointer kept only for the
            // identity test still reads as "same skeleton" if the old 3D is freed
            // and a new node is allocated over it.
            RE::NiPointer<RE::NiAVObject> graph;
            TDM::Interface* tdm{};
            GraphFlag npc{"IsNPC"}, spine{"bHeadTrackSpine"};
        } state;
        std::uint32_t restoreState{};

        bool ThirdPerson(RE::PlayerCharacter& p)
        {
            auto* camera = RE::PlayerCamera::GetSingleton();
            if (!camera || !camera->IsInThirdPerson() || !camera->cameraRoot) {
                return false;
            }

            return camera->cameraRoot->world.translate.GetDistance(p.GetLookingAtLocation()) > 40.0F;
        }
        bool OtherTarget(const RE::AIProcess& process)
        {
            if (!process.high) {
                return true;
            }
            using Type = RE::HighProcessData::HEAD_TRACK_TYPE;
            for (auto type : {Type::kScript, Type::kCombat, Type::kDialogue, Type::kProcedure}) {
                if (process.high->headTracked[type]) {
                    return true;
                }
            }
            return false;
        }
        void RestoreLoadedState(RE::PlayerCharacter& p)
        {
            if (!restoreState) {
                return;
            }
            p.AsActorState()->actorState2.headTracking = (restoreState & 2) != 0;
            if (restoreState & 16) {
                p.SetGraphVariableBool("IsNPC", (restoreState & 4) != 0);
            }
            if (restoreState & 32) {
                p.SetGraphVariableBool("bHeadTrackSpine", (restoreState & 8) != 0);
            }
            restoreState = 0;
        }
    } // namespace

    void Reset(RE::PlayerCharacter* p)
    {
        if (state.active && p && p->GetActorRuntimeData().currentProcess == state.process) {
            if (p->Is3DLoaded() && p->Get3D() == state.graph.get()) {
                state.npc.Restore(*p);
                state.spine.Restore(*p);
            }
            else if (!p->Is3DLoaded()) {

                restoreState = SavedOwnership();
            }
            if (p->AsActorState()->actorState2.headTracking == !state.authored) {
                p->AsActorState()->actorState2.headTracking = state.previousTracking;
            }
        }
        if (state.tdmOwned && state.tdm &&
            state.tdm->GetDisableHeadtrackingOwner() == SKSE::GetPluginHandle()) {
            state.tdm->ReleaseDisableHeadtracking(SKSE::GetPluginHandle());
        }
        state = {};
    }

    std::uint32_t SavedOwnership()
    {
        if (!state.active) {
            return restoreState;
        }
        return 0x57480001U | (state.previousTracking ? 2U : 0U) | (state.npc.previous ? 4U : 0U) |
               (state.spine.previous ? 8U : 0U) | (state.npc.valid ? 16U : 0U) |
               (state.spine.valid ? 32U : 0U);
    }
    void RestoreOwnership(std::uint32_t saved)
    {
        restoreState = (saved & 0xFFFFFFC1U) == 0x57480001U ? saved : 0;
    }

    void Start(RE::PlayerCharacter& p, float duration, bool authored)
    {
        Reset(&p);
        RestoreLoadedState(p);
        const auto& settings = Settings::GetSingleton().Get();
        if ((!authored && !settings.gestureAwareness) || !ThirdPerson(p)) {
            return;
        }
        auto* process = p.GetActorRuntimeData().currentProcess;
        if (!process || OtherTarget(*process)) {
            return;
        }
        const float degrees = GlanceDegrees(FormationController::GetSingleton().GestureFollowerBearings(p),
                                            settings.preferredSide);
        if (!authored && std::abs(degrees) < .5F) {
            return;
        }
        state.tdm = TDM::Get();
        if (GetModuleHandleW(L"TrueDirectionalMovement.dll") && !state.tdm) {
            return;
        }
        if (state.tdm) {
            if (state.tdm->GetTargetLockState()) {
                return;
            }
            const auto result = state.tdm->RequestDisableHeadtracking(SKSE::GetPluginHandle());
            if (result != TDM::Result::OK && result != TDM::Result::AlreadyGiven) {
                return;
            }
            state.tdmOwned = true;
        }
        state.process = process;
        state.graph.reset(p.Get3D());
        state.degrees = degrees;
        state.duration = duration;
        state.authored = authored;
        state.previousTracking = p.AsActorState()->actorState2.headTracking;
        state.npc.Capture(p, !authored);
        state.spine.Capture(p, false);
        state.active = true;
        logger::debug("[Gesture] follower glance requested: {:.1f} degrees", degrees);
    }

    void Update(RE::PlayerCharacter& p, float dt, bool gestureActive)
    {
        RestoreLoadedState(p);
        if (!state.active) {
            return;
        }
        const auto& settings = Settings::GetSingleton().Get();
        auto* process = p.GetActorRuntimeData().currentProcess;
        if (!gestureActive || !settings.enabled || (!state.authored && !settings.gestureAwareness) ||
            !settings.commandGestures || !ThirdPerson(p) || process != state.process ||
            p.Get3D() != state.graph.get() || !process || OtherTarget(*process) ||
            (state.tdm && (state.tdm->GetTargetLockState() ||
                           state.tdm->GetDisableHeadtrackingOwner() != SKSE::GetPluginHandle()))) {
            Reset(&p);
            return;
        }
        state.elapsed += dt;
        if (state.elapsed >= state.duration) {
            Reset(&p);
            return;
        }
        if (state.authored) {
            state.npc.Apply(p);
            state.spine.Apply(p);
            p.AsActorState()->actorState2.headTracking = false;
            return;
        }
        const float yaw =
            p.GetAngleZ() + state.degrees * .01745329252F * GlanceWeight(state.elapsed, state.duration);
        auto target = p.GetLookingAtLocation();
        target.x += std::sin(yaw) * 500.0F;
        target.y += std::cos(yaw) * 500.0F;
        state.npc.Apply(p);
        state.spine.Apply(p);
        p.AsActorState()->actorState2.headTracking = true;
        process->SetHeadtrackTarget(&p, target);
    }
} // namespace Wayfarer::CommandGesture::HeadTracking

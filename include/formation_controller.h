#pragma once
#include "party_life.h"
#include "party_orders.h"
#include "rest_recovery.h"

#include "companion_recovery.h"
#include "dialogue_guard.h"
#include "follower_catchup.h"
#include "gesture_awareness.h"
#include "individual_travel.h"
#include "menu_ui.h"
#include "party_recall.h"
#include "rest_activities.h"
#include "rest_pose.h"
#include "role_assignment.h"
#include "social_planner.h"
#include "tether_approach.h"
#include "travel_banter.h"
#include "travel_navigation.h"
#include "victory_encounter.h"
#include "victory_reaction.h"

namespace Wayfarer
{
    class FormationController final
    {
      public:
        static FormationController& GetSingleton();

        void Initialize();
        void Update(float a_deltaSeconds);
        void UpdateHandHolding(float dt);
        std::int32_t GetTetherHandoffTicket(std::uint32_t slot);
        void ConfirmTetherHandoff(std::uint32_t slot, std::int32_t ticket);
        void ResetForGameLoad();

        void SetEnabled(bool a_enabled, bool a_notify = true);
        [[nodiscard]] bool IsEnabled() const noexcept;
        void SetMode(FormationMode a_mode, bool a_notify = true);
        void CycleMode();
        [[nodiscard]] FormationMode GetMode() const noexcept;
        void ReloadSettings(bool a_notify = true);
        bool ApplyPreferences(const SettingsData& a_settings);
        void GiveOrder(PartyOrder a_order);
        [[nodiscard]] PartyView Snapshot();
        [[nodiscard]] std::vector<CommandGesture::FollowerBearing> GestureFollowerBearings(
            RE::PlayerCharacter& player);
        static void RegisterSerialization();

        [[nodiscard]] bool RegisterFollower(RE::Actor* a_actor, int a_preferredSlot = -1);
        [[nodiscard]] bool RegisterFollower(RE::FormID a_formID, int a_preferredSlot = -1);
        [[nodiscard]] bool UnregisterFollower(RE::FormID a_formID);
        [[nodiscard]] bool ExcludeFollower(RE::FormID a_formID);
        [[nodiscard]] bool IncludeFollower(RE::FormID a_formID);
        [[nodiscard]] bool IsManaged(RE::FormID a_formID) const;
        void SetDialogueManagement(RE::Actor* actor, bool add);
        void SetPackageControl(RE::FormID id, bool take, std::uint64_t expectedGeneration);
        void RequestPartyRecall(std::uint64_t expectedGeneration);
        void RequestCompanionPush(RE::FormID id, std::uint64_t expectedGeneration);
        bool ConsumeCompanionPush(RE::Actor* actor, std::int32_t ticket);
        void ToggleTemporaryRelaxation(std::uint64_t expectedGeneration);
        [[nodiscard]] std::uint32_t GetManagedCount() const noexcept;
        [[nodiscard]] RE::Actor* GetSlotFollower(std::uint32_t a_slot);
        [[nodiscard]] RE::TESObjectREFR* GetSlotMarker(std::uint32_t a_slot);
        RE::Actor* GetSocialTarget(std::uint32_t slot);
        RE::TESIdleForm* ConsumeSocialIdle(std::uint32_t slot);
        void ReportSocialIdle(RE::Actor* actor, bool accepted);
        bool ShouldSuspendForDialogue();
        void ReportAliasSync(bool busy);
        RE::TESObjectREFR* GetRestSeat(std::uint32_t slot);
        RE::TESObjectREFR* GetGatherMarker(std::uint32_t slot);
        RE::TESIdleForm* ConsumeRestIdle(std::uint32_t slot);
        void ReportRestIdle(std::uint32_t slot, RE::Actor* actor, RE::TESIdleForm* idle, bool accepted);
        bool HasRestActivities() const
        {
            std::lock_guard lock(stateMutex);
            return enabled && sandbox.active;
        }
        bool HasSocialGroup() const
        {
            std::lock_guard lock(stateMutex);
            return enabled && sandbox.active && social.count > 1;
        }

      private:
        static bool IsCustomFollower(RE::Actor& actor);
        bool IsDialogueFollower(RE::Actor& actor) const;
        bool IsRegisteredFollower(RE::FormID id) const;
        void SyncFollowerDialogue(RE::Actor& actor);
        void UpdateFollowerDialogue(float dt);
        const char* PackageControlLoss(RE::Actor& actor, std::uint32_t flags) const;
        const char* PackageControlStartRejection(RE::Actor& actor) const;
        void UpdatePackageControl();
        std::unordered_map<RE::FormID, std::uint32_t> packageRegistrations;
        std::unordered_set<RE::FormID> dialogueRegistrations;
        float dialogueRosterTimer{};
        struct ManagedFollower
        {
            RE::ActorHandle handle;
            int slot{-1};
            int role{-1};
            FormationSlot roleOffset{};
            float roleSetback{};
            bool haveRoleOffset{};
            bool travelActive{false};
            bool markerLogged{false};
            bool recoveryReleasePending{};
            HandHolding::ArrivalRadiusLease handArrivalRadius;
            std::int32_t requestedHandTicket{};
            TravelGoalState goal;
            IndividualTravel individual;
            FollowerCatchup catchup;
            ObstacleAvoidance avoidance;
            float speedScale{1.0F};
            bool distantCatchup{}, sandboxActive{};
            float sandboxRadius{}, sandboxRefresh{};
            bool anchorSet{};
            bool restAnchorZSet{};
            float restAnchorZ{};
            bool socialHold{};
            int socialPhase{};
            int mealsState{-1};
            RestActivity activity{RestActivity::Stand};
            bool restIdleRequested{};
            RE::ActorHandle patron;
            float patronTimer{}, patronCooldown{15};
            bool patronGesture{};
            RestPose restPose;
            RestRecovery restRecovery;
            ActivitySearch activitySearch;
            RE::ObjectRefHandle activityTarget;
            float activityWalkTime{20};
            bool awarenessActive{}, awarenessWalking{}, awarenessTried{};
            float awarenessTime{};
            int awarenessPreviousPhase{};
        };

        FormationController() = default;

        void ScanFollowers(RE::PlayerCharacter* a_player);
        void InitializeVictoryEvents();
        void UpdateVictoryCelebration(RE::PlayerCharacter& player, float dt);
        void ResetVictoryCelebration(bool stopAnimations = true);
        void UpdateVictoryCleanup();
        std::string VictoryActorRejection(RE::Actor& actor, RE::PlayerCharacter& player, bool starting) const;
        void UpdateRestRecovery(ManagedFollower& follower, RE::Actor& actor, RE::TESObjectREFR& anchor,
                                float dt);
        std::optional<RE::NiPoint3> FindRestActivity(ManagedFollower& follower, RE::Actor& actor,
                                                     RE::TESObjectREFR& anchor);
        RE::TESObjectREFR* PrepareAuxMarker(ManagedFollower& follower, RE::Actor& actor, RE::NiPoint3 point,
                                            bool move = false);
        void UpdateConversationAwareness(RE::PlayerCharacter& player, float dt);
        void EndConversationAwareness();
        void UpdateLookout(RE::PlayerCharacter& player, float dt);
        void UpdateTravelRoles(RE::PlayerCharacter& player, float dt);
        void UpdateTravelBanter(RE::PlayerCharacter& player, float dt);
        void UpdateBanterMotion(RE::PlayerCharacter& player);
        void UpdateFollowerCatchup(RE::PlayerCharacter& player, float dt);
        std::vector<RE::Actor*> RecallCandidates() const;
        const char* RecallPlayerRejection(RE::PlayerCharacter& player) const;
        bool RecallActorAllowed(RE::Actor& actor) const;
        void RecallParty(RE::PlayerCharacter& player);
        std::optional<RE::NiPoint3> RecallLanding(RE::PlayerCharacter& player, RE::FormID id, int slot,
                                                  const std::vector<RE::NiPoint3>& reserved = {}) const;
        PartyRecallRequest recallRequest;
        CompanionPushRequest companionPush;
        const char* CompanionPushRejection(RE::Actor& actor) const;
        void UpdateCompanionPush();
        void RestoreTravelBanter();
        bool IsWalkingBanterScene(const RE::BGSScene* scene) const;
        [[nodiscard]] static bool IsNFFFollower(RE::Actor& a_actor);
        [[nodiscard]] static const char* RecruitmentRejectionFor(RE::Actor& actor,
                                                                 bool requireTeammate = true);
        [[nodiscard]] const char* CandidateRejection(RE::Actor& a_actor) const;
        [[nodiscard]] std::string UsabilityRejection(RE::Actor& a_actor, RE::PlayerCharacter& a_player,
                                                     bool inspectBanterScene = false,
                                                     bool allowDrawnWeapon = false) const;
        [[nodiscard]] const char* GlobalRejection(RE::PlayerCharacter& a_player) const;
        [[nodiscard]] bool IsAutomaticCandidate(RE::Actor& a_actor) const;
        [[nodiscard]] bool IsActorUsable(RE::Actor& a_actor, RE::PlayerCharacter& a_player) const;
        [[nodiscard]] bool IsGlobalTravelStateSafe(RE::PlayerCharacter& a_player) const;
        [[nodiscard]] int FindAvailableSlot(int a_preferredSlot = -1) const;
        void ApplyTravel(ManagedFollower& a_follower, RE::Actor& a_actor, RE::PlayerCharacter& a_player,
                         float a_dt);
        bool SetHandApproachRadius(ManagedFollower& follower, bool active);
        void ApplySandbox(ManagedFollower& follower, RE::Actor& actor, RE::PlayerCharacter& player, float dt);
        void SetSandboxFlag(bool active);
        void SetSocialFlag(int slot, int phase);
        void UpdatePatron(ManagedFollower& follower, RE::Actor& actor, float dt);
        void SetPoseFlag(int slot, int kind);
        void UpdateRestPose(ManagedFollower& follower, RE::Actor& actor, float dt);
        void EndRestPose(ManagedFollower& follower, RE::Actor* actor);
        RE::TESObjectREFR* EnsureRestSeat(ManagedFollower& follower, RE::Actor& actor, RE::NiPoint3 point);
        bool IsRelaxing() const { return mode == FormationMode::kSandbox; }
        bool SocialEligible(RE::Actor& actor) const;
        void UpdateSocial(float dt);
        [[nodiscard]] RE::TESObjectREFR* EnsureMarker(ManagedFollower& a_follower, RE::Actor& a_anchor);
        [[nodiscard]] RE::TESQuest* EnsureQuest();
        void WakeQuest();
        [[nodiscard]] static RE::BGSRefAlias* GetRefAlias(RE::TESQuest* a_quest, std::uint32_t a_aliasID);
        void ReleaseTravel(ManagedFollower& a_follower, RE::Actor* a_actor, bool tetherHandoff = false);
        void ReleaseAll();
        void RemoveManaged(RE::FormID a_formID);
        void LogDiagnostic(RE::FormID a_formID, const char* a_name, const char* a_reason);
        void LogGlobalDiagnostic(const char* a_reason);
        void LogScanSummary(std::size_t a_high, std::size_t a_teammates, std::size_t a_inFaction);
        static void Notify(std::string_view a_message);

        std::unordered_map<RE::FormID, ManagedFollower> managed;
        Victory::Encounter victoryEncounter;
        std::unordered_map<RE::FormID, RE::ActorHandle> victoryEnemies;
        struct VictoryReaction
        {
            RE::ActorHandle actor;
            RE::FormID actorID{};
            RE::TESIdleForm* idle{};
            float delay{}, elapsed{};
            float duration{Victory::reactionSeconds};
            bool started{};
            std::uint32_t variation{};
            float nextAttempt{};
            std::uint64_t queuedAtAliasSync{};
            Victory::Equipment equipment{Victory::Equipment::Unsupported};
            std::string lastReason;
        };
        std::vector<VictoryReaction> victoryReactions;
        std::vector<VictoryReaction> victoryCleanup;
        bool aliasSyncBusy{true};
        std::uint64_t completedAliasSyncs{};
        float victoryScanTimer{};
        std::uint32_t victorySequence{};
        PartyOrder order{PartyOrder::kTravel};
        std::uint64_t generation{};
        std::unordered_map<RE::FormID, int> manualRegistrations;
        std::unordered_set<RE::FormID> explicitExclusions;
        std::unordered_map<RE::FormID, std::string> diagnosticReasons;
        std::string lastGlobalReason;
        std::string lastScanSummary;
        mutable std::recursive_mutex stateMutex;
        TravelIntent intent;
        SandboxState sandbox;
        SocialPlanner social;
        DialogueGuard dialogueGuard;
        RegroupState regroup;
        LookoutState lookout;
        RE::FormID conversationSpeaker{};
        float awarenessUpdateTimer{};
        bool restoredRelax{};
        bool restoredVanilla{};
        std::unordered_map<RE::FormID, int> restoredRelaxSlots;
        TravelNavigation navigation;
        std::array<RE::ObjectRefHandle, PARTY_CAPACITY> markerPool{};
        std::array<RE::ObjectRefHandle, PARTY_CAPACITY> seatPool{};
        std::array<RE::ObjectRefHandle, PARTY_CAPACITY> gatherPool{};
        RE::TESObjectCELL* lastCell{nullptr};
        RE::TESWorldSpace* lastWorld{nullptr};
        RE::NiPoint3 lastPlayerPosition{};
        bool lastInterior{false};
        bool havePlayerPosition{false};
        float scanTimer{0.0F};
        float updateTimer{0.0F};
        float roleTimer{1.0F}, roleCooldown{};
        struct BanterSceneLease
        {
            RE::BGSScene* scene{};
            RE::FormID first{}, second{};
            std::vector<RE::BGSSceneAction*> facingActions;
            std::vector<std::uint32_t> exclusiveActors;
        };
        std::vector<BanterSceneLease> banterScenes;
        std::vector<RE::TESTopicInfo*> banterTopics;
        std::vector<TravelBanterPair> travelBanterPairs;
        int travelPartyCount{};
        std::uint32_t naturalPatternSeed{};
        std::uint64_t naturalPartySignature{};
        bool naturalPatternReady{};
        float handsBackCooldown{45.0F};
        float activityDispatchCooldown{};
        float traceTimer{0.0F};
        float wakeTimer{2.0F};
        float loadReleaseTime{};
        bool traceDue{false};
        bool enabled{true};
        FormationMode mode{FormationMode::kDynamic};
        bool initialized{false};
        RE::TESQuest* questCache{nullptr};
        bool questWarned{false};
    };
} // namespace Wayfarer

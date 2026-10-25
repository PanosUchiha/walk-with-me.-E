#pragma once
#include "alias_sync.h"
#include "party_life.h"
#include "party_orders.h"
#include "rest_recovery.h"

#include "companion_recovery.h"
#include "dialogue_guard.h"
#include "follower_catchup.h"
#include "gesture_awareness.h"
#include "individual_travel.h"
#include "loot_scout.h"
#include "menu_ui.h"
#include "party_recall.h"
#include "pathfinder.h"
#include "area_activities.h"
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
        void UpdateAliasSync(float dt);
        bool AliasSyncAllowed(std::int32_t ticket, std::int32_t session);
        void ReportAliasLook(std::int32_t slot, RE::Actor* actor, RE::Actor* target, std::int32_t ticket,
                             std::int32_t session);
        void ReportAliasCleanup(std::int32_t slot, bool pending, std::int32_t ticket, std::int32_t session);
        std::int32_t GetTetherHandoffTicket(std::uint32_t slot);
        RE::BGSRefAlias* GetTetherHandoffAlias(std::uint32_t slot, std::int32_t ticket);
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
        [[nodiscard]] std::vector<RE::ActorHandle> RestDialogueActors() const;
        struct LootScoutView
        {
            RE::ActorHandle actor;
            bool valuable{};
            float elapsed{};
        };
        [[nodiscard]] std::vector<LootScoutView> LootScouts() const;
        // Companions out on a pathfinding trip, from the sighting until they rejoin.
        [[nodiscard]] std::vector<LootScoutView> Pathfinders() const;
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
        // NFF's catch-up timer goes back to the player's value before a save and while the journal
        // (NFF's menu) is open; the next update parks it again if Walk With Me still teleports.
        void ReleaseFrameworkCatchup(const char* reason);
        void RestartFrameworkCatchupAfterLoad();
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
        bool VictoryHoldState(RE::Actor* actor, std::int32_t ticket, std::int32_t state);
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
        // Imports whose recruitment signal is currently lost, and since when. Paused, not ended.
        [[nodiscard]] bool PackageControlSuspended(RE::FormID id) const;
        std::unordered_map<RE::FormID, std::uint64_t> packageLossSince;
        std::unordered_set<RE::FormID> packageSeenFollowing;
        const char* ImportCompanion(RE::Actor& actor);
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
            ObstacleAvoidance avoidance;
            float speedScale{1.0F};
            bool distantCatchup{}, sandboxActive{};
            float sandboxRadius{}, sandboxRefresh{};
            bool anchorSet{};
            bool restAnchorZSet{};
            float restAnchorZ{};
            // The anchor marker is the native sandbox centre. It starts at the stopping
            // point (restHome) and moves to wherever the companion walks, with a small
            // radius, so native sandbox does not pull them straight back.
            RE::NiPoint3 restHome{};
            bool restHomeSet{};
            bool focused{};
            float focusRadius{};
            AreaFeature feature{AreaFeature::None}; // The feature they are resting at.
            RE::ObjectRefHandle featureRef;
            RE::NiPoint3 featurePoint{};
            AreaFeature pendingFeature{AreaFeature::None}; // The feature the current walk heads for.
            RE::ObjectRefHandle pendingFeatureRef;
            RE::NiPoint3 pendingFeaturePoint{};
            bool poseTurn{};                  // Turning to face the feature before a pose.
            RE::NiPoint3 poseFacePoint{};
            bool socialLoop{};                // Holding a drink through a conversation.
            float poseExitCheck{};
            RE::ObjectRefHandle tableSeat;    // A chair at the inn table the party shares.
            RE::FormID tableIdle{};           // A seated drink or meal waiting to play.
            bool tableIdleActive{};
            float tableIdleTime{};            // Until the mug is put down.
            RE::FormID tableLook{};           // Who they turn to at the table: a companion or the server.
            float tableLookTime{};
            float tableRest{};                // After leaving the table, before coming back to it.
            bool socialHold{};
            bool socialWalk{}; // Walking over to a conversation partner.
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
            Loot::Visit scout;
            Loot::Target scoutTarget;
            float scoutCooldown{}, scoutInspect{}, scoutIdleGap{};
            bool scoutParked{}, scoutSawIdle{};
            int scoutRetries{};
            // Pathfinder: running ahead to point out an undiscovered place.
            Pathfinder::State pathfinder;
            RE::NiPoint3 pathfinderPlace{}; // The map marker being pointed out.
            RE::NiPoint3 pathfinderGoal{};  // Where they run to, then where they stand.
            RE::NiPoint3 pathfinderFrom{};  // Where the player stood when that was chosen.
            RE::ObjectRefHandle pathfinderRef; // Its map marker, to notice the player finding it.
            const char* pathfinderNoun{"something"};
        };

        FormationController() = default;

        void ScanFollowers(RE::PlayerCharacter* a_player);
        void InitializeVictoryEvents();
        void UpdateVictoryCelebration(RE::PlayerCharacter& player, float dt);
        void ResetVictoryCelebration(bool stopAnimations = true);
        void UpdateVictoryCleanup(float dt);
        std::string VictoryActorRejection(RE::Actor& actor, RE::PlayerCharacter& player, bool starting) const;
        void UpdateRestRecovery(ManagedFollower& follower, RE::Actor& actor, RE::TESObjectREFR& anchor,
                                float dt);
        std::optional<RE::NiPoint3> FindRestActivity(ManagedFollower& follower, RE::Actor& actor,
                                                     RE::TESObjectREFR& anchor);
        std::optional<RE::NiPoint3> FindRestSpot(ManagedFollower& follower, RE::Actor& actor,
                                                 RE::TESObjectREFR& anchor, float distance);
        [[nodiscard]] bool RestSpotCrowded(RE::FormID self, RE::NiPoint3 point, float clearance) const;
        void BeginRestWalk(ManagedFollower& follower, float budget = 20);
        void FocusRest(ManagedFollower& follower, RE::Actor& actor, RE::TESObjectREFR& anchor, float radius);
        void ClearRestAnchor(ManagedFollower& follower);
        [[nodiscard]] static RE::NiPoint3 RestCenter(const ManagedFollower& follower,
                                                     const RE::TESObjectREFR& anchor);
        [[nodiscard]] int FeatureUsers(RE::FormID self, RE::FormID feature) const;
        void BeginRestPose(ManagedFollower& follower, RE::Actor& actor, int kind, AreaFeature at);
        // The inn table: one companion sits, the others join, a server is called over.
        struct TavernTable
        {
            RE::FormID host{}, space{}, caller{};
            RE::NiPoint3 center{};
            float emptyTime{}, workerTime{};
            TableBeats beats;
            RE::ActorHandle worker;
            RE::ObjectRefHandle workerMark;
            void Reset() { *this = {}; }
        } table;
        bool bardPerforming{};
        void UpdateTavernTable(RE::PlayerCharacter& player, float dt);
        void UpdateBardApplause(RE::PlayerCharacter& player);
        void DissolveTable(const char* reason);
        void StartTableDrink(ManagedFollower& follower, RE::Actor& actor, bool meal);
        bool CallTableWorker(const std::vector<RE::FormID>& seated);
        [[nodiscard]] RE::TESObjectREFR* FreeTableChair(RE::Actor& actor, RE::NiPoint3 center) const;
        [[nodiscard]] RE::TESObjectREFR* SeatFor(ManagedFollower& follower);
        bool StartSocialWalk();
        void EndSocialWalk(ManagedFollower& follower, bool cancelPair);
        RE::TESObjectREFR* PrepareAuxMarker(ManagedFollower& follower, RE::Actor& actor, RE::NiPoint3 point,
                                            bool move = false);
        void UpdateConversationAwareness(RE::PlayerCharacter& player, float dt);
        void EndConversationAwareness();
        void UpdateLookout(RE::PlayerCharacter& player, float dt);
        void UpdateTravelRoles(RE::PlayerCharacter& player, float dt);
        void UpdateTravelBanter(RE::PlayerCharacter& player, float dt);
        void UpdateBanterMotion(RE::PlayerCharacter& player);
        [[nodiscard]] bool LootScoutPermitted() const;
        [[nodiscard]] bool HasLootScouts() const;
        [[nodiscard]] bool HasPathfinder() const;
        void UpdateLootScouts(RE::PlayerCharacter& player, float dt);
        void StartLootScout(RE::PlayerCharacter& player);
        bool DriveLootScout(ManagedFollower& follower, RE::Actor& actor, RE::PlayerCharacter& player,
                            bool retarget);
        void EndLootScout(ManagedFollower& follower, RE::Actor* actor, Loot::End reason);
        void UpdatePathfinder(RE::PlayerCharacter& player, float dt);
        void StartPathfinder(RE::PlayerCharacter& player);
        void EndPathfinder(ManagedFollower& follower, RE::Actor* actor, Pathfinder::End reason);
        [[nodiscard]] const char* PathfinderRejection(RE::PlayerCharacter& player) const;
        void NotePathfinder(std::string key, std::string detail);
        [[nodiscard]] std::optional<RE::NiPoint3> PathfinderGround(RE::PlayerCharacter& player,
                                                                   RE::NiPoint3 place) const;
        std::unordered_set<RE::FormID> pathfinderSeen; // Places already pointed out this session.
        float pathfinderScan{}, pathfinderCooldown{}, pathfinderNoteAge{};
        std::string pathfinderNoteKey, pathfinderNote; // Why the last scans pointed nothing out.
        void ForgetLootArea();
        std::unordered_set<RE::FormID> lootSeen;
        std::unordered_map<RE::FormID, float> lootBackoff;
        std::unordered_map<RE::FormID, Loot::Contents> lootContents; // Read once per area.
        struct LootRouteMemo
        {
            Loot::Route route;
            float until{};
        };
        std::unordered_map<RE::FormID, LootRouteMemo> lootRoutes; // Navmesh answers, briefly reused.
        Loot::Survey lootSurvey;
        Loot::Area lootArea;
        RE::FormID lootAreaSpace{};
        float lootScanTimer{}, lootClock{};
        bool lootInhabited{};
        std::vector<RE::Actor*> RecallRoster() const;
        std::vector<RE::Actor*> RecallCandidates() const;
        const char* RecallPlayerRejection(RE::PlayerCharacter& player) const;
        const char* RecallActorRejection(RE::Actor& actor, bool automatic) const;
        const char* RecallStateRejection(RE::Actor& actor, bool automatic) const;
        [[nodiscard]] bool IsTravelCompanion(RE::Actor& actor) const;
        void RecallParty(RE::PlayerCharacter& player);
        struct Landing
        {
            RE::NiPoint3 point;
            const char* source{};
        };
        // Never empty for a player standing in the world: the last tier is beside the player.
        Landing FindLanding(RE::PlayerCharacter& player, RE::FormID id, int index,
                            const std::vector<RE::NiPoint3>& reserved) const;
        void TeleportCompanion(RE::Actor& actor, RE::NiPoint3 landing, RE::PlayerCharacter& player);
        const char* CatchupPlayerRejection(RE::PlayerCharacter& player) const;
        void UpdateTeleportCatchup(RE::PlayerCharacter& player, float dt);
        void RecordPlayerTrail(RE::PlayerCharacter& player);
        static RE::NiPoint3 PlayerGround(RE::PlayerCharacter& player);
        static std::uint64_t TravelSpaceKey(RE::PlayerCharacter& player);
        std::unordered_map<RE::FormID, FollowerCatchup> catchups;
        std::unordered_map<RE::FormID, std::string> catchupHolds;
        PlayerTrail playerTrail;
        float catchupTimer{};
        // Whether Walk With Me is doing Nether's Follower Framework's teleporting (see
        // FrameworkWarpPark); decided each update, applied to NFF's nwsWarpStart global.
        void UpdateFrameworkCatchup();
        [[nodiscard]] bool HoldingRelaxCamp() const;
        FrameworkWarpPark frameworkWarp;
        bool frameworkCatchup{};
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
        // Someone hostile within reach is actually fighting the player or a companion. A combat
        // flag or a drawn weapon alone is not a fight; the party keeps its formation until this.
        [[nodiscard]] bool EnemiesEngaged(RE::PlayerCharacter& a_player) const;
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
        RE::TESObjectREFR* EnsureRestSeat(ManagedFollower& follower, RE::Actor& actor, RE::NiPoint3 point,
                                          float yaw);
        bool IsRelaxing() const { return mode == FormationMode::kSandbox; }
        bool SocialEligible(RE::Actor& actor) const;
        void UpdateSocial(float dt);
        [[nodiscard]] RE::TESObjectREFR* EnsureMarker(ManagedFollower& a_follower, RE::Actor& a_anchor);
        [[nodiscard]] RE::TESQuest* EnsureQuest();
        void CompleteAliasSync(std::uint64_t ticket, std::int32_t session, bool completed);
        [[nodiscard]] static RE::BGSRefAlias* GetRefAlias(RE::TESQuest* a_quest, std::uint32_t a_aliasID);
        void ReleaseTravel(ManagedFollower& a_follower, RE::Actor* a_actor, bool tetherHandoff = false);
        void ReleaseAll(bool keepLootScouts = false);
        void RemoveManaged(RE::FormID a_formID);
        void LogDiagnostic(RE::FormID a_formID, const char* a_name, const char* a_reason);
        void LogGlobalDiagnostic(const char* a_reason);
        void LogScanSummary(std::size_t a_high, std::size_t a_teammates, std::size_t a_inFaction);
        static void Notify(std::string_view a_message);

        std::unordered_map<RE::FormID, ManagedFollower> managed;
        mutable std::uint64_t enemiesCheckedAt{};
        mutable std::uint64_t enemiesSeenAt{};
        mutable bool enemiesEngaged{};
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
            bool shoulderSelected{}, clipChecked{};
            unsigned startAttempts{};
            unsigned playbackRetries{};
            float windowElapsed{};
            Victory::PositionHold hold;
            std::uint32_t variation{};
            float nextAttempt{};
            float sheathSettled{}, cleanupElapsed{};
            bool cleanupStopped{}, combatRestore{};
            std::uint64_t queuedAtAliasSync{};
            Victory::Equipment equipment{Victory::Equipment::Unsupported};
            std::string lastReason;
        };
        std::vector<VictoryReaction> victoryReactions;
        void QueueVictoryCleanup(VictoryReaction reaction, bool combat = false);
        std::vector<VictoryReaction> victoryCleanup;
        bool aliasSyncBusy{true};
        std::uint64_t completedAliasSyncs{};
        float victoryScanTimer{};
        std::uint32_t victorySequence{};
        bool victoryVoiceRequested{};
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
        RoleDebounce roleDebounce; // A new role assignment must hold before anyone moves.
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
        AliasSyncQueue aliasSyncQueue;
        std::array<std::uint32_t, PARTY_CAPACITY> aliasLookActors{}, aliasLookTargets{};
        std::array<bool, PARTY_CAPACITY> aliasCleanupPending{};
        float aliasSyncTimer{0.25F};
        float aliasSyncRetryDelay{0.25F};
        std::int32_t aliasSyncTicket{};
        std::int32_t aliasSyncSession{static_cast<std::int32_t>(GetTickCount64() ^ GetCurrentProcessId())};
        float loadReleaseTime{};
        bool traceDue{false};
        bool enabled{true};
        FormationMode mode{FormationMode::kDynamic};
        bool initialized{false};
        RE::TESQuest* questCache{nullptr};
        bool questWarned{false};
    };
} // namespace Wayfarer

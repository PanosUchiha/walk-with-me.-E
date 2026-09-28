#include "formation_controller.h"
#include "RE/C/CrosshairPickData.h"
#include "command_gesture.h"
#include "custom_followers.h"
#include "engine_compatibility.h"
#include "follower_eligibility.h"
#include "gesture_head_tracking.h"
#include "hand_hold_locomotion.h"
#include "hand_holding.h"
#include "package_control.h"
#include "travel_space.h"

#include "RE/B/BGSPackageDataBool.h"
#include "RE/B/BGSPackageDataLocation.h"
#include "RE/B/BGSScene.h"
#include "RE/B/BGSSceneAction.h"
#include "RE/B/BGSSceneActionDialogue.h"
#include "RE/E/ExtraSayToTopicInfo.h"
#include "RE/M/MenuTopicManager.h"
#include "RE/P/PackageLocation.h"
#include "RE/T/TESCustomPackageData.h"
#include "config.h"
#include "travel_speed.h"

#include <bit>
#include <numbers>
#include <span>

namespace
{
    constexpr RE::FormID CURRENT_FOLLOWER_FACTION = 0x0005C84E;

    // Nether's Follower Framework records, read out of nwsFollowerFramework.esp (2.8.6).
    constexpr std::string_view NFF_PLUGIN = "nwsFollowerFramework.esp";
    constexpr RE::FormID NFF_WARP_ENABLED = 0x01BDFF;  // nwsAllowWarp: its Catch Up toggle.
    constexpr RE::FormID NFF_WARP_INTERVAL = 0x068AE2; // nwsWarpStart: seconds between checks.
    constexpr RE::FormID NFF_SANDBOX_QUEST = 0x179283; // nwsFollowerSandbox, runs the warp timer.
    constexpr RE::FormID NFF_SANDBOX_RANGE = 0x05C4C6; // nwsSandboxRange.
    constexpr RE::FormID NFF_SANDBOX_PACKAGE = 0x019664; // nwsFollowerBoxPackPKG.
    constexpr RE::FormID NFF_LOOT_PACKAGES[]{0x415AD9, 0x41F941}; // nwsFollowerLoot{Ext,Int}PKG.

    constexpr RE::FormID WAYFARER_QUEST_LOCAL_ID = 0x805;

    // A script's own variable (not a property), searched from the most derived class up; the
    // object's variable array holds the base classes' variables first.
    RE::BSScript::Variable* ScriptVariable(RE::BSScript::Object& object, const RE::BSFixedString& name)
    {
        constexpr auto missing = static_cast<std::uint32_t>(-1);
        std::uint32_t index = missing, offset = 0;
        for (auto* type = object.type.get(); type; type = type->GetParent()) {
            if (index != missing) {
                offset += type->GetNumVariables();
                continue;
            }
            if (const auto* variables = type->GetVariableIter()) {
                for (std::uint32_t i = 0; i < type->GetNumVariables(); ++i) {
                    if (variables[i].name == name) {
                        index = i;
                        break;
                    }
                }
            }
        }
        return index == missing ? nullptr : std::addressof(object.variables[offset + index]);
    }

    // NFF's warp counts one per second tick into warpCDTime and teleports once the count reaches
    // nwsWarpStart. The count keeps climbing while the interval is parked, so handing the player's
    // interval back fires a warp on the very next tick: every door autosave pulled a Relax camp
    // into the new area, and it walked straight back out. Restarting the count gives Walk With Me a
    // full interval to park it again first.
    bool RestartFrameworkWarpCount()
    {
        auto* quest = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(NFF_SANDBOX_QUEST, NFF_PLUGIN);
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
        if (!quest || !policy) {
            return false;
        }
        RE::BSTSmartPointer<RE::BSScript::Object> script;
        const auto handle = policy->GetHandleForObject(RE::FormType::Quest, quest);
        if (!vm->FindBoundObject(handle, "nwsFollowerSandboxScript", script) || !script) {
            return false;
        }
        auto* count = ScriptVariable(*script, "warpCDTime");
        if (!count || !count->IsFloat()) {
            return false;
        }
        count->SetFloat(0.0F);
        return true;
    }
    float Distance2D(RE::NiPoint3 a_left, RE::NiPoint3 a_right)
    {
        const float x = a_left.x - a_right.x;
        const float y = a_left.y - a_right.y;
        return std::sqrt(x * x + y * y);
    }

    Wayfarer::RestContext ContextFor(RE::Actor& actor)
    {
        using Wayfarer::RestContext;
        bool town = false, inn = false, dungeon = false;
        auto* location = actor.GetCurrentLocation();
        for (int depth = 0; location && depth < 32; ++depth, location = location->parentLoc) {
            auto has = [&](RE::FormID id) {
                auto* key = RE::TESForm::LookupByID<RE::BGSKeyword>(id);
                return key && location->HasKeyword(key);
            };
            inn |= has(0x1CB87);
            for (auto id : {0x130DBu, 0x130E2u, 0x18EF1u, 0x130EFu, 0x100819u, 0x130F2u, 0x130F0u, 0x130ECu,
                            0x130EBu, 0x130DFu}) {
                dungeon |= has(id);
            }
            for (auto id : {0x13168u, 0x13167u, 0x13166u, 0x18EF0u, 0x130E9u, 0x868E2u}) {
                town |= has(id);
            }
        }
        if (dungeon) {
            return RestContext::Dungeon;
        }
        if (inn) {
            return RestContext::Tavern;
        }
        if (!actor.GetParentCell() || actor.GetParentCell()->IsInteriorCell()) {
            return RestContext::Interior;
        }
        return town ? RestContext::Settlement : RestContext::Wilderness;
    }
    // Finer than ContextFor: what kind of interior, cave or ruin they are resting in.
    Wayfarer::RestPlace PlaceFor(RE::Actor& actor)
    {
        Wayfarer::PlaceSignals signals;
        signals.interior = !actor.GetParentCell() || actor.GetParentCell()->IsInteriorCell();
        auto* location = actor.GetCurrentLocation();
        for (int depth = 0; location && depth < 32; ++depth, location = location->parentLoc) {
            auto has = [&](std::initializer_list<RE::FormID> ids) {
                for (auto id : ids) {
                    auto* key = RE::TESForm::LookupByID<RE::BGSKeyword>(id);
                    if (key && location->HasKeyword(key)) {
                        return true;
                    }
                }
                return false;
            };
            // LocSetCave, LocSetCaveIce, LocTypeAnimalDen, LocTypeMine.
            signals.cave |= has({0x130EF, 0x100819, 0x130DE, 0x18EF1});
            // Nordic and Dwemer ruins, forts, crypts, lairs, hives, generic and clearable dungeons.
            signals.ruin |= has({0x130F2, 0x130F0, 0x130F1, 0x130E7, 0x130E2, 0x130E1, 0x130E4, 0x130EB, 0x130EC,
                                 0x130DB, 0xF5E80});
            signals.inn |= has({0x1CB87});
            signals.playerHouse |= has({0xFC1A3});
            signals.temple |= has({0x1CD56});
            signals.store |= has({0x1CB86});
            // Castle, guild, barracks, jail, steward's dwelling.
            signals.hall |= has({0x1CD57, 0x1CD5A, 0x1CD55, 0x1CD59, 0x504F9});
            // City, settlement, town, farm, orc stronghold, hold capital, lumber mill.
            signals.town |= has({0x13168, 0x13167, 0x13166, 0x18EF0, 0x130E9, 0x868E2, 0x18EF2});
        }
        // A cave that is also flagged a dungeon reads as a cave, not a ruin.
        if (signals.cave) {
            signals.ruin = false;
        }
        return Wayfarer::ClassifyPlace(signals);
    }
    Wayfarer::PlaceTraits TraitsFor(RE::Actor& actor)
    {
        Wayfarer::PlaceTraits traits;
        auto* cell = actor.GetParentCell();
        if (cell && !cell->IsInteriorCell()) {
            auto* sky = RE::Sky::GetSingleton();
            traits.cold = sky && sky->currentWeather &&
                          sky->currentWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow);
        }
        auto* ice = RE::TESForm::LookupByID<RE::BGSKeyword>(0x100819); // LocSetCaveIce
        for (auto* location = actor.GetCurrentLocation(); ice && location && !traits.cold;
             location = location->parentLoc) {
            traits.cold = location->HasKeyword(ice);
        }
        auto* weapon = actor.GetEquippedObject(false);
        auto* sword = weapon ? weapon->As<RE::TESObjectWEAP>() : nullptr;
        traits.sword = sword && sword->GetWeaponType() == RE::WEAPON_TYPE::kOneHandSword;
        return traits;
    }
    // Fires, shrines and standing stones, recognized by model path (cached per base form).
    // A cooking pot or spit counts as a fire: it only ever hangs over one.
    Wayfarer::AreaFeature FeatureFor(RE::TESBoundObject& base)
    {
        static std::unordered_map<RE::FormID, Wayfarer::AreaFeature> cache;
        if (auto found = cache.find(base.GetFormID()); found != cache.end()) {
            return found->second;
        }
        auto feature = Wayfarer::AreaFeature::None;
        if (auto* furniture = base.As<RE::TESFurniture>()) {
            for (auto id : {0xA5CB3u, 0x68ADAu}) { // CraftingCookpot, isCookingSpit
                auto* key = RE::TESForm::LookupByID<RE::BGSKeyword>(id);
                if (key && furniture->HasKeyword(key)) {
                    feature = Wayfarer::AreaFeature::Fire;
                }
            }
        }
        else if (auto* model = base.As<RE::TESModel>(); model && model->GetModel()) {
            std::string path = model->GetModel();
            std::transform(path.begin(), path.end(), path.begin(),
                           [](unsigned char c) { return static_cast<char>(c == '/' ? '\\' : std::tolower(c)); });
            feature = Wayfarer::FeatureFromModel(path);
        }
        cache.emplace(base.GetFormID(), feature);
        return feature;
    }
    // Work, ritual and quest markers that are furniture to the engine but not a place to rest.
    bool UnsuitableRestFurniture(RE::TESFurniture& furniture)
    {
        static constexpr RE::FormID blocked[] = {
            0xC481F,  0xA82C3,  0x613A8,  0x613A9,  0xC432A,  0xC4329,  0xF09FD,  0xF71C8,  0xEF94F,
            0x70C0A,  0x2E8ED,  0xEB60C,  0xD8737,  0xC482F,  0x8C775,  0x10651B, 0x106514, 0x105D4B,
            0x103444, 0x103443, 0x101A74, 0x1018EB, 0xFDBE9,  0xFBA5E,  0xF6CD0,  0xF3B64,  0xEDF3C,
            0x10ACA9, 0xDBE42,  0xE5197,  0x72DFB,  0x88107,  0x9C6DE,  0x9C6C3,  0xA5CB3,  0x68ADA};
        for (auto id : blocked) {
            auto* key = RE::TESForm::LookupByID<RE::BGSKeyword>(id);
            if (key && furniture.HasKeyword(key)) {
                return true;
            }
        }
        return false;
    }
    // Turns toward a point a little each update; true once facing it.
    bool TurnToward(RE::Actor& actor, RE::NiPoint3 point, float dt)
    {
        constexpr float tolerance = .3F;
        const auto pos = actor.GetPosition();
        const float dx = point.x - pos.x, dy = point.y - pos.y;
        if (dx * dx + dy * dy < 1) {
            return true;
        }
        const float yaw = actor.GetAngleZ();
        const float difference = std::remainder(std::atan2(dx, dy) - yaw, 2 * std::numbers::pi_v<float>);
        if (std::abs(difference) <= tolerance) {
            return true;
        }
        const float step = std::clamp(dt, 0.0F, .2F) * 4.0F;
        Wayfarer::Engine::SetHeading(actor, yaw + std::clamp(difference, -step, step));
        return std::abs(difference) <= step + tolerance;
    }
    // A chair, bench or stool: somewhere to sit that is not a bed, workbench or special marker.
    bool SitFurniture(RE::TESObjectREFR& ref)
    {
        auto* base = ref.GetBaseObject();
        auto* furniture = base ? base->As<RE::TESFurniture>() : nullptr;
        return furniture && furniture->furnFlags.any(RE::TESFurniture::ActiveMarker::kCanSit) &&
               !furniture->furnFlags.any(RE::TESFurniture::ActiveMarker::kCanSleep) &&
               furniture->workBenchData.benchType == RE::TESFurniture::WorkBenchData::BenchType::kNone &&
               !UnsuitableRestFurniture(*furniture);
    }
    // A bench seats several: one per enabled sit marker.
    int SeatCount(RE::TESObjectREFR& ref)
    {
        auto* base = ref.GetBaseObject();
        auto* furniture = base ? base->As<RE::TESFurniture>() : nullptr;
        if (!furniture) {
            return 0;
        }
        const auto markers = static_cast<std::uint32_t>(furniture->furnFlags.underlying()) & 0x00FFFFFFU;
        return std::max(1, std::popcount(markers));
    }
    bool SeatedIn(RE::Actor& actor, const RE::TESObjectREFR* chair)
    {
        auto* state = actor.AsActorState();
        return chair && state && state->GetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSitting &&
               actor.GetOccupiedFurniture().get().get() == chair;
    }
    // Queues a Papyrus Actor method on someone Wayfarer does not control, such as an
    // inn server. Asynchronous: poll for the effect.
    template <class... Args>
    bool CallActorMethod(RE::Actor& actor, const char* function, Args... args)
    {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
        if (!policy) {
            return false;
        }
        const auto handle = policy->GetHandleForObject(actor.GetFormType(), &actor);
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result;
        return vm->DispatchMethodCall(handle, RE::BSFixedString("Actor"), RE::BSFixedString(function),
                                      RE::MakeFunctionArguments(std::move(args)...), result);
    }
    RE::FormID SocialSpace(RE::Actor& actor)
    {
        auto* cell = actor.GetParentCell();
        if (!cell) {
            return 0;
        }
        if (!cell->IsInteriorCell() && actor.GetWorldspace()) {
            return actor.GetWorldspace()->GetFormID();
        }
        return cell->GetFormID();
    }
    std::string PersonalityKey(RE::Actor& actor)
    {
        auto* base = actor.GetActorBase();
        auto* file = base ? base->GetFile(0) : nullptr;
        std::string source{file ? file->GetFilename() : "runtime"};
        std::transform(source.begin(), source.end(), source.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return fmt::format("{}|{:06X}", source,
                           base ? base->GetFormID() & (file && file->IsLight() ? 0xFFF : 0xFFFFFF) : 0);
    }
    Wayfarer::Personality PersonalityFor(RE::Actor& actor)
    {
        const auto& settings = Wayfarer::Settings::GetSingleton().Get();
        const auto key = PersonalityKey(actor);
        const auto found = settings.personalityOverrides.find(key);
        return Wayfarer::ResolvePersonality(
            key, found == settings.personalityOverrides.end() ? -1 : found->second, settings.personalities);
    }
    std::uint32_t TravelIdentity(RE::Actor& actor)
    {
        auto* file = actor.GetFile(0);
        const auto local = actor.GetFormID() & (file && file->IsLight() ? 0xFFF : 0xFFFFFF);
        std::string source{file ? file->GetFilename() : "runtime"};
        std::transform(source.begin(), source.end(), source.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return Wayfarer::IdentityHash(fmt::format("{}|{}|{:06X}", PersonalityKey(actor), source, local));
    }
    RE::BGSPackageDataBool* BoolInput(RE::TESPackage* package, std::uint8_t uid)
    {
        auto* custom = package ? skyrim_cast<RE::TESCustomPackageData*>(package->data) : nullptr;
        if (custom && custom->data.data && custom->data.uids) {
            for (std::uint16_t i = 0; i < custom->data.dataSize; ++i) {
                if (custom->data.uids[i] == uid) {
                    return skyrim_cast<RE::BGSPackageDataBool*>(custom->data.data[i]);
                }
            }
        }
        return nullptr;
    }

    bool IsSpeaking(RE::Actor& actor)
    {
        RE::TESConditionItem condition;
        condition.data.functionData.function = RE::FUNCTION_DATA::FunctionID::kIsTalking;
        condition.data.comparisonValue.f = 1;
        condition.data.flags.opCode = RE::CONDITION_ITEM_DATA::OpCode::kEqualTo;
        condition.data.object = RE::CONDITIONITEMOBJECT::kSelf;
        RE::ConditionCheckParams params{&actor, &actor};
        return condition.IsTrue(params);
    }

    // Places a quest reaches without the party. Nether's Follower Framework never warps into
    // the first four; the Soul Cairn admits only Serana.
    const char* RestrictedTeleportArea(RE::PlayerCharacter& player)
    {
        if (auto* cell = player.GetParentCell(); cell && cell->GetFormID() == 0x165A6) {
            return "Whiterun Underforge";
        }
        struct Place
        {
            RE::FormID id;
            const char* file;
            const char* name;
        };
        static constexpr Place places[]{{0x192B2, "Skyrim.esm", "Sovngarde"},
                                        {0x192AB, "Skyrim.esm", "Skuldafn"},
                                        {0x16E2B, "Dragonborn.esm", "Apocrypha"},
                                        {0x3B8F, "Dawnguard.esm", "the Soul Cairn"}};
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* location = player.GetCurrentLocation();
        for (int depth = 0; data && location && depth < 32; ++depth, location = location->parentLoc) {
            for (const auto& place : places) {
                if (location == data->LookupForm<RE::BGSLocation>(place.id, place.file)) {
                    return place.name;
                }
            }
        }
        return nullptr;
    }

    std::string_view ModeName(Wayfarer::FormationMode a_mode)
    {
        switch (a_mode) {
        case Wayfarer::FormationMode::kVanilla:
            return "Vanilla";
        case Wayfarer::FormationMode::kSandbox:
            return "Relax";
        case Wayfarer::FormationMode::kLead:
            return "Lead";
        case Wayfarer::FormationMode::kRear:
            return "Rear";
        case Wayfarer::FormationMode::kCompanion:
            return "Companion";
        case Wayfarer::FormationMode::kCustom:
            return "Custom";
        default:
            return "Natural";
        }
    }

    std::int32_t ProcedureOf(const RE::TESPackage* a_package)
    {
        return a_package ? static_cast<std::int32_t>(a_package->procedureType.get()) : -1;
    }

    std::string DescribePackage(const RE::TESPackage* a_package)
    {
        if (!a_package) {
            return "none";
        }
        return fmt::format("{:08X} packType={} procedure={} source={}", a_package->GetFormID(),
                           static_cast<std::int32_t>(a_package->packData.packType.get()),
                           ProcedureOf(a_package),
                           a_package->GetFile(0) ? a_package->GetFile(0)->GetFilename() : "runtime");
    }

    bool IsFollowerTravelPackage(const RE::TESPackage* package)
    {
        if (!package) {
            return false;
        }

        switch (static_cast<int>(package->procedureType.get())) {
        case 6:
        case 43:
        case 8:
        case 9:
        case 25:
        case 46:
            return true;
        default:
            return false;
        }
    }

} // namespace

namespace Wayfarer
{
    bool FormationController::IsCustomFollower(RE::Actor& actor)
    {
        auto* base = actor.GetActorBase();
        auto* file = base ? base->GetFile(0) : nullptr;
        std::string plugin{file ? file->GetFilename() : ""};
        std::transform(plugin.begin(), plugin.end(), plugin.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(CURRENT_FOLLOWER_FACTION);
        return Wayfarer::UsesCustomFollowerAI(plugin, base ? base->GetFormID() & 0xFFFFFF : 0,
                                              actor.IsPlayerTeammate(),
                                              Engine::FactionRank(actor, faction) >= 0);
    }

    const char* FormationController::RecruitmentRejectionFor(RE::Actor& actor, bool requireTeammate)
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* base = actor.GetActorBase();
        auto* file = base ? base->GetFile(0) : nullptr;
        std::string plugin{file ? file->GetFilename() : ""};
        std::transform(plugin.begin(), plugin.end(), plugin.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(CURRENT_FOLLOWER_FACTION);
        return RecruitmentRejection(
            {data && data->LookupModByName("nwsFollowerFramework.esp"), IsNFFFollower(actor),
             KnownCustomFollower(plugin, base ? base->GetFormID() & 0xFFFFFF : 0), actor.IsPlayerTeammate(),
             Engine::FactionRank(actor, faction),
             actor.AsActorValueOwner()->GetActorValue(RE::ActorValue::kWaitingForPlayer)},
            requireTeammate);
    }

    FormationController& FormationController::GetSingleton()
    {
        static FormationController singleton;
        return singleton;
    }

    void FormationController::Initialize()
    {
        std::lock_guard lock(stateMutex);
        sandbox.Reset();
        Settings::GetSingleton().Load();
        const auto& settings = Settings::GetSingleton().Get();
        enabled = settings.enabled;
        SetCustomFormation(settings.customFormation);
        mode = settings.mode;
        initialized = true;
        companionPush.ticket = static_cast<std::int32_t>(GetTickCount64() & 0x3fffffff);
        InitializeVictoryEvents();
        logger::info("[Following] Baseline r9: September 19 r3 movement restored; "
                     "post-victory reactions shelved; separate command gestures retained");
        logger::info(
            "[Enrollment] NFF roster guard r1: active slots authoritative; teammate-only fallback disabled");
        logger::info("[PackageControl] Popup r1: explicit per-save travel and rest control");
        scanTimer = settings.scanInterval;
        logger::info("[Controller] Initialized in {} mode; enabled={}", ModeName(mode), enabled);
    }

    void FormationController::Update(float dt)
    {
        std::lock_guard lock(stateMutex);
        if (!initialized || !std::isfinite(dt) || dt <= 0.0F || dt > 1.0F) {
            return;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->Is3DLoaded() || !player->GetParentCell()) {
            ResetVictoryCelebration(false);
            ReleaseAll();
            sandbox.Reset();
            intent.Reset();
            return;
        }
        UpdateFollowerDialogue(dt);
        UpdateFrameworkCatchup();
        UpdateVictoryCleanup(dt);
        UpdateCompanionPush();
        if (recallRequest.pending) {
            auto* ui = RE::UI::GetSingleton();
            const bool menuOpen =
                !ui || ui->GameIsPaused() || Menu::HasBlockingWindow() || ShouldSuspendForDialogue();
            const auto dispatch = recallRequest.Poll(generation, GetTickCount64(), menuOpen,
                                                     loadReleaseTime <= 0 && !RecallPlayerRejection(*player));
            if (dispatch == RecallDispatch::Run) {
                RecallParty(*player);
            }
            else if (dispatch == RecallDispatch::Cancel) {
                Notify("Party teleport cancelled: wait until you are ready to travel.");
            }
        }

        if (mode == FormationMode::kVanilla && loadReleaseTime <= 0) {
            ResetVictoryCelebration();
            return;
        }

        if (ShouldSuspendForDialogue()) {
            ResetVictoryCelebration();
            RestoreTravelBanter();
            social.ClearGestures();
            if (mode != FormationMode::kVanilla) {
                UpdateConversationAwareness(*player, dt);
            }
            LogGlobalDiagnostic("dialogue protected; AI updates suspended");
            return;
        }

        if (auto* ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
            return;
        }
        EndConversationAwareness();
        const auto& settings = Settings::GetSingleton().Get();
        if (loadReleaseTime > 0.0F) {
            loadReleaseTime -= dt;
            ResetVictoryCelebration(false);
            ReleaseAll();
            return;
        }
        const auto pos = player->GetPosition();
        const bool spaceChanged = lastCell && (lastWorld != player->GetWorldspace() ||
                                               ((lastInterior || player->GetParentCell()->IsInteriorCell()) &&
                                                lastCell != player->GetParentCell()));
        const bool jumped = havePlayerPosition && Distance2D(pos, lastPlayerPosition) > 1000.0F;
        if (spaceChanged || jumped) {
            ResetVictoryCelebration();
        }
        if ((spaceChanged || jumped) && !IsRelaxing()) {
            ReleaseAll();
            sandbox.Reset();
            intent.Reset();
        }
        if (spaceChanged) {
            // A new interior or worldspace is a new area; unlooted finds can be shown again on return.
            ForgetLootArea();
        }
        lastCell = player->GetParentCell();
        lastWorld = player->GetWorldspace();
        lastInterior = lastCell && lastCell->IsInteriorCell();
        lastPlayerPosition = pos;
        havePlayerPosition = true;
        UpdateTeleportCatchup(*player, dt);

        if (enabled) {
            scanTimer += dt;
            if (scanTimer >= settings.scanInterval) {
                scanTimer = 0.0F;
                ScanFollowers(player);
            }
        }
        UpdateVictoryCelebration(*player, dt);
        if (!victoryReactions.empty()) {
            ReleaseAll();
            sandbox.Reset();
            intent.Reset();
            return;
        }
        if (!enabled || (!IsRelaxing() && GlobalRejection(*player))) {
            LogGlobalDiagnostic(enabled ? GlobalRejection(*player) : "controller disabled");
            ReleaseAll();
            sandbox.Reset();
            intent.Reset();
            return;
        }
        RE::NiPoint3 velocity{};
        player->GetLinearVelocity(velocity);
        if (regroup.active) {
            intent.MaintainOrder();
        }
        intent.Update({pos.x, pos.y}, {velocity.x, velocity.y}, dt, settings.travel);
        bool gathered = !managed.empty();
        if (regroup.active) {
            for (const auto& [id, f] : managed) {
                if (auto actor = f.handle.get(); actor && UsabilityRejection(*actor, *player).empty()) {
                    const auto p = actor->GetPosition();
                    if (!f.goal.active || Length({p.x - f.goal.goal.x, p.y - f.goal.goal.y}) >
                                              settings.travel.arrivalRadius + 100) {
                        gathered = false;
                    }
                }
            }
        }
        if (regroup.Tick(dt, intent.IsMoving(), gathered)) {
            sandbox.Reset();
        }
        auto restTuning = settings.sandbox;
        // A companion waiting at a find, or waiting to lead the way, would otherwise be pulled
        // into temporary rest.
        if (regroup.active || HasLootScouts() || HasPathfinder()) {
            restTuning.automatic = false;
        }
        sandbox.Update({pos.x, pos.y}, Length({velocity.x, velocity.y}), dt,
                       order == PartyOrder::kRoam || mode == FormationMode::kSandbox,
                       settings.travel.idleRelease, restTuning);
        SetSandboxFlag(sandbox.active);
        const bool handEnabled = mode == FormationMode::kCompanion && settings.handHolding.enabled &&
                                 !settings.handHolding.partner.empty();
        if (!sandbox.active && !intent.CanTravel() && !handEnabled) {
            // Standing still is when a companion most often wanders over to a find.
            ReleaseAll(true);
            LogGlobalDiagnostic("waiting for sustained movement");
            UpdateLootScouts(*player, dt);
            UpdatePathfinder(*player, dt);
            return;
        }
        LogGlobalDiagnostic(nullptr);
        UpdateBanterMotion(*player);
        UpdateLootScouts(*player, dt);
        UpdatePathfinder(*player, dt);
        updateTimer += dt;
        traceTimer += dt;
        if (updateTimer < settings.updateInterval) {
            return;
        }
        const float elapsed = updateTimer;
        updateTimer = 0.0F;
        traceDue = traceTimer >= 1.0F;
        if (traceDue) {
            traceTimer = 0.0F;
        }
        UpdateTravelBanter(*player, elapsed);
        handsBackCooldown = std::max(0.0F, handsBackCooldown - elapsed);
        activityDispatchCooldown = std::max(0.0F, activityDispatchCooldown - elapsed);
        navigation.Collect(*player);
        UpdateTravelRoles(*player, elapsed);
        UpdateLookout(*player, elapsed);

        for (int slot = 0; slot < PARTY_CAPACITY; ++slot) {
            for (auto& [id, follower] : managed) {
                if (follower.slot != slot) {
                    continue;
                }
                auto actor = follower.handle.get();
                if (!actor) {
                    ReleaseTravel(follower, nullptr);
                    continue;
                }
                if (const auto reason = UsabilityRejection(*actor, *player); !reason.empty()) {
                    LogDiagnostic(id, actor->GetName(), reason.c_str());
                    ReleaseTravel(follower, actor.get());
                    continue;
                }

                if (HandHolding::OwnsMovement(id)) {
                    const auto approach = HandHolding::GetApproach();
                    bool continueTravel = false;
                    if (handEnabled && !sandbox.active && approach.actor == id && approach.handoffTravel) {
                        auto* alias = GetRefAlias(EnsureQuest(), FollowerAlias(follower.slot));
                        auto* package = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                            PackageLocal(follower.slot), settings.pluginName);
                        continueTravel = HandHolding::ContinueHandoffTravel(
                            true, follower.travelActive, alias && alias->GetReference() == actor.get(),
                            package && actor->GetCurrentPackage() == package);
                    }
                    if (continueTravel) {
                        // GetSlotFollower still returns none during handoff: only
                        // move the existing marker until the script clears its alias.
                        ApplyTravel(follower, *actor, *player, elapsed);
                    }
                    else if (follower.travelActive || follower.sandboxActive) {
                        ReleaseTravel(follower, actor.get(), true);
                    }
                    continue;
                }
                if (follower.scout.Active() || follower.pathfinder.Active()) {
                    continue; // UpdateLootScouts or UpdatePathfinder drives this companion.
                }
                if (IsRelaxing() && follower.anchorSet && (!actor->Is3DLoaded() || actor->IsInCombat())) {
                    EndRestPose(follower, actor.get());
                    follower.travelActive = follower.sandboxActive = true;
                    continue;
                }
                if (sandbox.active) {
                    ApplySandbox(follower, *actor, *player, elapsed);
                }
                else if (HandHolding::TravelRequested(intent.CanTravel(), mode == FormationMode::kCompanion,
                                                      settings.handHolding, id,
                                                      HandHolding::GetApproach().actor)) {
                    ApplyTravel(follower, *actor, *player, elapsed);
                }
                else if (follower.travelActive) {
                    ReleaseTravel(follower, actor.get());
                }
            }
        }
        UpdateTavernTable(*player, elapsed);
        UpdateBardApplause(*player);
        UpdateSocial(elapsed);
    }

    void FormationController::UpdateHandHolding(float dt)
    {
        std::lock_guard lock(stateMutex);
        if (!std::isfinite(dt) || dt <= 0 || dt > 1) {
            return;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        const bool paused = ui && ui->GameIsPaused();
        const bool permitted = initialized && enabled && mode == FormationMode::kCompanion &&
                               !sandbox.active && loadReleaseTime <= 0 && !ShouldSuspendForDialogue() &&
                               player && player->Is3DLoaded() && player->GetParentCell() &&
                               (!ui || (!ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) &&
                                        !ui->IsMenuOpen(RE::MainMenu::MENU_NAME)));
        const auto& tuning = Settings::GetSingleton().Get().handHolding;
        if (!HandHolding::BeginFrame(dt, permitted, paused, tuning)) {
            return;
        }
        RE::NiPointer<RE::Actor> selected;

        for (const auto& [id, follower] : managed) {
            auto actor = follower.handle.get();
            if (!actor || explicitExclusions.contains(id) || follower.recoveryReleasePending ||
                follower.goal.recoveryTime > 0 || follower.sandboxActive ||
                !InSameTravelSpace(*actor, *player) || !UsabilityRejection(*actor, *player).empty()) {
                continue;
            }
            if (HandHolding::ReferenceKey(*actor) == tuning.partner) {
                selected = actor;
                break;
            }
        }
        HandHolding::UpdatePartner(*player, selected.get(), dt, navigation);
        if (!selected) {
            return;
        }
        auto found = managed.find(selected->GetFormID());
        if (found == managed.end()) {
            return;
        }
        auto& follower = found->second;
        const auto ticket = HandHolding::HandoffTicket(found->first);
        // Script cleanup and the engine's package transition can finish in
        // different frames. Poll only AFTER the script's ticketed final ack;
        // an empty alias alone cannot bypass in-flight ClearKeepOffset work.
        if (HandHolding::HandoffCleanupFinished(found->first, ticket)) {
            ConfirmTetherHandoff(static_cast<std::uint32_t>(follower.slot), ticket);
        }
        if (!ticket || ticket == follower.requestedHandTicket) {
            return;
        }
        auto* quest = EnsureQuest();
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
        if (!quest || !quest->IsRunning() || !policy) {
            return;
        }
        const auto handle = policy->GetHandleForObject(RE::FormType::Quest, quest);
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result;
        // Request priority only. Alias mutation remains inside the serialized
        // script sweep, so it cannot race an older ClearKeepOffset call.
        if (vm->DispatchMethodCall(
                handle, RE::BSFixedString("WayfarerQuestScript"), RE::BSFixedString("RequestTetherHandoff"),
                RE::MakeFunctionArguments(static_cast<std::int32_t>(follower.slot), std::int32_t{ticket}),
                result)) {
            follower.requestedHandTicket = ticket;
            logger::info("[HandHolding/TETHER] Session {} priority alias handoff requested for slot {}",
                         ticket, follower.slot);
        }
    }

    std::int32_t FormationController::GetTetherHandoffTicket(std::uint32_t slot)
    {
        std::lock_guard lock(stateMutex);
        for (const auto& [id, f] : managed) {
            if (static_cast<std::uint32_t>(f.slot) == slot) {
                return HandHolding::HandoffTicket(id);
            }
        }
        return 0;
    }

    RE::BGSRefAlias* FormationController::GetTetherHandoffAlias(std::uint32_t slot, std::int32_t ticket)
    {
        std::lock_guard lock(stateMutex);
        if (!ticket || ShouldSuspendForDialogue()) {
            return nullptr;
        }
        for (const auto& [id, follower] : managed) {
            if (static_cast<std::uint32_t>(follower.slot) == slot &&
                HandHolding::HandoffTicket(id) == ticket && HandHolding::OwnsMovement(id)) {
                auto* alias = GetRefAlias(EnsureQuest(), FollowerAlias(slot));
                auto actor = follower.handle.get();
                // This is queried only at a serialized script boundary. Never
                // clear a reassigned alias, even if the old slot ticket survives.
                return actor && alias && (!alias->GetReference() || alias->GetReference() == actor.get())
                           ? alias
                           : nullptr;
            }
        }
        return nullptr;
    }

    void FormationController::ConfirmTetherHandoff(std::uint32_t slot, std::int32_t ticket)
    {
        std::lock_guard lock(stateMutex);
        if (!ticket || ShouldSuspendForDialogue()) {
            return;
        }
        for (const auto& [id, f] : managed) {
            if (static_cast<std::uint32_t>(f.slot) == slot) {
                auto actor = f.handle.get();
                auto* alias = GetRefAlias(EnsureQuest(), FollowerAlias(slot));
                if (!actor || !alias) {
                    return;
                }
                auto* package = actor->GetCurrentPackage();
                auto* source = package ? package->GetFile(0) : nullptr;
                const bool own = source && std::string_view(source->GetFilename()) ==
                                               Settings::GetSingleton().Get().pluginName;
                HandHolding::ConfirmHandoff(id, ticket, !alias->GetReference(), !own);
                return;
            }
        }
    }

    void FormationController::ResetForGameLoad()
    {
        std::lock_guard lock(stateMutex);
        ResetVictoryCelebration(false);
        aliasSyncBusy = true;
        ReleaseAll();
        dialogueGuard = {};
        regroup.Reset();
        sandbox.Reset();
        table.Reset();
        bardPerforming = false;
        ++generation;
        recallRequest = {};
        catchups.clear();
        catchupHolds.clear();
        playerTrail.Clear();
        // The loaded save holds the player's own NFF timer; the next update decides afresh.
        frameworkWarp = {};
        frameworkCatchup = false;
        pathfinderSeen.clear();
        pathfinderScan = pathfinderCooldown = 0;
        companionPush.Reset();
        order = PartyOrder::kTravel;
        mode = Settings::GetSingleton().Get().mode;
        managed.clear();
        markerPool = {};
        seatPool = {};
        gatherPool = {};
        roleTimer = 1.0F;
        roleCooldown = 0;
        handsBackCooldown = 45.0F;
        naturalPatternReady = false;
        travelPartyCount = 0;
        navigation = TravelNavigation{};
        manualRegistrations.clear();
        dialogueRosterTimer = 0.0F;
        diagnosticReasons.clear();
        lastGlobalReason.clear();
        lastScanSummary.clear();
        questCache = nullptr;
        questWarned = false;
        intent.Reset();
        lastCell = nullptr;
        lastWorld = nullptr;
        havePlayerPosition = false;
        scanTimer = Settings::GetSingleton().Get().scanInterval;
        updateTimer = traceTimer = 0.0F;
        aliasSyncQueue.Reset();
        aliasLookActors = {};
        aliasLookTargets = {};
        aliasCleanupPending = {};
        aliasSyncTicket = 0;
        aliasSyncSession = static_cast<std::int32_t>(static_cast<std::uint32_t>(aliasSyncSession) + 1);
        aliasSyncTimer = aliasSyncRetryDelay = 0.25F;
        ForgetLootArea();
        lootScanTimer = lootClock = 0.0F;
        static_cast<void>(Loot::ConsumeActivations());

        loadReleaseTime = 1.5F;
        if (restoredRelax) {
            mode = FormationMode::kSandbox;
            for (auto [id, slot] : restoredRelaxSlots) {
                auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
                auto* alias = GetRefAlias(EnsureQuest(), MarkerAlias(slot));
                auto* marker = alias ? alias->GetReference() : nullptr;
                if (!actor || !marker || !marker->GetBaseObject() ||
                    marker->GetBaseObject()->GetFormID() != 0x3B || explicitExclusions.contains(id) ||
                    slot >= Settings::GetSingleton().Get().maxFollowers) {
                    continue;
                }
                ManagedFollower follower;
                follower.handle = actor->GetHandle();
                follower.slot = slot;
                follower.anchorSet = true;
                managed.emplace(id, std::move(follower));
                markerPool[slot] = marker->GetHandle();
            }
            restoredRelax = false;
            restoredRelaxSlots.clear();
        }
        if (restoredVanilla) {
            mode = FormationMode::kVanilla;
            restoredVanilla = false;
            managed.clear();
        }
        logger::info("[Controller] Travel state reset; saved marker aliases will be reused.");
    }

    void FormationController::SetEnabled(bool a_enabled, bool a_notify)
    {
        std::lock_guard lock(stateMutex);
        enabled = a_enabled;
        regroup.Reset();
        if (!enabled) {
            ResetVictoryCelebration();
            ReleaseAll();
        }
        else {
            scanTimer = Settings::GetSingleton().Get().scanInterval;
            intent.Reset();
        }
        logger::info("[Controller] Enabled={}", enabled);
        if (a_notify) {
            Notify(enabled ? "Walk With Me enabled" : "Walk With Me disabled");
        }
    }

    bool FormationController::IsEnabled() const noexcept
    {
        std::lock_guard lock(stateMutex);
        return enabled;
    }

    void FormationController::SetMode(FormationMode a_mode, bool a_notify)
    {
        std::lock_guard lock(stateMutex);
        if (a_mode == FormationMode::kCustom && !CustomFormation().Defined()) {
            Notify("Create a custom formation in Travel settings first.");
            return;
        }
        ResetVictoryCelebration();
        if (mode != a_mode || sandbox.active || a_mode != FormationMode::kSandbox) {
            naturalPatternReady = false;
            ReleaseAll();
            sandbox.Reset();
            roleTimer = 1.0F;
            roleCooldown = 0;
            for (auto& [id, f] : managed) {
                ClearRestAnchor(f);
                f.role = -1;
            }
        }
        order = PartyOrder::kTravel;
        mode = static_cast<FormationMode>(std::clamp(static_cast<int>(a_mode), 0, orderCount - 1));
        if (mode == FormationMode::kVanilla) {
            regroup.Reset();
            intent.Reset();
            managed.clear();
            scanTimer = Settings::GetSingleton().Get().scanInterval;
            logger::info("[Controller] Vanilla: returned travel and rest to follower AI");
            return;
        }
        if (mode == FormationMode::kSandbox) {
            regroup.Reset();
        }
        else {
            regroup.Begin();
        }
        for (auto& [id, follower] : managed) {
            follower.goal.sinceCommit = 100.0F;
        }
        if (!intent.IsMoving()) {
            if (auto* player = RE::PlayerCharacter::GetSingleton(); player && player->GetParentCell()) {
                auto pos = player->GetPosition();
                intent.BeginOrder({pos.x, pos.y}, ForwardFromYaw(player->GetAngle().z));
            }
        }
        logger::info("[Controller] Formation mode={}", ModeName(mode));
        (void)a_notify;
    }

    void FormationController::CycleMode()
    {
        std::lock_guard lock(stateMutex);
        order = PartyOrder::kTravel;
        auto next = static_cast<FormationMode>((static_cast<int>(mode) + 1) % orderCount);
        if (next == FormationMode::kCustom && !CustomFormation().Defined()) {
            next = FormationMode::kDynamic;
        }
        SetMode(next);
    }

    FormationMode FormationController::GetMode() const noexcept
    {
        std::lock_guard lock(stateMutex);
        return mode;
    }

    void FormationController::ReloadSettings(bool a_notify)
    {
        std::lock_guard lock(stateMutex);
        ReleaseAll();
        sandbox.Reset();
        Settings::GetSingleton().Load();
        naturalPatternReady = false;
        roleTimer = 1;
        roleCooldown = 0;
        const auto& settings = Settings::GetSingleton().Get();
        SetCustomFormation(settings.customFormation);
        mode = settings.mode;
        SetEnabled(settings.enabled, false);
        intent.Reset();
        scanTimer = settings.scanInterval;
        diagnosticReasons.clear();
        std::vector<RE::FormID> excess;
        for (const auto& [id, follower] : managed) {
            if (follower.slot >= settings.maxFollowers) {
                excess.push_back(id);
            }
        }
        for (auto id : excess) {
            RemoveManaged(id);
        }
        if (a_notify) {
            Notify("Walk With Me settings reloaded");
        }
    }

    bool FormationController::RegisterFollower(RE::Actor* a_actor, int a_preferredSlot)
    {
        std::lock_guard lock(stateMutex);
        return a_actor && RegisterFollower(a_actor->GetFormID(), a_preferredSlot);
    }

    bool FormationController::RegisterFollower(RE::FormID a_formID, int a_preferredSlot)
    {
        std::lock_guard lock(stateMutex);
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_formID);
        if (!actor || actor->IsPlayerRef()) {
            return false;
        }
        manualRegistrations[a_formID] = a_preferredSlot;
        packageRegistrations.erase(a_formID);
        explicitExclusions.erase(a_formID);
        scanTimer = Settings::GetSingleton().Get().scanInterval;
        logger::info("[Controller] Manual registration {:08X}, preferred slot {}", a_formID, a_preferredSlot);
        return true;
    }

    bool FormationController::UnregisterFollower(RE::FormID a_formID)
    {
        std::lock_guard lock(stateMutex);
        const bool removed = manualRegistrations.erase(a_formID) > 0;
        RemoveManaged(a_formID);
        return removed;
    }

    bool FormationController::ExcludeFollower(RE::FormID a_formID)
    {
        std::lock_guard lock(stateMutex);
        if (!RE::TESForm::LookupByID<RE::Actor>(a_formID)) {
            return false;
        }
        explicitExclusions.insert(a_formID);
        manualRegistrations.erase(a_formID);
        dialogueRegistrations.erase(a_formID);
        packageRegistrations.erase(a_formID);
        RemoveManaged(a_formID);
        logger::info("[Controller] Explicit exclusion {:08X}", a_formID);
        return true;
    }

    bool FormationController::IncludeFollower(RE::FormID a_formID)
    {
        std::lock_guard lock(stateMutex);
        const bool removed = explicitExclusions.erase(a_formID) > 0;
        scanTimer = Settings::GetSingleton().Get().scanInterval;
        return removed;
    }

    bool FormationController::IsManaged(RE::FormID a_formID) const
    {
        std::lock_guard lock(stateMutex);
        return managed.contains(a_formID);
    }

    std::uint32_t FormationController::GetManagedCount() const noexcept
    {
        std::lock_guard lock(stateMutex);
        return static_cast<std::uint32_t>(managed.size());
    }

    void FormationController::ScanFollowers(RE::PlayerCharacter* a_player)
    {
        if (!a_player) {
            return;
        }

        const auto* followerFaction = RE::TESForm::LookupByID<RE::TESFaction>(CURRENT_FOLLOWER_FACTION);

        std::vector<RE::Actor*> candidates;
        std::size_t highCount = 0;
        std::size_t teammateCount = 0;
        std::size_t factionCount = 0;

        if (auto* lists = RE::ProcessLists::GetSingleton()) {
            Engine::ForEachHighActor(*lists, [&](RE::Actor& a_actor) {
                if (&a_actor == a_player) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                ++highCount;
                const bool teammate = a_actor.IsPlayerTeammate();
                const bool inFaction = followerFaction && a_actor.IsInFaction(followerFaction);
                if (teammate) {
                    ++teammateCount;
                }
                if (inFaction) {
                    ++factionCount;
                }

                const auto formID = a_actor.GetFormID();

                const bool plausible = teammate || inFaction || IsRegisteredFollower(formID);

                if (explicitExclusions.contains(formID)) {
                    if (plausible) {
                        LogDiagnostic(formID, a_actor.GetName(), "excluded through the Walk With Me API");
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                const char* rejection = IsRegisteredFollower(formID) ? nullptr : CandidateRejection(a_actor);
                if (!rejection) {
                    candidates.push_back(&a_actor);
                }
                else if (plausible) {
                    LogDiagnostic(formID, a_actor.GetName(), rejection);
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }

        LogScanSummary(highCount, teammateCount, factionCount);

        std::sort(candidates.begin(), candidates.end(),
                  [](const RE::Actor* a_left, const RE::Actor* a_right) {
                      return a_left->GetFormID() < a_right->GetFormID();
                  });

        std::unordered_set<RE::FormID> seen;

        if (Settings::GetSingleton().Get().teleportCatchup) {
            for (const auto& [id, f] : managed) {
                auto actor = f.handle.get();
                if (actor && !explicitExclusions.contains(id) && !actor->IsDead() && !actor->IsDisabled() &&
                    (IsRegisteredFollower(id) || !CandidateRejection(*actor))) {
                    seen.insert(id);
                }
            }
        }
        if (IsRelaxing()) {
            for (const auto& [id, f] : managed) {
                auto actor = f.handle.get();
                if (f.anchorSet && actor && !explicitExclusions.contains(id) &&
                    (IsRegisteredFollower(id) || !CandidateRejection(*actor))) {
                    seen.insert(id);
                }
            }
        }
        const auto maxFollowers = static_cast<std::size_t>(Settings::GetSingleton().Get().maxFollowers);
        for (auto* actor : candidates) {
            if (!actor) {
                continue;
            }
            if (!InSameTravelSpace(*actor, *a_player)) {
                LogDiagnostic(actor->GetFormID(), actor->GetName(),
                              "in a different cell or worldspace than the player");
                continue;
            }
            const auto formID = actor->GetFormID();
            if (managed.contains(formID)) {
                managed[formID].handle = actor->GetHandle();
                seen.insert(formID);
                continue;
            }
            if (managed.size() >= maxFollowers) {
                LogDiagnostic(formID, actor->GetName(), "iMaxFollowers already reached");
                continue;
            }
            const auto registration = manualRegistrations.find(formID);
            const int preferredSlot = registration == manualRegistrations.end() ? -1 : registration->second;
            const int slot = FindAvailableSlot(preferredSlot);
            if (slot < 0) {
                LogDiagnostic(formID, actor->GetName(), "no free formation slot");
                continue;
            }
            ManagedFollower follower;
            follower.handle = actor->GetHandle();
            follower.slot = slot;
            managed.emplace(formID, std::move(follower));
            seen.insert(formID);
            logger::info("[Controller] Managing {:08X} ({}) in slot {}", formID, actor->GetName(), slot);
        }

        std::vector<RE::FormID> stale;
        for (const auto& [formID, follower] : managed) {
            static_cast<void>(follower);
            if (!seen.contains(formID)) {
                stale.push_back(formID);
            }
        }
        for (const auto formID : stale) {
            RemoveManaged(formID);
        }
    }

    const char* FormationController::CandidateRejection(RE::Actor& a_actor) const
    {
        const auto& settings = Settings::GetSingleton().Get();
        if (!settings.autoDiscover) {
            return "bAutoDiscover is off";
        }
        if (a_actor.IsDead()) {
            return "dead";
        }
        if (a_actor.IsDisabled()) {
            return "disabled";
        }

        if (const auto* base = a_actor.GetActorBase()) {
            if (const auto* file = base->GetFile(0);
                file && Settings::GetSingleton().IsPluginExcluded(file->GetFilename())) {
                return "source plugin listed in sExcludedPlugins";
            }
        }

        if (const auto* reason = RecruitmentRejectionFor(a_actor, settings.requirePlayerTeammate)) {
            return reason;
        }
        if (IsNFFFollower(a_actor)) {
            return settings.enforceNFF ? nullptr : "NFF automatic enrollment is off";
        }
        if (IsCustomFollower(a_actor) && !settings.enforceCustomFollowers) {
            return "custom follower enforcement is off";
        }
        return nullptr;
    }

    bool FormationController::IsAutomaticCandidate(RE::Actor& a_actor) const
    {
        return CandidateRejection(a_actor) == nullptr;
    }

    std::string FormationController::UsabilityRejection(RE::Actor& a_actor, RE::PlayerCharacter& a_player,
                                                        bool inspectBanterScene, bool allowDrawnWeapon) const
    {
        const auto& settings = Settings::GetSingleton().Get();
        const auto* actorState = a_actor.AsActorState();
        const auto heldEntry = managed.find(a_actor.GetFormID());
        const bool held = IsRelaxing() && heldEntry != managed.end() && heldEntry->second.anchorSet;
        if (a_actor.IsDead()) {
            return "dead";
        }
        if (a_actor.IsDisabled()) {
            return "disabled";
        }
        if (!held && !a_actor.Is3DLoaded()) {
            return "3D not loaded";
        }
        if (!a_actor.IsAIEnabled()) {
            return "AI disabled";
        }
        if (!held && !InSameTravelSpace(a_actor, a_player)) {
            return "in a different cell or worldspace than the player";
        }
        if (!actorState) {
            return "no actor state";
        }
        if (!held && a_actor.IsInCombat()) {
            return "in combat";
        }
        if (!held && a_actor.IsSneaking()) {
            return "sneaking";
        }
        if (actorState->IsSwimming()) {
            return "swimming";
        }
        if (a_actor.IsOnMount()) {
            return "mounted";
        }
        if (actorState->IsBleedingOut()) {
            return "bleeding out";
        }
        if (actorState->IsUnconscious()) {
            return "unconscious";
        }

        const auto* sittingPackage = a_actor.GetCurrentPackage();
        const auto* sittingSource = sittingPackage ? sittingPackage->GetFile(0) : nullptr;
        const bool ourSandbox = heldEntry != managed.end() &&
                                (heldEntry->second.sandboxActive ||
                                 (heldEntry->second.travelActive && sittingSource &&
                                  std::string_view(sittingSource->GetFilename()) == settings.pluginName));
        if (!ourSandbox && !sandbox.active &&
            actorState->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) {
            return "sitting or sleeping";
        }
        if (a_actor.AsActorValueOwner()->GetActorValue(RE::ActorValue::kWaitingForPlayer) > 0.0F) {
            return "waiting for player";
        }
        if (a_actor.GetCurrentScene() && !inspectBanterScene &&
            !IsWalkingBanterScene(a_actor.GetCurrentScene())) {
            return "in a scene";
        }
        if (actorState->GetLifeState() == RE::ACTOR_LIFE_STATE::kRestrained) {
            return "restrained by another behavior";
        }
        if (!held && !allowDrawnWeapon && actorState->IsWeaponDrawn()) {
            return "weapon drawn";
        }
        if (!IsRegisteredFollower(a_actor.GetFormID()) && CandidateRejection(a_actor)) {
            return "no longer an eligible follower";
        }
        const auto* current = a_actor.GetCurrentPackage();
        const auto* source = current ? current->GetFile(0) : nullptr;
        const bool own = source && std::string_view(source->GetFilename()) == settings.pluginName;
        const bool banterPackage =
            settings.walkingBanter && current && static_cast<int>(current->packData.packType.get()) == 28 &&
            (inspectBanterScene ||
             std::any_of(travelBanterPairs.begin(), travelBanterPairs.end(),
                         [&](const auto& pair) { return pair.Contains(a_actor.GetFormID()); }));
        if (settings.requireTravelPackage && !sandbox.active && !own && !banterPackage &&
            !(a_actor.GetCurrentScene() &&
              (inspectBanterScene || IsWalkingBanterScene(a_actor.GetCurrentScene()))) &&
            !IsRegisteredFollower(a_actor.GetFormID()) &&
            !(settings.enforceCustomFollowers && IsCustomFollower(a_actor)) &&
            !(settings.enforceNFF && IsNFFFollower(a_actor))) {
            const auto* package = a_actor.GetCurrentPackage();
            if (!IsFollowerTravelPackage(package)) {
                if (!package) {
                    return "no AI package running (bRequireTravelPackage)";
                }
                return fmt::format("busy with package {} (bRequireTravelPackage)", DescribePackage(package));
            }
        }
        const float distance = Distance2D(a_actor.GetPosition(), a_player.GetPosition());
        if (!held && distance > settings.releaseDistance) {
            return fmt::format("{:.0f} units away, beyond fReleaseDistance", distance);
        }
        return {};
    }

    bool FormationController::IsActorUsable(RE::Actor& a_actor, RE::PlayerCharacter& a_player) const
    {
        return UsabilityRejection(a_actor, a_player).empty();
    }

    const char* FormationController::GlobalRejection(RE::PlayerCharacter& a_player) const
    {
        const auto& settings = Settings::GetSingleton().Get();
        const auto* playerState = a_player.AsActorState();
        if (!playerState) {
            return "no player actor state";
        }
        if (auto* ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
            return "game paused";
        }
        if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) &&
                                               !settings.sandbox.automatic && !sandbox.active) {
            return "dialogue";
        }
        if (!a_player.GetParentCell()) {
            return "player cell unavailable";
        }
        if (settings.releaseInCombat && a_player.IsInCombat()) {
            return "player in combat";
        }
        if (settings.releaseWhenSneaking && a_player.IsSneaking()) {
            return "player sneaking";
        }
        if (settings.releaseWhenWeaponDrawn && playerState->IsWeaponDrawn()) {
            return "player weapon drawn";
        }
        if (playerState->IsSwimming()) {
            return "player swimming";
        }
        if (a_player.IsOnMount()) {
            return "player mounted";
        }
        if (!sandbox.active && !settings.sandbox.automatic &&
            playerState->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) {
            return "player sitting or sleeping";
        }
        if (settings.releaseWhenControlsDisabled && !sandbox.active) {
            const auto* controls = RE::ControlMap::GetSingleton();
            if (controls && !controls->IsMovementControlsEnabled() &&
                !(settings.sandbox.automatic &&
                  RE::UI::GetSingleton()->IsMenuOpen(RE::DialogueMenu::MENU_NAME))) {
                return "player movement controls disabled";
            }
        }
        if (settings.disableIndoors) {
            const auto* cell = a_player.GetParentCell();
            if (cell && cell->IsInteriorCell()) {
                return "interior cell and bDisableIndoors is on";
            }
        }
        return nullptr;
    }

    bool FormationController::IsGlobalTravelStateSafe(RE::PlayerCharacter& a_player) const
    {
        return GlobalRejection(a_player) == nullptr;
    }

    int FormationController::FindAvailableSlot(int a_preferredSlot) const
    {
        std::set<int> occupied;
        for (const auto& [formID, follower] : managed) {
            static_cast<void>(formID);
            occupied.insert(follower.slot);
        }
        const int maximum = Settings::GetSingleton().Get().maxFollowers;
        if (a_preferredSlot >= 0 && a_preferredSlot < maximum && !occupied.contains(a_preferredSlot)) {
            return a_preferredSlot;
        }
        for (int slot = 0; slot < maximum; ++slot) {
            if (!occupied.contains(slot)) {
                return slot;
            }
        }
        return -1;
    }

    bool FormationController::IsWalkingBanterScene(const RE::BGSScene* scene) const
    {
        return scene && Settings::GetSingleton().Get().walkingBanter &&
               std::any_of(banterScenes.begin(), banterScenes.end(),
                           [&](const auto& lease) { return lease.scene == scene; });
    }

    void FormationController::RestoreTravelBanter()
    {

        for (auto& lease : banterScenes) {
            for (auto* action : lease.facingActions) {
                action->flags.set(RE::BGSSceneAction::Flag::kFaceTarget);
            }
            for (auto index : lease.exclusiveActors) {
                if (index < lease.scene->actorFlags.size()) {
                    lease.scene->actorFlags[index].set(RE::SCENE_ACTOR_FLAG::kRunOnlyScenePackages);
                }
            }
        }
        for (auto* topic : banterTopics) {
            topic->data.flags.reset(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kCanMoveWhileGreeting);
        }
        banterTopics.clear();
        banterScenes.clear();
        travelBanterPairs.clear();
    }

    void FormationController::UpdateTravelBanter(RE::PlayerCharacter& player, float dt)
    {
        const auto& settings = Settings::GetSingleton().Get();
        if (!settings.walkingBanter || sandbox.active || !intent.IsMoving()) {
            RestoreTravelBanter();
            return;
        }
        auto eligible = [&](RE::FormID id) -> RE::Actor* {
            const auto found = managed.find(id);
            if (found == managed.end() || found->second.scout.Active()) {
                return nullptr;
            }
            auto actor = found->second.handle.get();
            return actor && !actor->IsPlayerRef() && UsabilityRejection(*actor, player, true).empty()
                       ? actor.get()
                       : nullptr;
        };
        for (auto it = banterScenes.begin(); it != banterScenes.end();) {
            auto* first = eligible(it->first);
            auto* second = eligible(it->second);
            if (first && second && first->GetCurrentScene() == it->scene &&
                second->GetCurrentScene() == it->scene) {
                ++it;
                continue;
            }
            for (auto* action : it->facingActions) {
                action->flags.set(RE::BGSSceneAction::Flag::kFaceTarget);
            }
            for (auto index : it->exclusiveActors) {
                if (index < it->scene->actorFlags.size()) {
                    it->scene->actorFlags[index].set(RE::SCENE_ACTOR_FLAG::kRunOnlyScenePackages);
                }
            }
            logger::info("[Banter] restored scene {:08X}", it->scene->GetFormID());
            it = banterScenes.erase(it);
        }
        for (auto& pair : travelBanterPairs) {
            pair.remaining -= dt;
        }
        std::erase_if(travelBanterPairs, [&](const auto& pair) {
            auto* a = eligible(pair.first);
            auto* b = eligible(pair.second);
            return pair.remaining <= 0 || !a || !b ||
                   (a->GetCurrentScene() && !IsWalkingBanterScene(a->GetCurrentScene())) ||
                   (b->GetCurrentScene() && !IsWalkingBanterScene(b->GetCurrentScene()));
        });
        auto rememberPair = [&](RE::FormID a, RE::FormID b) {
            if (a == b) {
                return;
            }
            if (a > b) {
                std::swap(a, b);
            }
            for (auto& pair : travelBanterPairs) {
                if (pair.first == a && pair.second == b) {
                    pair.remaining = 4;
                    return;
                }
                if (pair.Contains(a) || pair.Contains(b)) {
                    return;
                }
            }
            travelBanterPairs.push_back({a, b, 4});
            roleTimer = 1;
            roleCooldown = 0;
            logger::info("[Banter] walking pair {:08X} + {:08X}", a, b);
        };
        std::vector<RE::FormID> ids;
        for (const auto& [id, f] : managed) {
            ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end());
        for (auto id : ids) {
            auto* actor = eligible(id);
            if (!actor) {
                continue;
            }
            auto* scene = actor->GetCurrentScene();
            if (traceDue && settings.traceMovement && (scene || IsSpeaking(*actor))) {
                auto target = actor->GetActorRuntimeData().dialogueItemTarget.get();
                logger::info("[Banter] {} ({:08X}) scene={:08X} target={:08X} package={}", actor->GetName(),
                             id, scene ? scene->GetFormID() : 0, target ? target->GetFormID() : 0,
                             DescribePackage(actor->GetCurrentPackage()));
            }
            if (!scene) {
                if (!IsSpeaking(*actor)) {
                    continue;
                }
                auto target = actor->GetActorRuntimeData().dialogueItemTarget.get();
                auto* listener = target ? target->As<RE::Actor>() : nullptr;
                if (listener && !listener->GetCurrentScene() && eligible(listener->GetFormID())) {
                    rememberPair(id, listener->GetFormID());
                }
                continue;
            }
            if (IsWalkingBanterScene(scene)) {
                continue;
            }
            if (!scene->parentQuest || scene->actors.size() != 2 || scene->actorFlags.size() != 2) {
                continue;
            }
            std::array<RE::Actor*, 2> participants{};
            bool allManaged = true, sceneOnly = false, dialogue = false, packageAction = false,
                 playerTarget = false, looping = false;
            for (std::size_t i = 0; i < 2; ++i) {
                auto* alias = GetRefAlias(scene->parentQuest, scene->actors[i]);
                auto* ref = alias ? alias->GetReference() : nullptr;
                participants[i] = ref ? eligible(ref->GetFormID()) : nullptr;
                allManaged &= participants[i] && participants[i]->GetCurrentScene() == scene;
                sceneOnly |= scene->actorFlags[i].any(RE::SCENE_ACTOR_FLAG::kNoPlayerActivation);
            }
            if (!allManaged || participants[0] == participants[1]) {
                continue;
            }
            const auto memberAlias = [&](std::uint32_t alias) {
                return alias == scene->actors[0] || alias == scene->actors[1];
            };
            for (auto* action : scene->actions) {
                if (!action) {
                    packageAction = true;
                    break;
                }
                const auto type = action->GetType();
                packageAction |=
                    type != RE::BGSSceneAction::Type::kDialogue && type != RE::BGSSceneAction::Type::kTimer;
                looping |= action->Loops();
                playerTarget |= action->flags.any(RE::BGSSceneAction::Flag::kHeadTrackPlayer);
                if (type != RE::BGSSceneAction::Type::kDialogue) {
                    continue;
                }
                dialogue = true;
                const auto* speech = static_cast<RE::BGSSceneActionDialogue*>(action);

                playerTarget |= !memberAlias(action->actorID) ||
                                (speech->headtrackActorID >= 0 &&
                                 !memberAlias(static_cast<std::uint32_t>(speech->headtrackActorID)));
            }
            if (!CanWalkBanterScene(2, allManaged, dialogue, packageAction, sceneOnly, playerTarget,
                                    looping)) {
                continue;
            }
            BanterSceneLease lease{scene, participants[0]->GetFormID(), participants[1]->GetFormID(), {}};

            for (std::uint32_t i = 0; i < 2; ++i) {
                if (scene->actorFlags[i].any(RE::SCENE_ACTOR_FLAG::kRunOnlyScenePackages)) {
                    lease.exclusiveActors.push_back(i);
                    scene->actorFlags[i].reset(RE::SCENE_ACTOR_FLAG::kRunOnlyScenePackages);
                }
            }
            for (auto* action : scene->actions) {
                if (action->GetType() == RE::BGSSceneAction::Type::kDialogue &&
                    action->flags.any(RE::BGSSceneAction::Flag::kFaceTarget)) {
                    lease.facingActions.push_back(action);
                    action->flags.reset(RE::BGSSceneAction::Flag::kFaceTarget);
                }
            }
            logger::info("[Banter] admitted scene {:08X}: {} + {}; relaxed facing actions={}",
                         scene->GetFormID(), participants[0]->GetName(), participants[1]->GetName(),
                         lease.facingActions.size());
            banterScenes.push_back(std::move(lease));
        }

        for (const auto& lease : banterScenes) {
            rememberPair(lease.first, lease.second);
        }
        UpdateBanterMotion(player);
    }

    void FormationController::UpdateBanterMotion(RE::PlayerCharacter& player)
    {
        if (!Settings::GetSingleton().Get().walkingBanter || sandbox.active || !intent.IsMoving()) {
            return;
        }
        std::vector<RE::TESTopicInfo*> activeTopics;
        for (const auto& pair : travelBanterPairs) {
            for (auto id : {pair.first, pair.second}) {
                auto found = managed.find(id);
                auto actor = found == managed.end() ? RE::NiPointer<RE::Actor>{} : found->second.handle.get();
                if (!actor || !UsabilityRejection(*actor, player).empty()) {
                    continue;
                }
                auto& runtime = actor->GetActorRuntimeData();
                auto target = runtime.dialogueItemTarget.get();
                const bool scene = IsWalkingBanterScene(actor->GetCurrentScene());
                auto* process = runtime.currentProcess;
                auto* high = process ? process->high : nullptr;
                if (!high) {
                    continue;
                }
                using Flag = RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS;
                auto* sayTo = actor->extraList.GetByType<RE::ExtraSayToTopicInfo>();
                auto* speech = sayTo && sayTo->item ? sayTo->item : high->greetTopic.get();
                auto* topic = speech ? speech->info : nullptr;
                const bool forced = topic && topic->data.flags.any(Flag::kIsForceGreet, Flag::kPlayerAddress,
                                                                   Flag::kRequiresPlayerActivation);
                if (!CanRelaxBanterFacing(pair, id, target ? target->GetFormID() : 0, scene,
                                          scene || IsSpeaking(*actor),
                                          high->talkingToPC || high->greetingPlayer, forced)) {
                    continue;
                }

                runtime.boolFlags.reset(RE::Actor::BOOL_FLAGS::kSceneHeadTrackRotation);
                runtime.boolBits.reset(RE::Actor::BOOL_BITS::kShouldRotateToTrack);
                if (scene) {
                    high->sceneHeadTrackTimer = 0;
                }
                if (topic) {
                    activeTopics.push_back(topic);
                    if (!topic->data.flags.any(Flag::kCanMoveWhileGreeting)) {
                        topic->data.flags.set(Flag::kCanMoveWhileGreeting);
                        banterTopics.push_back(topic);
                    }
                }
            }
        }
        std::erase_if(banterTopics, [&](auto* topic) {
            if (std::find(activeTopics.begin(), activeTopics.end(), topic) != activeTopics.end()) {
                return false;
            }
            topic->data.flags.reset(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kCanMoveWhileGreeting);
            return true;
        });
    }

    const char* FormationController::RecallPlayerRejection(RE::PlayerCharacter& player) const
    {
        if (!enabled) {
            return "Walk With Me is disabled";
        }
        const auto* state = player.AsActorState();
        if (!state || !player.Is3DLoaded() || !player.GetParentCell()) {
            return "Load a game first";
        }
        if (player.GetCurrentScene()) {
            return "Wait until the scene ends";
        }
        // A seated player (a chair, a workbench, a carriage) may still call the party; the
        // landing search starts from where they sit.
        if (state->IsBleedingOut() || state->IsUnconscious()) {
            return "Recover first";
        }
        return nullptr;
    }

    // Deliberately short, like Nether's Follower Framework's: a seated player, a crafting
    // station or locked movement no longer holds anyone back. What remains would put a companion
    // somewhere they cannot stand, or into a scene they are not part of.
    const char* FormationController::CatchupPlayerRejection(RE::PlayerCharacter& player) const
    {
        if (const auto* reason = RecallPlayerRejection(player)) {
            return reason;
        }
        if (player.IsDead() || player.IsInKillMove()) {
            return "player unavailable";
        }
        const auto* state = player.AsActorState();
        RE::NiPointer<RE::Actor> mount;
        const bool mounted = player.IsOnMount() && player.GetMount(mount) && mount;
        const auto* mountState = mounted ? mount->AsActorState() : nullptr;
        if ((state && state->IsFlying()) || (mountState && mountState->IsFlying())) {
            return "player is flying"; // On a dragon: the ground below is out of reach.
        }
        if (mounted ? mount->IsInMidair() : player.IsInMidair()) {
            return "player is in the air";
        }
        return nullptr;
    }

    bool FormationController::IsTravelCompanion(RE::Actor& actor) const
    {
        const auto id = actor.GetFormID();
        return !explicitExclusions.contains(id) && (IsRegisteredFollower(id) || !CandidateRejection(actor));
    }

    const char* FormationController::RecallActorRejection(RE::Actor& actor, bool automatic) const
    {
        if (!actor.AsActorState() || actor.IsPlayerRef() || actor.IsDead() || actor.IsDisabled()) {
            return "unavailable";
        }
        if (!IsTravelCompanion(actor)) {
            return "not a travelling companion";
        }
        return RecallStateRejection(actor, automatic);
    }

    // Whether this actor can be moved right now, whoever they follow. Seated or sleeping is not a
    // reason: MoveTo stands them up, which is what Nether's Follower Framework does too.
    const char* FormationController::RecallStateRejection(RE::Actor& actor, bool automatic) const
    {
        const auto* state = actor.AsActorState();
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto id = actor.GetFormID();
        if (!state || !player || actor.IsPlayerRef() || actor.IsDead() || actor.IsDisabled()) {
            return "unavailable";
        }
        if (actor.AsActorValueOwner()->GetActorValue(RE::ActorValue::kWaitingForPlayer) != 0) {
            return "waiting";
        }
        if (!actor.IsAIEnabled()) {
            return "AI disabled";
        }
        if (actor.IsHostileToActor(player)) {
            return "hostile";
        }
        if (actor.GetCurrentScene() || IsSpeaking(actor)) {
            return "in a scene";
        }
        if (actor.IsOnMount()) {
            return "riding";
        }
        if (state->GetLifeState() == RE::ACTOR_LIFE_STATE::kRestrained) {
            return "restrained";
        }
        if (state->IsBleedingOut() || state->IsUnconscious()) {
            return "down";
        }
        if (!automatic) {
            return nullptr;
        }
        // An explicit teleport may pull a companion out of a fight; catch-up never does.
        if (actor.IsInCombat()) {
            return "in combat";
        }
        if (HandHolding::OwnsMovement(id)) {
            return "holding hands";
        }
        if (const auto entry = managed.find(id); entry != managed.end() && entry->second.scout.Active()) {
            return "scouting loot";
        }
        if (const auto entry = managed.find(id); entry != managed.end() && entry->second.pathfinder.Active()) {
            return "pointing out a place";
        }
        // NFF leaves its own looters alone, and so does its stand-in.
        if (auto* data = RE::TESDataHandler::GetSingleton(); data && actor.GetCurrentPackage()) {
            for (const auto loot : NFF_LOOT_PACKAGES) {
                if (actor.GetCurrentPackage() == data->LookupForm<RE::TESPackage>(loot, NFF_PLUGIN)) {
                    return "looting for NFF";
                }
            }
        }
        return nullptr;
    }

    std::vector<RE::Actor*> FormationController::RecallRoster() const
    {
        std::vector<RE::Actor*> result;
        auto add = [&](RE::Actor* actor) {
            if (actor && std::find(result.begin(), result.end(), actor) == result.end()) {
                result.push_back(actor);
            }
        };
        for (const auto& [id, follower] : managed) {
            static_cast<void>(id);
            auto actor = follower.handle.get();
            add(actor.get());
        }
        for (const auto& [id, slot] : manualRegistrations) {
            static_cast<void>(slot);
            add(RE::TESForm::LookupByID<RE::Actor>(id));
        }
        for (const auto& [id, flags] : packageRegistrations) {
            static_cast<void>(flags);
            add(RE::TESForm::LookupByID<RE::Actor>(id));
        }
        for (const auto id : dialogueRegistrations) {
            add(RE::TESForm::LookupByID<RE::Actor>(id));
        }
        // NFF uses these active slots; vanilla uses the original follower slot.
        // Eligibility still runs through the strict enrollment guard in RecallActorRejection.
        if (auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(0x750BA); quest && quest->IsRunning()) {
            for (std::uint32_t slot = 0; slot < 12; ++slot) {
                auto* alias = GetRefAlias(quest, slot);
                auto* ref = alias ? alias->GetReference() : nullptr;
                add(ref ? ref->As<RE::Actor>() : nullptr);
            }
        }
        std::stable_sort(result.begin(), result.end(), [&](auto* left, auto* right) {
            if (managed.contains(left->GetFormID()) != managed.contains(right->GetFormID())) {
                return managed.contains(left->GetFormID());
            }
            return left->GetFormID() < right->GetFormID();
        });
        return result;
    }

    std::vector<RE::Actor*> FormationController::RecallCandidates() const
    {
        auto result = RecallRoster();
        std::erase_if(result, [&](auto* actor) { return RecallActorRejection(*actor, false) != nullptr; });
        result.resize(
            std::min(result.size(), static_cast<std::size_t>(Settings::GetSingleton().Get().maxFollowers)));
        return result;
    }

    RE::NiPoint3 FormationController::PlayerGround(RE::PlayerCharacter& player)
    {
        // A rider's own position sits on the saddle; the mount's is on the ground.
        RE::NiPointer<RE::Actor> mount;
        if (player.IsOnMount() && player.GetMount(mount) && mount) {
            return mount->GetPosition();
        }
        return player.GetPosition();
    }

    std::uint64_t FormationController::TravelSpaceKey(RE::PlayerCharacter& player)
    {
        auto* cell = player.GetParentCell();
        if (!cell) {
            return 0;
        }
        return cell->IsInteriorCell() ? reinterpret_cast<std::uintptr_t>(cell)
                                      : reinterpret_cast<std::uintptr_t>(player.GetWorldspace());
    }

    void FormationController::RecordPlayerTrail(RE::PlayerCharacter& player)
    {
        const auto* state = player.AsActorState();
        RE::NiPointer<RE::Actor> mount;
        const bool mounted = player.IsOnMount() && player.GetMount(mount) && mount;
        const bool grounded = state && !state->IsSwimming() && !state->IsFlying() &&
                              !(mounted ? mount->IsInMidair() : player.IsInMidair()) &&
                              (mounted || state->GetSitSleepState() == RE::SIT_SLEEP_STATE::kNormal);
        const auto p = mounted ? mount->GetPosition() : player.GetPosition();
        playerTrail.Record({p.x, p.y, p.z}, TravelSpaceKey(player), grounded);
    }

    FormationController::Landing FormationController::FindLanding(
        RE::PlayerCharacter& player, RE::FormID id, int index, const std::vector<RE::NiPoint3>& reserved) const
    {
        const auto pos = PlayerGround(player);
        const auto forward = ForwardFromYaw(player.GetAngleZ());
        const auto right = RightFromForward(forward);
        const GroundPoint here{pos.x, pos.y, pos.z};
        const auto space = TravelSpaceKey(player);
        auto at = [](GroundPoint p) { return RE::NiPoint3{p.x, p.y, p.z}; };
        auto clear = [&](RE::NiPoint3 point, float room) {
            if (std::any_of(reserved.begin(), reserved.end(),
                            [&](auto other) { return Distance2D(other, point) < room; })) {
                return false;
            }
            bool open = true;
            if (auto* lists = RE::ProcessLists::GetSingleton()) {
                Engine::ForEachHighActor(*lists, [&](RE::Actor& other) {
                    if (open && other.GetFormID() != id && std::abs(other.GetPosition().z - point.z) < 120 &&
                        Distance2D(other.GetPosition(), point) < room && InSameTravelSpace(other, player)) {
                        open = false;
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
            return open;
        };

        // 1. This companion's own place behind the player: a straight line of navmesh, any slope.
        for (int attempt = 0; attempt < 6; ++attempt) {
            const float back = 220.0F + 90.0F * (index / 2) + 60.0F * (attempt / 2);
            const float side = ((index + attempt) % 2 ? 1.0F : -1.0F) * (90.0F + 45.0F * (attempt % 3));
            const RE::NiPoint3 proposed{pos.x - forward.x * back + right.x * side,
                                        pos.y - forward.y * back + right.y * side, pos.z};
            if (auto ground = navigation.LandingPoint(pos, proposed); ground && clear(*ground, 100)) {
                return {*ground, "behind"};
            }
        }
        // 2. Where the player walked. Walkable a moment ago and joined to the player by that
        //    route, so it holds around corners, on stairs and in narrow passages.
        for (const auto& point : playerTrail.Behind(here, space, 200, 1200, 150, 800)) {
            if (clear(at(point), 90)) {
                return {at(point), "trail"};
            }
        }
        // 3. Anywhere reachable on foot nearby, behind the player first.
        for (const auto& point : RankLandings(navigation.LandingSpots(pos, 120, 700), here, forward, 320)) {
            if (clear(at(point), 90)) {
                return {at(point), "navmesh"};
            }
        }
        // 4. Beside the player, as Nether's Follower Framework does, and finally the player's own
        //    footing. Character collision separates them; this tier never refuses.
        for (const auto side : {Vec2{-forward.x, -forward.y}, Vec2{right.x, right.y}, Vec2{-right.x, -right.y}}) {
            const RE::NiPoint3 proposed{pos.x + side.x * 80, pos.y + side.y * 80, pos.z};
            if (auto ground = navigation.LandingPoint(pos, proposed); ground && clear(*ground, 60)) {
                return {*ground, "beside"};
            }
        }
        if (const auto steps = playerTrail.Behind(here, space, 0, 400, 40, 400); !steps.empty()) {
            return {at(steps.front()), "footsteps"};
        }
        return {pos, "player"};
    }

    void FormationController::TeleportCompanion(RE::Actor& actor, RE::NiPoint3 landing,
                                                RE::PlayerCharacter& player)
    {
        if (auto entry = managed.find(actor.GetFormID()); entry != managed.end()) {
            ReleaseTravel(entry->second, &actor);
            ClearRestAnchor(entry->second);
        }
        actor.MoveTo(&player);
        actor.SetPosition(landing, true);
        actor.EvaluatePackage(false, true);
    }

    const char* FormationController::CompanionPushRejection(RE::Actor& actor) const
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!initialized || !player || !player->Is3DLoaded() || player->IsDead()) {
            return "Load a game first";
        }
        if (actor.IsPlayerRef() || actor.IsDead() || actor.IsDisabled() || !actor.Is3DLoaded()) {
            return "Companion is not available";
        }
        if (!InSameTravelSpace(actor, *player) ||
            actor.GetPosition().GetDistance(player->GetPosition()) > 800.0F) {
            return "Move closer to this companion";
        }
        if (player->IsInCombat() || actor.IsInCombat() || actor.IsHostileToActor(player)) {
            return "Wait until combat ends";
        }
        if (actor.GetCurrentScene() || player->GetCurrentScene() || IsSpeaking(actor) ||
            IsSpeaking(*player)) {
            return "Wait until the conversation or scene ends";
        }
        if (!actor.IsAIEnabled()) {
            return "This companion's AI is disabled";
        }
        const auto* state = actor.AsActorState();
        if (!state || actor.IsOnMount() || actor.IsInKillMove() || actor.IsInRagdollState() ||
            actor.IsInMidair() || state->IsSwimming() || state->IsFlying() || state->IsBleedingOut() ||
            state->IsUnconscious() || state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal ||
            state->GetLifeState() == RE::ACTOR_LIFE_STATE::kRestrained) {
            return "Wait until this companion is standing on the ground";
        }
        if (HandHolding::OwnsMovement(actor.GetFormID()) || HandHolding::HandoffTicket(actor.GetFormID())) {
            return "Release hands and let the connection finish first";
        }
        return nullptr;
    }

    void FormationController::RequestCompanionPush(RE::FormID id, std::uint64_t expectedGeneration)
    {
        std::lock_guard lock(stateMutex);
        if (generation != expectedGeneration) {
            return;
        }
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
        if (!actor) {
            return;
        }
        if (const auto* reason = CompanionPushRejection(*actor)) {
            Notify(reason);
            return;
        }
        if (!companionPush.Request(id, generation, GetTickCount64())) {
            Notify("Wait a moment before another push.");
        }
    }

    void FormationController::UpdateCompanionPush()
    {
        if (!companionPush.queue.pending && !companionPush.dispatched) {
            return;
        }
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(companionPush.actor);
        auto* ui = RE::UI::GetSingleton();
        const bool menuOpen =
            !ui || ui->GameIsPaused() || Menu::HasBlockingWindow() || ShouldSuspendForDialogue();
        const auto result =
            companionPush.Poll(generation, GetTickCount64(), menuOpen,
                               loadReleaseTime <= 0 && actor && !CompanionPushRejection(*actor));
        if (result == RecallDispatch::Cancel) {
            Notify("Unstick cancelled: companion is no longer ready.");
            return;
        }
        if (result != RecallDispatch::Run) {
            return;
        }
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        struct Completion final : RE::BSScript::IStackCallbackFunctor
        {
            std::uint64_t save;
            explicit Completion(std::uint64_t value) : save(value) {}
            void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
            void operator()(RE::BSScript::Variable value) override
            {
                const bool pushed = value.IsBool() && value.GetBool();
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([stamp = save, pushed] {
                        if (FormationController::GetSingleton().Snapshot().generation == stamp) {
                            Engine::Notify(pushed ? "Unstick push applied."
                                                  : "Unstick cancelled: companion is no longer ready.");
                        }
                    });
                }
            }
        };
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{new Completion(generation)};
        if (!vm || !vm->DispatchStaticCall(RE::BSFixedString("WayfarerRecovery"), RE::BSFixedString("Push"),
                                           RE::MakeFunctionArguments(static_cast<RE::Actor*>(actor),
                                                                     std::int32_t{companionPush.ticket}),
                                           callback)) {
            companionPush.Reset();
            logger::error("[CompanionRecovery] Cannot dispatch Push; install WayfarerRecovery.pex");
            Notify("Unstick unavailable: recovery script could not be started.");
        }
    }

    bool FormationController::ConsumeCompanionPush(RE::Actor* actor, std::int32_t ticket)
    {
        std::lock_guard lock(stateMutex);
        auto* ui = RE::UI::GetSingleton();
        auto* controls = RE::ControlMap::GetSingleton();
        const bool safe = actor && ui && !ui->GameIsPaused() && !Menu::HasBlockingWindow() &&
                          !ShouldSuspendForDialogue() && loadReleaseTime <= 0 && controls &&
                          controls->IsMovementControlsEnabled() && !CompanionPushRejection(*actor);
        if (!companionPush.Consume(actor ? actor->GetFormID() : 0, ticket, generation, GetTickCount64(),
                                   safe)) {
            return false;
        }
        logger::info("[CompanionRecovery] Player requested a gentle push for {} ({:08X}), ticket {}",
                     actor->GetName(), actor->GetFormID(), ticket);
        return true;
    }

    void FormationController::RequestPartyRecall(std::uint64_t expectedGeneration)
    {
        std::lock_guard lock(stateMutex);
        if (generation != expectedGeneration) {
            return;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }
        if (const auto* reason = RecallPlayerRejection(*player)) {
            Notify(reason);
            return;
        }
        if (RecallCandidates().empty()) {
            Notify("No travelling companions are available to teleport.");
            return;
        }
        recallRequest.Request(generation, GetTickCount64());
    }

    void FormationController::RecallParty(RE::PlayerCharacter& player)
    {
        if (auto* controls = RE::ControlMap::GetSingleton();
            controls && !controls->IsMovementControlsEnabled()) {
            Notify("Party teleport cancelled: player movement is locked.");
            return;
        }
        const auto candidates = RecallCandidates();
        navigation.Collect(player);
        std::vector<RE::NiPoint3> reserved;
        for (auto* actor : candidates) {
            const auto id = actor->GetFormID();
            const auto landing = FindLanding(player, id, static_cast<int>(reserved.size()), reserved);
            TeleportCompanion(*actor, landing.point, player);
            reserved.push_back(landing.point);
            logger::info("[PartyRecall] Summoned {} ({:08X}) via {} to ({:.0f}, {:.0f}, {:.0f})",
                         actor->GetName(), id, landing.source, landing.point.x, landing.point.y,
                         landing.point.z);
        }
        // Name whoever stayed behind, and why, so a missing companion is never a mystery.
        std::vector<std::string> stayed;
        for (auto* actor : RecallRoster()) {
            const char* rejection = RecallActorRejection(*actor, false);
            const std::string_view reason = rejection ? rejection : "";
            if (!reason.empty() && reason != "unavailable" && reason != "not a travelling companion") {
                stayed.push_back(fmt::format("{} ({})", actor->GetName(), reason));
                logger::info("[PartyRecall] Left {} ({:08X}): {}", actor->GetName(), actor->GetFormID(), reason);
            }
        }
        if (!reserved.empty()) {
            ReleaseAll();
            sandbox.Reset();
            intent.Reset();
            for (auto& [id, follower] : managed) {
                static_cast<void>(id);
                ClearRestAnchor(follower);
            }
            scanTimer = Settings::GetSingleton().Get().scanInterval;
        }
        auto message = reserved.empty()
                           ? std::string{"No travelling companions to teleport."}
                           : fmt::format("Teleported {} companion{}.", reserved.size(), reserved.size() == 1 ? "" : "s");
        if (!stayed.empty()) {
            message += fmt::format(" Stayed: {}{}", stayed.front(),
                                   stayed.size() > 1 ? fmt::format(" and {} more", stayed.size() - 1) : "");
        }
        Notify(message);
    }

    void FormationController::ToggleTemporaryRelaxation(std::uint64_t expectedGeneration)
    {
        std::lock_guard lock(stateMutex);
        if (generation != expectedGeneration) {
            return;
        }
        auto settings = Settings::GetSingleton().Get();
        settings.sandbox.automatic = !settings.sandbox.automatic;
        if (!ApplyPreferences(settings)) {
            Notify("Could not save temporary relaxation setting.");
            return;
        }
        if (!settings.sandbox.automatic && !IsRelaxing()) {
            ReleaseAll();
            sandbox.Reset();
        }
        Notify(settings.sandbox.automatic ? "Temporary relaxation enabled."
                                          : "Temporary relaxation disabled.");
    }

    bool FormationController::HoldingRelaxCamp() const
    {
        return IsRelaxing() && Settings::GetSingleton().Get().holdRelaxing && !managed.empty();
    }

    // Nether's Follower Framework teleports its roster on its own timer, 512 units behind the
    // player with no ground check. While Walk With Me teleports, it does that job for NFF's whole
    // roster as well and parks NFF's timer, so the two never race and a Relax camp stays a camp.
    void FormationController::UpdateFrameworkCatchup()
    {
        auto* ui = RE::UI::GetSingleton();
        if (ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::JournalMenu::MENU_NAME))) {
            return; // NFF's menu may be showing the player's value; park again afterwards.
        }
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* interval = data ? data->LookupForm<RE::TESGlobal>(NFF_WARP_INTERVAL, NFF_PLUGIN) : nullptr;
        auto* allowed = data ? data->LookupForm<RE::TESGlobal>(NFF_WARP_ENABLED, NFF_PLUGIN) : nullptr;
        const auto& settings = Settings::GetSingleton().Get();
        const bool owns =
            OwnsFrameworkCatchup(interval && allowed, allowed && allowed->value == 1.0F,
                                 settings.replaceFrameworkCatchup,
                                 initialized && enabled && mode != FormationMode::kVanilla,
                                 settings.teleportCatchup, HoldingRelaxCamp());
        if (owns != frameworkCatchup) {
            frameworkCatchup = owns;
            logger::info("[Catchup] {} Nether's Follower Framework catch-up", owns ? "Taking over" : "Returning");
        }
        if (!interval) {
            return;
        }
        const float previous = interval->value;
        if (const float value = frameworkWarp.Update(owns, previous); std::isfinite(value)) {
            interval->value = value;
            if (owns) {
                logger::info("[Catchup] Parked NFF's catch-up timer (the player's value is {:.0f} s)", previous);
            }
            else {
                logger::info("[Catchup] Restored NFF's catch-up timer to {:.0f} s{}", value,
                             RestartFrameworkWarpCount() ? "" : " (its countdown could not be restarted)");
            }
        }
    }

    void FormationController::ReleaseFrameworkCatchup(const char* reason)
    {
        std::lock_guard lock(stateMutex);
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* interval = data ? data->LookupForm<RE::TESGlobal>(NFF_WARP_INTERVAL, NFF_PLUGIN) : nullptr;
        if (!interval) {
            return;
        }
        if (const float value = frameworkWarp.Release(interval->value); std::isfinite(value)) {
            interval->value = value;
            logger::info("[Catchup] Restored NFF's catch-up timer to {:.0f} s {}{}", value, reason,
                         RestartFrameworkWarpCount() ? "" : " (its countdown could not be restarted)");
        }
    }

    // Saves written by earlier builds hold NFF's full interval beside a count that climbed while it
    // was parked, so the first tick after loading one would warp everyone at once.
    void FormationController::RestartFrameworkCatchupAfterLoad()
    {
        std::lock_guard lock(stateMutex);
        if (Settings::GetSingleton().Get().replaceFrameworkCatchup && RestartFrameworkWarpCount()) {
            logger::info("[Catchup] Restarted NFF's catch-up countdown after loading");
        }
    }

    void FormationController::UpdateTeleportCatchup(RE::PlayerCharacter& player, float dt)
    {
        RecordPlayerTrail(player);
        catchupTimer += dt;
        if (catchupTimer < .25F) {
            return;
        }
        const float elapsed = std::min(catchupTimer, 1.0F);
        catchupTimer = 0;
        const auto& settings = Settings::GetSingleton().Get();
        if (!enabled || (!settings.teleportCatchup && !frameworkCatchup)) {
            catchups.clear();
            catchupHolds.clear();
            return;
        }
        // Roaming is not being lost: temporary rest, and a Relax camp the player lets follow them.
        const float roaming = std::max(settings.teleportDistance, settings.sandbox.maxRadius + 800.0F);
        const bool holdCamp = HoldingRelaxCamp();
        const char* playerReason = CatchupPlayerRejection(player);
        if (!playerReason) {
            playerReason = RestrictedTeleportArea(player);
        }
        // NFF's own sandbox keeps its group within nwsSandboxRange of a marker that trails the
        // player by up to 1024 units, and NFF never teleports a relaxing group.
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* nffSandbox = frameworkCatchup && data
                               ? data->LookupForm<RE::TESPackage>(NFF_SANDBOX_PACKAGE, NFF_PLUGIN)
                               : nullptr;
        auto* nffRange = nffSandbox ? data->LookupForm<RE::TESGlobal>(NFF_SANDBOX_RANGE, NFF_PLUGIN) : nullptr;
        const float nffRoaming =
            std::max(settings.teleportDistance, (nffRange ? nffRange->value : 2048.0F) + 1024.0F);
        const auto pos = PlayerGround(player);
        const auto maxFollowers = static_cast<std::size_t>(settings.maxFollowers);
        std::unordered_set<RE::FormID> present;
        std::vector<RE::NiPoint3> reserved;
        std::size_t eligible = 0;
        bool collected = false;
        for (auto* actor : RecallRoster()) {
            const auto id = actor->GetFormID();
            present.insert(id);
            const bool companion = IsTravelCompanion(*actor);
            // With NFF's timer parked, everyone NFF would have teleported is covered here.
            const bool covered =
                (companion && settings.teleportCatchup) || (frameworkCatchup && IsNFFFollower(*actor));
            const bool camper = IsRelaxing() && managed.contains(id);
            const char* reason = playerReason;
            if (!reason && !covered) {
                reason = "not a travelling companion";
            }
            if (!reason && camper && holdCamp) {
                reason = "relaxing at camp";
            }
            if (!reason) {
                reason = RecallStateRejection(*actor, true);
            }
            if (!reason && companion && ++eligible > maxFollowers) {
                reason = "iMaxFollowers reached";
            }
            float threshold = settings.teleportDistance;
            if (camper || (sandbox.active && managed.contains(id))) {
                threshold = roaming;
            }
            if (nffSandbox && actor->GetCurrentPackage() == nffSandbox) {
                threshold = nffRoaming;
            }
            // Another interior or worldspace, or an exterior cell too far away to be loaded.
            const bool separated = !actor->Is3DLoaded() || !InSameTravelSpace(*actor, player);
            const auto at = actor->GetPosition();
            const float distance = separated ? std::numeric_limits<float>::infinity()
                                             : CatchupDistance({at.x, at.y, at.z}, {pos.x, pos.y, pos.z});
            // Say once why a distant companion is being left where they are.
            const std::string hold = reason && (separated || distance >= threshold) ? reason : "";
            if (auto& last = catchupHolds[id]; last != hold) {
                if (!hold.empty()) {
                    logger::info("[Catchup] holding {} ({:08X}) {}: {}", actor->GetName(), id,
                                 separated ? std::string{"in another cell"} : fmt::format("at {:.0f} units", distance),
                                 hold);
                }
                last = hold;
            }
            auto& state = catchups[id];
            if (!state.Update(elapsed, !reason, distance, threshold, separated)) {
                continue;
            }
            if (!collected) {
                navigation.Collect(player);
                collected = true;
            }
            const auto entry = managed.find(id);
            const int index =
                entry != managed.end() ? std::max(entry->second.slot, 0) : static_cast<int>(reserved.size());
            const auto landing = FindLanding(player, id, index, reserved);
            state.Attempt(true);
            TeleportCompanion(*actor, landing.point, player);
            reserved.push_back(landing.point);
            logger::info("[Catchup] recalled {} ({:08X}) from {} via {} to ({:.0f}, {:.0f}, {:.0f}){}",
                         actor->GetName(), id,
                         separated ? std::string{"another cell"} : fmt::format("{:.0f} units", distance),
                         landing.source, landing.point.x, landing.point.y, landing.point.z,
                         companion ? "" : " for NFF");
        }
        std::erase_if(catchups, [&](const auto& entry) { return !present.contains(entry.first); });
        std::erase_if(catchupHolds, [&](const auto& entry) { return !present.contains(entry.first); });
    }

    void FormationController::UpdateTravelRoles(RE::PlayerCharacter& player, float dt)
    {

        if (sandbox.active) {
            naturalPatternReady = false;
            return;
        }
        if (!intent.IsMoving() && !regroup.active) {
            return;
        }
        roleTimer += dt;
        roleCooldown = std::max(0.0F, roleCooldown - dt);
        if (roleTimer < .5F || roleCooldown > 0) {
            return;
        }
        const float sinceCheck = roleTimer;
        roleTimer = 0;
        std::vector<std::pair<RE::FormID, ManagedFollower*>> followers;
        bool detouring = false;
        for (auto& [id, f] : managed) {
            auto actor = f.handle.get();
            if (actor && UsabilityRejection(*actor, player).empty()) {
                followers.emplace_back(id, &f);
                detouring |= f.avoidance.active;
            }
        }
        std::sort(followers.begin(), followers.end(), [](auto& a, auto& b) { return a.first < b.first; });
        if (followers.empty()) {
            return;
        }
        const auto& settings = Settings::GetSingleton().Get();
        const bool interior = player.GetParentCell()->IsInteriorCell();
        travelPartyCount = static_cast<int>(followers.size());
        std::uint64_t signature = 1469598103934665603ULL;

        std::vector<RE::FormID> members;
        for (const auto& [id, f] : managed) {
            members.push_back(id);
        }
        std::sort(members.begin(), members.end());
        for (auto id : members) {
            signature ^= id;
            signature *= 1099511628211ULL;
        }
        if (!naturalPatternReady || signature != naturalPartySignature) {
            naturalPartySignature = signature;
            naturalPatternReady = true;
            naturalPatternSeed = RestPose::Mix(static_cast<std::uint32_t>(GetTickCount64()) ^
                                               static_cast<std::uint32_t>(signature));
            logger::info("[Formation] party={} naturalStragglers={} setback={:.0f} spacing={:.2f}",
                         travelPartyCount,
                         mode == FormationMode::kDynamic
                             ? NaturalStragglerCount(travelPartyCount, naturalPatternSeed,
                                                     settings.travel.naturalStragglerDistance)
                             : 0,
                         settings.travel.naturalStragglerDistance, settings.travel.spacing);
        }
        std::vector<Vec2> actors, goals;
        std::vector<int> previous;
        // A custom formation may reserve places for particular companions: they take theirs, and
        // everyone else shares out the rest in the player's numbering.
        const bool custom = mode == FormationMode::kCustom;
        std::vector<int> reserved, open;
        if (custom) {
            std::vector<std::string> keys;
            for (auto& [id, f] : followers) {
                auto actor = f->handle.get();
                keys.push_back(actor ? HandHolding::ReferenceKey(*actor) : std::string{});
            }
            reserved = Custom::ReservedRoles(CustomFormation(), keys);
            open = Custom::OpenRoles(CustomFormation(), reserved,
                                     static_cast<int>(std::count(reserved.begin(), reserved.end(), -1)));
        }
        for (std::size_t index = 0; index < followers.size(); ++index) {
            const int role = custom ? (index < open.size() ? open[index] : 0) : static_cast<int>(index);
            const auto planned = PartyRole(mode, role, travelPartyCount, naturalPatternSeed,
                                           settings.travel.naturalStragglerDistance);
            const auto desired =
                intent.GoalAt(mode, planned.offset, interior, custom ? 1 : settings.preferredSide, settings.travel,
                              planned.straggler ? settings.travel.naturalStragglerDistance : 0);
            const auto grounded = navigation.Resolve(player.GetPosition(), desired, intent.Direction());

            if (!grounded) {
                return;
            }
            goals.push_back({grounded->x, grounded->y});
        }
        for (auto& [id, f] : followers) {
            auto actor = f->handle.get();
            if (!actor) {
                return;
            }
            const auto p = actor->GetPosition();
            actors.push_back({p.x, p.y});
            previous.push_back(f->role);
        }
        std::vector<std::array<int, 2>> pairs;
        for (const auto& pair : travelBanterPairs) {
            int a = -1, b = -1;
            for (int i = 0; i < static_cast<int>(followers.size()); ++i) {
                if (followers[i].first == pair.first) {
                    a = i;
                }
                if (followers[i].first == pair.second) {
                    b = i;
                }
            }
            if (a >= 0 && b >= 0) {
                pairs.push_back({a, b});
            }
        }
        auto roles = AssignNearestRoles(actors, goals, previous);
        if (custom) {
            // `goals` holds the open roles; nearest-assign only the unreserved companions to them.
            std::vector<Vec2> freeActors;
            std::vector<int> freePrevious, freeIndex;
            for (std::size_t i = 0; i < followers.size(); ++i) {
                if (reserved[i] < 0) {
                    freeActors.push_back(actors[i]);
                    const auto at = std::find(open.begin(), open.end(), previous[i]);
                    freePrevious.push_back(at == open.end() ? -1 : static_cast<int>(at - open.begin()));
                    freeIndex.push_back(static_cast<int>(i));
                }
            }
            const std::span<const Vec2> freeGoals(goals.data(), freeActors.size());
            const auto freeRoles = AssignNearestRoles(freeActors, freeGoals, freePrevious);
            for (std::size_t i = 0; i < followers.size(); ++i) {
                roles[i] = reserved[i];
            }
            for (std::size_t k = 0; k < freeIndex.size() && k < freeRoles.size(); ++k) {
                const int goal = freeRoles[k];
                roles[freeIndex[k]] = goal >= 0 && goal < static_cast<int>(open.size()) ? open[goal] : open[k];
            }
            pairs.clear(); // The player placed everyone; banter pairs do not rearrange them.
        }
        if (!pairs.empty()) {

            auto paired = PairNearbyRoles(actors, goals, previous, pairs, intent.Direction());
            if (paired.empty()) {
                paired = PairNearbyRoles(actors, goals, roles, pairs, intent.Direction());
            }
            if (!paired.empty()) {
                roles = std::move(paired);
            }
        }
        // Hold the proposal until it has won for a while. Someone stepping round an obstacle is
        // briefly off the route, so nothing is swapped mid-detour, though an unplaced companion
        // still gets a role at once.
        roles = roleDebounce.Update(previous, roles, detouring ? 0.0F : sinceCheck);
        if (roles.empty()) {
            return;
        }
        bool changed = false;
        for (std::size_t i = 0; i < roles.size(); ++i) {
            auto& f = *followers[i].second;
            if (f.role != roles[i]) {
                if (settings.traceMovement) {
                    logger::info("[Roles] {:08X} packageSlot={} role={} -> {}", followers[i].first, f.slot,
                                 f.role, roles[i]);
                }
                f.role = roles[i];
                changed = true;
            }
        }

        if (changed) {
            roleCooldown = pairs.empty() ? 1.0F : 3.0F;
        }
    }

    bool FormationController::SetHandApproachRadius(ManagedFollower& f, bool active)
    {
        if (!active && !f.handArrivalRadius.original) {
            return true;
        }
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* package = data ? data->LookupForm<RE::TESPackage>(PackageLocal(f.slot),
                                                                Settings::GetSingleton().Get().pluginName)
                             : nullptr;
        auto* custom = package ? skyrim_cast<RE::TESCustomPackageData*>(package->data) : nullptr;
        if (custom && custom->data.data && custom->data.uids) {
            for (std::uint16_t i = 0; i < custom->data.dataSize; ++i) {
                if (custom->data.uids[i] != 0) {
                    continue;
                }
                auto* input = skyrim_cast<RE::BGSPackageDataLocation*>(custom->data.data[i]);
                auto* location = input ? input->pointer : nullptr;
                if (!location || location->locType != RE::PackageLocation::Type::kAlias_Reference) {
                    break;
                }
                if (f.handArrivalRadius.Update(location->rad, active)) {
                    // An already-running route can cache the old stopping radius.
                    f.goal.hasRoute = false;
                    f.goal.sinceCommit = 100;
                    logger::info("[HandHolding/Approach] slot={} package={:08X} arrivalRadius={} active={}",
                                 f.slot, package->GetFormID(), location->rad, active);
                }
                return true;
            }
        }
        logger::warn("[HandHolding/Approach] slot={} travel location unavailable; approach refused", f.slot);
        return false;
    }

    void FormationController::ApplyTravel(ManagedFollower& f, RE::Actor& actor, RE::PlayerCharacter& player,
                                          float dt)
    {
        const auto& settings = Settings::GetSingleton().Get();
        ClearRestAnchor(f);
        auto* quest = EnsureQuest();
        if (!quest || !quest->IsRunning()) {
            ReleaseTravel(f, &actor);
            return;
        }
        auto* package = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(PackageLocal(f.slot),
                                                                                       settings.pluginName);
        if (!package) {
            ReleaseTravel(f, &actor);
            return;
        }
        if (f.sandboxActive) {
            f.avoidance.Reset();
            EndRestPose(f, &actor);
            f.restRecovery.Reset();
            f.activitySearch.Reset();
            f.activityTarget = {};
            f.socialHold = f.socialWalk = false;
            f.socialPhase = 0;
            f.patron = {};
            f.patronGesture = false;
            SetSocialFlag(f.slot, 0);
            f.sandboxActive = false;
            f.sandboxRadius = 0;
            f.goal.Release();

            f.travelActive = false;
            f.speedScale = 1;
            TravelSpeed::Clear(static_cast<std::uint32_t>(f.slot));
        }
        const auto pos = actor.GetPosition();
        const auto playerPos = player.GetPosition();
        const auto handApproach = HandHolding::GetApproach();
        const bool handGuided = handApproach.actor == actor.GetFormID();
        const bool classicHands = handGuided && settings.handHolding.method == HandHolding::Method::Classic;
        const bool handHoldPosition = handGuided && !classicHands && handApproach.holdPosition;
        const bool handHandoff = handGuided && handApproach.handoffTravel;
        const bool handAttached = classicHands && HandHolding::IsAttached(actor.GetFormID());
        const bool handLocomotion = classicHands && (handAttached || handApproach.closing);
        RE::NiPoint3 handVelocity{};
        if (classicHands || handHandoff) {
            player.GetLinearVelocity(handVelocity);
        }
        if (handLocomotion) {
            handVelocity = {handApproach.velocity.x, handApproach.velocity.y, 0};
        }
        const float handSpeed = std::hypot(handVelocity.x, handVelocity.y);
        if (!SetHandApproachRadius(f, handGuided && !classicHands)) {
            ReleaseTravel(f, &actor);
            return;
        }
        const auto travelTuning = HandHolding::ApproachTuning(settings.travel, handGuided && !classicHands);
        const bool interior = player.GetParentCell()->IsInteriorCell();
        const float playerDistance = Distance2D(pos, playerPos);
        f.distantCatchup = DistantCatchup(f.distantCatchup, playerDistance, settings.distantCatchupStart,
                                          settings.distantCatchupEnd);
        const bool urgent = UrgentIndividualTravel(playerDistance, settings.distantCatchupEnd, f.goal.active,
                                                   Length({f.goal.goal.x - pos.x, f.goal.goal.y - pos.y}),
                                                   intent.RoutingLead());
        if (f.individual.Initialize(TravelIdentity(actor))) {
            const auto& p = f.individual.Disposition();
            logger::info("[IndividualTravel] {} reaction={:.2f}s turnDelay={:.2f}s turnRate={:.2f} "
                         "cruise={:.3f} catchGap={:.0f} settleGap={:.0f}",
                         actor.GetName(), p.reaction, p.turnDelay, p.turnSharpness, p.cruise, p.catchGap,
                         p.settleGap);
        }
        f.individual.Update(intent.Direction(), dt, intent.IsMoving(), urgent, interior,
                            settings.travel.individuality);
        const Vec2 actorXY{pos.x, pos.y};
        const bool ownsPackage = actor.GetCurrentPackage() == package;
        f.goal.Tick(actorXY, dt, ownsPackage, travelTuning);
        if (f.goal.recoveryTime > 0.0F || f.recoveryReleasePending) {
            if (f.travelActive) {
                logger::info("[Travel] {} stalled; yielding for recovery; activePackage={}", actor.GetName(),
                             DescribePackage(actor.GetCurrentPackage()));
                f.recoveryReleasePending = true;
            }

            if (f.recoveryReleasePending) {
                auto* alias = GetRefAlias(quest, FollowerAlias(f.slot));
                if (!alias || alias->GetReference() != &actor) {
                    f.recoveryReleasePending = false;
                    f.goal.recoveryTime = std::max(f.goal.recoveryTime, .5F);
                }
            }
            ReleaseTravel(f, &actor);
            return;
        }

        if (!f.travelActive && !handGuided && !f.individual.Ready(urgent, settings.travel.individuality)) {
            return;
        }
        const auto planned =
            PartyRole(mode, f.role >= 0 ? f.role : f.slot, travelPartyCount, naturalPatternSeed,
                      settings.travel.naturalStragglerDistance, actor.GetFormID());
        auto offset = planned.offset;
        if (mode != FormationMode::kCustom) {
            offset = PersonalOffset(offset, PersonalityFor(actor)); // Sociable companions keep closer.
        }
        const float setback = planned.straggler ? settings.travel.naturalStragglerDistance : 0;
        f.roleSetback =
            f.haveRoleOffset ? BlendRoleOffset({0, f.roleSetback}, {0, setback}, dt).longitudinal : setback;
        f.roleOffset = f.haveRoleOffset ? BlendRoleOffset(f.roleOffset, offset, dt) : offset;
        f.haveRoleOffset = true;
        const float rearBlend =
            std::clamp(f.roleSetback / std::max(1.0F, settings.travel.naturalStragglerDistance), 0.0F, 1.0F);
        const auto personalIntent = intent.WithDirection(f.individual.Direction());
        Vec2 desired = personalIntent.GoalAt(mode, f.roleOffset, interior,
                                             mode == FormationMode::kCustom ? 1 : settings.preferredSide,
                                             settings.travel, f.roleSetback, rearBlend);
        const auto direction = classicHands
                                   ? HandHolding::LocomotionDirection(handAttached, handApproach.heading,
                                                                      {handVelocity.x, handVelocity.y},
                                                                      ForwardFromYaw(player.GetAngleZ()))
                               : handGuided ? ForwardFromYaw(player.GetAngleZ())
                                            : personalIntent.Direction();
        const float handLead = HandHolding::LocomotionLead(handSpeed);
        if (handGuided) {
            desired = handHoldPosition ? Vec2{handApproach.position.x, handApproach.position.y}
                                       : HandHolding::ApproachPoint({playerPos.x, playerPos.y},
                                                                    player.GetAngleZ(), handApproach.side);
        }
        if (classicHands) {
            const auto point =
                HandHolding::SeekPoint({playerPos.x, playerPos.y, playerPos.z}, player.GetAngleZ(),
                                       handApproach.side, handApproach.spacing);
            desired = {point.x, point.y};
        }
        if (handLocomotion) {
            desired = {pos.x + direction.x * handLead, pos.y + direction.y * handLead};
        }
        const Vec2 ordinaryAim = handGuided ? desired : personalIntent.RouteGoal(desired, mode, interior);
        const Vec2 rearAim =
            handGuided ? desired : personalIntent.RouteGoal(desired, FormationMode::kRear, interior);
        const Vec2 aim{ordinaryAim.x + (rearAim.x - ordinaryAim.x) * rearBlend,
                       ordinaryAim.y + (rearAim.y - ordinaryAim.y) * rearBlend};
        const float lead = Length({aim.x - desired.x, aim.y - desired.y});
        ObstaclePlan corridor;
        auto target = handHoldPosition
                          ? navigation.AttachmentPoint(pos, {handApproach.position.x, handApproach.position.y,
                                                             handApproach.position.z})
                      : handLocomotion
                          ? HandHolding::LocomotionTarget(
                                pos, direction, handLead,
                                [&](RE::NiPoint3 from, Vec2 goal, Vec2 facing) {
                                    return navigation.Resolve(from, goal, facing);
                                },
                                [&](RE::NiPoint3 from) { return navigation.AttachmentPoint(from, from); })
                      : settings.forwardCollision && !handGuided
                          ? navigation.ResolveClear(player, aim, direction, corridor)
                          : navigation.Resolve(playerPos, aim, direction);
        bool connectedRoute = false;
        if (settings.forwardCollision && !handGuided) {
            const auto searchDirection =
                mode == FormationMode::kLead
                    ? direction
                    : Normalize({aim.x - playerPos.x, aim.y - playerPos.y}, direction);
            const float advance = target ? (target->x - playerPos.x) * searchDirection.x +
                                               (target->y - playerPos.y) * searchDirection.y
                                         : 0;
            const float requestedAdvance =
                (aim.x - playerPos.x) * searchDirection.x + (aim.y - playerPos.y) * searchDirection.y;
            if (requestedAdvance >= 160 && advance < std::clamp(requestedAdvance * .7F, 110.0F, 240.0F)) {
                if (auto ahead = navigation.ResolveConnected(player, aim, searchDirection)) {
                    target = ahead;
                    connectedRoute = true;
                    if (settings.traceMovement && traceDue) {
                        logger::info("[Corridor] {} uses connected navmesh destination beyond bend "
                                     "({:.0f},{:.0f},{:.0f})",
                                     actor.GetName(), ahead->x, ahead->y, ahead->z);
                    }
                }
            }
        }
        if (settings.traceMovement && traceDue && settings.forwardCollision && !handGuided) {
            logger::info("[Corridor] {} interior={} desired=({:.0f},{:.0f}) usable={} blocker={:08X} "
                         "rays={} navRejects={} queryFailed={}",
                         actor.GetName(), interior, aim.x, aim.y, target.has_value(), corridor.blocker,
                         corridor.rays, corridor.navRejects, corridor.queryFailed);
            logger::info("[CorridorPosition] {} cell={:08X} actor=({:.0f},{:.0f},{:.0f}) "
                         "player=({:.0f},{:.0f},{:.0f}) target=({:.0f},{:.0f},{:.0f}) connected={}",
                         actor.GetName(), player.GetParentCell()->GetFormID(), pos.x, pos.y, pos.z,
                         playerPos.x, playerPos.y, playerPos.z, target ? target->x : aim.x,
                         target ? target->y : aim.y, target ? target->z : playerPos.z, connectedRoute);
        }
        if (!target) {
            LogDiagnostic(actor.GetFormID(), actor.GetName(), "no usable travel corridor; yielding");
            ReleaseTravel(f, &actor);
            f.goal.recoveryTime = 1.0F;
            return;
        }
        bool obstacleChanged = false;
        const bool localObstacles =
            settings.forwardCollision &&
            UseLocalObstacleSteering(!interior && ContextFor(player) == RestContext::Wilderness &&
                                         ContextFor(actor) == RestContext::Wilderness,
                                     handGuided, f.distantCatchup, playerDistance);
        if (localObstacles && !connectedRoute) {
            RE::NiPoint3 velocity{};
            actor.GetLinearVelocity(velocity);
            auto avoidance = navigation.AvoidObstacles(
                actor, *target, std::max(intent.Speed(), std::hypot(velocity.x, velocity.y)), dt,
                travelTuning.arrivalRadius, f.avoidance);
            obstacleChanged = avoidance.changed;
            if (avoidance.route == ObstacleRoute::Blocked) {
                logger::info(
                    "[Obstacle] {} ({:08X}): local corridor unavailable; retaining native travel route at "
                    "({:.0f}, {:.0f}, {:.0f}); blocker={:08X} rays={} navRejects={} queryFailed={}",
                    actor.GetName(), actor.GetFormID(), pos.x, pos.y, pos.z, avoidance.blocker,
                    avoidance.rays, avoidance.navRejects, avoidance.queryFailed);

                avoidance = f.avoidance.UseNative({target->x, target->y, target->z});
                obstacleChanged = true;
            }
            target = RE::NiPoint3{avoidance.target.x, avoidance.target.y, avoidance.target.z};
            if (obstacleChanged || (traceDue && settings.traceMovement && f.avoidance.active)) {
                logger::info("[Obstacle] {} ({:08X}): route={} side={} speed={:.0f} actor=({:.0f}, {:.0f}, "
                             "{:.0f}) target=({:.0f}, {:.0f}, {:.0f}) blocker={:08X} rays={} navRejects={}",
                             actor.GetName(), actor.GetFormID(),
                             avoidance.route == ObstacleRoute::Native ? "native"
                             : f.avoidance.active                     ? "detour"
                                                                      : "clear",
                             f.avoidance.side, std::hypot(velocity.x, velocity.y), pos.x, pos.y, pos.z,
                             target->x, target->y, target->z, avoidance.blocker, avoidance.rays,
                             avoidance.navRejects);
            }
        }
        else {
            obstacleChanged = f.avoidance.active;
            f.avoidance.Reset();
        }
        auto* marker = EnsureMarker(f, player);
        if (!marker) {
            ReleaseTravel(f, &actor);
            return;
        }
        if (settings.conversationAwareness) {
            PrepareAuxMarker(f, actor, pos);
        }
        const Vec2 candidate{target->x, target->y};
        const float distance = Length({candidate.x - pos.x, candidate.y - pos.y});

        const float routedLead = std::clamp(
            (target->x - desired.x) * direction.x + (target->y - desired.y) * direction.y, 0.0F, lead);
        const float paceDistance =
            f.avoidance.active ? std::min(distance, 100.0F) : std::max(0.0F, distance - routedLead);
        const bool immediateHandPace = classicHands || handHandoff;
        auto pace =
            ChoosePace(immediateHandPace ? handSpeed : intent.Speed(), handLocomotion ? 0 : paceDistance,
                       f.goal.pace, immediateHandPace ? handSpeed > 5 : intent.IsMoving());
        if (f.avoidance.active && pace == TravelPace::kRun) {
            pace = TravelPace::kJog;
        }
        const bool holdRepath = handHoldPosition && HandHolding::RepathArrivalHold(f.goal, candidate);
        const bool handoffRepath = handHandoff && HandHolding::RepathHandoffTravel(f.goal, candidate);
        if (classicHands || holdRepath || handoffRepath || obstacleChanged ||
            f.goal.ShouldCommit(candidate, actorXY, pace, travelTuning, intent.Speed(), intent.IsMoving())) {

            package->packData.packFlags.set(RE::PACKAGE_DATA::GeneralFlag::kPreferredSpeed);
            package->packData.maxSpeed = static_cast<RE::PACKAGE_DATA::PreferredSpeed>(pace);
            if (marker->GetParentCell() != player.GetParentCell()) {
                marker->MoveTo(&player);
            }
            marker->SetPosition({target->x, target->y, target->z});
            f.goal.Commit(candidate, actorXY, pace);
            f.travelActive = true;
        }
        if (holdRepath || handoffRepath ||
            (handLocomotion
                 ? HandHolding::RepathAttached(f.goal, actorXY, handSpeed, ownsPackage, travelTuning)
                 : f.goal.ShouldRepath(actorXY, intent.Speed(), intent.IsMoving(), ownsPackage,
                                       travelTuning)) ||
            (obstacleChanged && ownsPackage && f.goal.sinceRepath >= .30F)) {
            auto* followerAlias = GetRefAlias(quest, FollowerAlias(f.slot));
            if (followerAlias && followerAlias->GetReference() == &actor) {

                marker->SetPosition({target->x, target->y, target->z});
                package->packData.maxSpeed = static_cast<RE::PACKAGE_DATA::PreferredSpeed>(pace);
                f.goal.Commit(candidate, actorXY, pace);

                actor.EvaluatePackage(false, true);
                if (settings.traceMovement && f.goal.stalledTime >= 1.0F) {
                    logger::info("[TravelRetry] {} stalled={:.1f} routeAge={:.2f} ownsPackage={}",
                                 actor.GetName(), f.goal.stalledTime, f.goal.sinceRepath, ownsPackage);
                }
                f.goal.MarkRepathed();
            }
        }
        if (f.travelActive) {

            const auto facing = ForwardFromYaw(actor.GetAngle().z);
            const auto towardGoal = Normalize({candidate.x - pos.x, candidate.y - pos.y}, facing);
            const float alignment = facing.x * towardGoal.x + facing.y * towardGoal.y;
            const auto speedPace = f.goal.hasRoute ? f.goal.submittedPace : f.goal.pace;
            const float normalScale =
                f.individual.SpeedScale(intent.Speed(), paceDistance, speedPace, intent.IsMoving(),
                                        settings.travel, alignment, urgent);
            float desiredScale = TravelSpeedTarget(normalScale, settings.travel.maxSpeedScale,
                                                   f.distantCatchup, Distance2D(pos, playerPos), paceDistance,
                                                   settings.distantCatchupStart, settings.distantCatchupEnd);
            if (classicHands) {
                const float nominal = pace == TravelPace::kRun   ? 335.0F
                                      : pace == TravelPace::kJog ? 245.0F
                                                                 : 100.0F;
                const float seekSpeed =
                    std::max(100.0F, handSpeed) + std::clamp(paceDistance * .8F, 0.0F, 180.0F);
                desiredScale = std::clamp((handLocomotion ? handSpeed : seekSpeed) / nominal, .65F, 3.5F);
            }
            if (f.avoidance.active) {
                desiredScale = std::min(desiredScale, 1.15F);
            }
            f.speedScale = classicHands ? desiredScale : SmoothSpeedScale(f.speedScale, desiredScale, dt);

            f.speedScale = std::min(f.speedScale, desiredScale + .12F);
            TravelSpeed::Set(static_cast<std::uint32_t>(f.slot), actor.GetFormID(), f.speedScale,
                             classicHands ? 3.5F : settings.travel.maxSpeedScale, settings.distantCatchupEnd);
        }
        if (settings.traceMovement && traceDue) {
            if (handGuided) {
                logger::info("[HandHolding/Approach] {} target=({:.1f},{:.1f}) remaining={:.1f} "
                             "radius={:.0f} ownsPackage={} classic={} arrivalHold={} handoffTravel={}",
                             actor.GetName(), candidate.x, candidate.y, distance, travelTuning.arrivalRadius,
                             ownsPackage, classicHands, handHoldPosition, handHandoff);
            }
            RE::NiPoint3 velocity{};
            actor.GetLinearVelocity(velocity);
            const auto forward = ForwardFromYaw(actor.GetAngle().z);
            auto* alias = GetRefAlias(quest, FollowerAlias(f.slot));
            const float forwardSpeed = velocity.x * forward.x + velocity.y * forward.y;
            logger::info(
                "[Travel] {} slot={} phase={} pace={} speed={:.0f} forwardSpeed={:.0f} goalDistance={:.0f} "
                "alias={} ownPackage={} stalled={:.1f} playerSpeed={:.0f} speedScale={:.2f} "
                "pathDistance={:.0f} targetAge={:.2f} routeAge={:.2f} steeringLead={:.0f} distantCatchup={}",
                actor.GetName(), f.slot, static_cast<int>(intent.Phase()), static_cast<int>(f.goal.pace),
                Length({velocity.x, velocity.y}), forwardSpeed, distance,
                alias && alias->GetReference() == &actor, ownsPackage, f.goal.stalledTime, intent.Speed(),
                f.speedScale, Length({f.goal.submittedGoal.x - pos.x, f.goal.submittedGoal.y - pos.y}),
                f.goal.sinceCommit, f.goal.sinceRepath, routedLead, f.distantCatchup);
            const auto right = RightFromForward(direction);
            logger::info("[IndividualPace] {} catchingUp={} heading=({:.2f},{:.2f}) strength={:.2f}",
                         actor.GetName(), f.individual.CatchingUp(), direction.x, direction.y,
                         settings.travel.individuality);
            logger::info(
                "[Spacing] {} role={} scale={:.2f} desiredSide={:.0f} groundSide={:.0f} straggler={}",
                actor.GetName(), f.role, settings.travel.spacing,
                (desired.x - playerPos.x) * right.x + (desired.y - playerPos.y) * right.y,
                (target->x - playerPos.x) * right.x + (target->y - playerPos.y) * right.y, planned.straggler);
            if (!ownsPackage) {
                logger::info("[TravelOwner] {} expected={:08X} active={}", actor.GetName(),
                             package->GetFormID(), DescribePackage(actor.GetCurrentPackage()));
            }
        }
    }

    void FormationController::SetSandboxFlag(bool active)
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* flag =
            data ? data->LookupForm<RE::TESGlobal>(SANDBOX_GLOBAL, Settings::GetSingleton().Get().pluginName)
                 : nullptr;
        if (flag) {
            flag->value = active ? 1.0F : 0.0F;
        }
    }

    void FormationController::SetPoseFlag(int slot, int kind)
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* flag = data ? data->LookupForm<RE::TESGlobal>(PoseGlobalLocal(slot),
                                                            Settings::GetSingleton().Get().pluginName)
                          : nullptr;
        if (flag) {
            flag->value = static_cast<float>(kind);
        }
    }

    void FormationController::EndRestPose(ManagedFollower& f, RE::Actor* actor)
    {
        if (f.restPose.kind == 1 && f.restPose.started && actor && actor->Is3DLoaded() && !actor->IsDead() &&
            !actor->IsInCombat() && !actor->GetCurrentScene() && !IsSpeaking(*actor) &&
            !ShouldSuspendForDialogue()) {
            if (f.activity == RestActivity::Kneel || f.activity == RestActivity::WarmHandsCrouched) {
                // Crouched loops stand up through the furniture-style exit when they have one.
                if (!actor->NotifyAnimationGraph("IdleChairExitStart")) {
                    actor->NotifyAnimationGraph("IdleStop");
                }
                f.poseExitCheck = 3.5F;
            }
            else {
                actor->NotifyAnimationGraph("IdleStop");
            }
        }
        if (f.restPose.kind == 2 && f.activity == RestActivity::TableSeat) {
            f.tableSeat = {};
            f.tableIdle = 0;
            f.tableIdleActive = false;
            f.tableLook = 0;
            f.tableRest = 60; // Stretch their legs before sitting back down.
        }
        if (f.restPose.kind) {
            f.restPose.End(actor ? actor->GetFormID() : static_cast<std::uint32_t>(f.slot));
            if (f.activity == RestActivity::Eat || f.activity == RestActivity::Drink) {
                f.restPose.cooldown += 30;
            }
        }
        SetPoseFlag(f.slot, 0);
        f.restIdleRequested = false;
    }

    RE::TESObjectREFR* FormationController::GetRestSeat(std::uint32_t slot)
    {
        std::lock_guard lock(stateMutex);
        if (slot >= PARTY_CAPACITY) {
            return nullptr;
        }
        for (auto& [id, f] : managed) {
            if (f.slot == static_cast<int>(slot) && f.tableSeat.get()) {
                return f.tableSeat.get().get(); // The chair at the inn table.
            }
        }
        const auto& tuning = Settings::GetSingleton().Get().sandbox;
        if (!tuning.extraPoses && !tuning.areaActivities) {
            return nullptr;
        }
        return seatPool[slot].get().get();
    }

    RE::TESObjectREFR* FormationController::SeatFor(ManagedFollower& f)
    {
        if (auto chair = f.tableSeat.get()) {
            return chair.get();
        }
        return f.slot >= 0 ? seatPool[static_cast<std::size_t>(f.slot)].get().get() : nullptr;
    }

    RE::TESObjectREFR* FormationController::EnsureRestSeat(ManagedFollower& f, RE::Actor& actor,
                                                           RE::NiPoint3 point, float yaw)
    {
        auto& pooled = seatPool[f.slot];
        if (!pooled.get()) {
            if (auto* alias = GetRefAlias(EnsureQuest(), SeatAlias(f.slot))) {
                auto* saved = alias->GetReference();
                if (saved && !saved->IsDeleted() && saved->GetBaseObject() &&
                    saved->GetBaseObject()->GetFormID() == 0x108D3C) {
                    pooled = saved->GetHandle();
                }
            }
        }
        if (auto ref = pooled.get()) {
            bool occupied = false;
            if (auto* lists = RE::ProcessLists::GetSingleton()) {
                Engine::ForEachHighActor(*lists, [&](RE::Actor& other) {
                    if (other.GetOccupiedFurniture().get().get() == ref.get()) {
                        occupied = true;
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
            if (occupied) {
                return nullptr;
            }
            ref->MoveTo(&actor);
            ref->SetPosition(point);
            Engine::SetReferenceAngle(*ref, {0, 0, yaw}); // They sit the way the marker faces.
            return ref.get();
        }
        auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(0x108D3C);
        if (!base) {
            return nullptr;
        }
        pooled = RE::TESDataHandler::GetSingleton()->CreateReferenceAtLocation(
            base, point, RE::NiPoint3{0, 0, yaw}, actor.GetParentCell(), actor.GetWorldspace(), nullptr, nullptr,
            RE::ObjectRefHandle{}, true, false);
        return pooled.get().get();
    }

    void FormationController::UpdateRestPose(ManagedFollower& f, RE::Actor& actor, float dt)
    {
        if (f.poseExitCheck > 0) {
            // Nobody walks off frozen in a crouch or kneel the exit event did not end.
            f.poseExitCheck -= dt;
            bool playing = false;
            if (f.poseExitCheck <= 0 && !f.restPose.kind && !actor.GetOccupiedFurniture().get() &&
                !IsSpeaking(actor) && !actor.GetCurrentScene() &&
                actor.GetGraphVariableBool("bIdlePlaying", playing) && playing) {
                actor.NotifyAnimationGraph("IdleForceDefaultState");
                logger::info("[RestPose] {} was still posed after standing; cleared it", actor.GetName());
            }
        }
        if (f.restRecovery.walking || lookout.actor == actor.GetFormID()) {
            f.poseTurn = false;
            return;
        }
        const auto& tuning = Settings::GetSingleton().Get().sandbox;
        const auto context = ContextFor(actor);
        const bool refreshments = SettledRefreshments(context, tuning);
        if (!AnyRestPoses(context, tuning)) {
            if (f.restPose.kind) {
                EndRestPose(f, &actor);
            }
            f.poseTurn = false;
            return;
        }
        auto& pose = f.restPose;
        const auto id = actor.GetFormID();
        pose.Init(id);
        auto* state = actor.AsActorState();
        // Seated companions may talk; everyone else stops posing to speak.
        if (!state || actor.IsInCombat() || actor.GetCurrentScene() || (IsSpeaking(actor) && pose.kind != 2) ||
            state->IsWeaponDrawn() || actor.IsSneaking() || state->IsSwimming() || actor.IsOnMount()) {
            EndRestPose(f, &actor);
            f.poseTurn = false;
            return;
        }
        auto* data = RE::TESDataHandler::GetSingleton();
        const auto& plugin = Settings::GetSingleton().Get().pluginName;
        if (pose.kind) {
            auto* package = data->LookupForm<RE::TESPackage>(
                pose.kind == 2 ? SeatPackageLocal(f.slot) : PosePackageLocal(f.slot), plugin);
            bool ready = actor.GetCurrentPackage() == package;
            if (pose.kind == 2) {
                auto* seat = SeatFor(f);
                ready = ready && seat && actor.GetOccupiedFurniture().get().get() == seat &&
                        state->GetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSitting;
            }
            else if (!pose.started) {
                ready = false;
            }
            const bool wasStarted = pose.started;
            if (pose.Tick(dt, ready)) {
                if (!wasStarted) {
                    logger::info("[RestPose] {} entry unavailable; returning to sandbox", actor.GetName());
                }
                EndRestPose(f, &actor);
                return;
            }
            if (pose.started && !wasStarted) {
                logger::info("[RestPose] {} kind={} holding for {:.0f}s", actor.GetName(), pose.kind,
                             pose.duration);
            }
            return;
        }

        if (f.socialHold || social.Involves(id)) {
            pose.cooldown = 12.0F + RestPose::Mix(id + pose.sequence) % 13;
            f.poseTurn = false;
            return;
        }
        pose.Tick(dt, false);
        if (pose.cooldown > 0 || social.Involves(id) || actor.GetOccupiedFurniture().get() ||
            state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) {
            return;
        }
        auto* npc = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13794);
        if (!actor.GetRace() || !npc || !actor.GetRace()->HasKeyword(npc)) {
            return;
        }
        if (actor.GetCurrentPackage() !=
            data->LookupForm<RE::TESPackage>(SandboxPackageLocal(f.slot), plugin)) {
            return;
        }
        RE::NiPoint3 velocity{};
        actor.GetLinearVelocity(velocity);
        if (Length({velocity.x, velocity.y}) > 12) {
            return;
        }
        bool driven = false;
        actor.GetGraphVariableBool("bAnimationDriven", driven);
        if (driven || f.patronTimer > 0) {
            return;
        }
        if (f.poseTurn) {
            // Facing the fire or shrine first; the pose was already chosen.
            if (f.feature != AreaFeature::None && Distance2D(actor.GetPosition(), f.featurePoint) > 420) {
                f.poseTurn = false;
                f.feature = AreaFeature::None;
                return;
            }
            if (TurnToward(actor, f.poseFacePoint, dt)) {
                f.poseTurn = false;
                BeginRestPose(f, actor, 1, f.feature);
            }
            return;
        }

        const auto place = PlaceFor(actor);
        // A Relax order settles in; temporary rest is a short stop and keeps to brief standing poses.
        const bool settled = IsRelaxing();
        const auto roll = RestPose::Mix(id + pose.sequence * 3571);
        auto at = AreaFeature::None;
        if (f.pendingFeature != AreaFeature::None &&
            Distance2D(actor.GetPosition(), f.pendingFeaturePoint) > (FacesFeature(f.pendingFeature) ? 420.0F : 720.0F)) {
            f.pendingFeature = AreaFeature::None; // Not actually there.
            f.pendingFeatureRef = {};
        }
        if (f.pendingFeature != AreaFeature::None) {
            // Just arrived at a fire, shrine, shore or lookout.
            at = f.feature = std::exchange(f.pendingFeature, AreaFeature::None);
            f.featureRef = std::exchange(f.pendingFeatureRef, {});
            f.featurePoint = f.pendingFeaturePoint;
            f.activity = FeatureActivity(at, place, roll, tuning, settled);
        }
        else if (tuning.areaActivities && f.feature != AreaFeature::None &&
                 Distance2D(actor.GetPosition(), f.featurePoint) < (FacesFeature(f.feature) ? 320.0F : 720.0F) &&
                 roll % 4 != 0) {
            at = f.feature;
            f.activity = FeatureFollowUp(at, place, roll >> 3, tuning, settled);
        }
        else {
            f.feature = AreaFeature::None;
            f.featureRef = {};
            f.activity = ChoosePlaceActivity(place, context, roll, tuning, TraitsFor(actor), settled);
        }
        if ((f.activity == RestActivity::Eat || f.activity == RestActivity::Drink) && refreshments &&
            std::count_if(managed.begin(), managed.end(), [](const auto& entry) {
                return entry.second.restPose.kind && (entry.second.activity == RestActivity::Eat ||
                                                      entry.second.activity == RestActivity::Drink);
            }) >= 2) {
            ++pose.sequence;
            pose.cooldown = 8;
            return;
        }
        int kind = 1;
        if (f.activity == RestActivity::Ground) {
            auto point = navigation.RestPoint(actor.GetPosition());
            float water{};
            if (point && !(actor.GetParentCell()->GetWaterHeight(*point, water) && water >= point->z - 2)) {
                bool clear = true;
                for (auto& [otherID, other] : managed) {
                    auto otherActor = other.handle.get();
                    if (otherID != id && otherActor && Distance2D(otherActor->GetPosition(), *point) < 100) {
                        clear = false;
                        break;
                    }
                }
                // By a fire, sit facing it; otherwise the way they already face.
                const float yaw = at == AreaFeature::Fire ? std::atan2(f.featurePoint.x - point->x,
                                                                       f.featurePoint.y - point->y)
                                                          : actor.GetAngleZ();
                if (clear && EnsureRestSeat(f, actor, *point, yaw)) {
                    kind = 2;
                }
            }
        }
        if (!data->LookupForm<RE::TESGlobal>(PoseGlobalLocal(f.slot), plugin) ||
            !data->LookupForm<RE::TESPackage>(kind == 2 ? SeatPackageLocal(f.slot) : PosePackageLocal(f.slot),
                                              plugin)) {
            return;
        }
        if (kind != 2 && f.activity == RestActivity::Ground) {
            if (at == AreaFeature::Fire) {
                f.activity = RestActivity::WarmHandsCrouched; // No room to sit: crouch at the fire instead.
            }
            else {
                ++pose.sequence;
                pose.cooldown = 15;
                return;
            }
        }
        if (kind == 1 && at != AreaFeature::None) {
            // Face the fire or shrine; shore and lookout spots store a point out over the view.
            f.poseFacePoint = f.featurePoint;
            if (!TurnToward(actor, f.poseFacePoint, dt)) {
                f.poseTurn = true;
                return;
            }
        }
        BeginRestPose(f, actor, kind, at);
    }

    void FormationController::SetSocialFlag(int slot, int phase)
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* flag = data ? data->LookupForm<RE::TESGlobal>(SocialGlobalLocal(slot),
                                                            Settings::GetSingleton().Get().pluginName)
                          : nullptr;
        if (flag) {
            flag->value = static_cast<float>(phase);
        }
    }

    void FormationController::EndConversationAwareness()
    {
        if (!conversationSpeaker) {
            return;
        }
        for (auto& [id, f] : managed) {
            if (f.awarenessActive) {
                SetSocialFlag(f.slot, f.awarenessPreviousPhase);
                if (auto actor = f.handle.get(); actor && !actor->GetCurrentScene() && !IsSpeaking(*actor)) {
                    auto* stroll = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                        0x870 + f.slot, Settings::GetSingleton().Get().pluginName);
                    if (actor->GetCurrentPackage() == stroll) {
                        actor->EvaluatePackage(false, true);
                    }
                }
            }
            f.awarenessActive = f.awarenessWalking = f.awarenessTried = false;
            f.awarenessTime = 0;
        }
        conversationSpeaker = 0;
        awarenessUpdateTimer = 0;
    }

    void FormationController::UpdateConversationAwareness(RE::PlayerCharacter& player, float dt)
    {
        const auto& settings = Settings::GetSingleton().Get();
        auto* ui = RE::UI::GetSingleton();
        auto* topics = RE::MenuTopicManager::GetSingleton();
        if (!enabled || !settings.conversationAwareness || !ui || ui->GameIsPaused() ||
            !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) || !topics) {
            return;
        }
        auto speakerRef = topics->speaker.get();
        auto* speaker = speakerRef ? speakerRef->As<RE::Actor>() : nullptr;
        if (!speaker || speaker->IsPlayerRef() || !speaker->Is3DLoaded() ||
            !InSameTravelSpace(*speaker, player)) {
            return;
        }

        conversationSpeaker = speaker->GetFormID();
        awarenessUpdateTimer += dt;
        if (awarenessUpdateTimer < .2F) {
            return;
        }
        const float elapsed = awarenessUpdateTimer;
        awarenessUpdateTimer = 0;
        const auto p = player.GetPosition(), s = speaker->GetPosition();
        if (Distance2D(p, s) > 600) {
            return;
        }
        bool walking = false;
        for (auto& [id, f] : managed) {
            if (f.awarenessWalking) {
                if (id == speaker->GetFormID()) {
                    walking = true;
                    continue;
                }
                auto actor = f.handle.get();
                auto target = gatherPool[f.slot].get();
                if (!actor || !target) {
                    f.awarenessWalking = false;
                    continue;
                }
                f.awarenessTime += elapsed;
                if (Distance2D(actor->GetPosition(), target->GetPosition()) < 75) {
                    f.awarenessWalking = false;
                }
                else if (f.awarenessTime >= 15 && !actor->GetCurrentScene() && !IsSpeaking(*actor)) {
                    SetSocialFlag(f.slot, f.awarenessPreviousPhase);
                    f.awarenessActive = f.awarenessWalking = false;
                    auto* stroll = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                        0x870 + f.slot, settings.pluginName);
                    if (actor->GetCurrentPackage() == stroll) {
                        actor->EvaluatePackage(false, true);
                    }
                    logger::info("[ConversationSpace] {} could not clear the path; yielding",
                                 actor->GetName());
                }
                walking |= f.awarenessWalking;
            }
        }
        if (walking) {
            return;
        }
        navigation.Collect(player);
        for (auto& [id, f] : managed) {
            if (id == speaker->GetFormID() || f.awarenessTried || !f.travelActive || f.restPose.kind ||
                f.restRecovery.walking || social.Involves(id) || lookout.actor == id) {
                continue;
            }
            auto actor = f.handle.get();
            auto target = gatherPool[f.slot].get();
            if (!actor || !target || !actor->Is3DLoaded() || !InSameTravelSpace(*actor, player) ||
                actor->IsDead() || actor->IsInCombat() || actor->GetCurrentScene() ||
                actor->GetOccupiedFurniture().get() || IsSpeaking(*actor)) {
                continue;
            }
            auto* state = actor->AsActorState();
            bool driven = false;
            actor->GetGraphVariableBool("bAnimationDriven", driven);
            if (!state || driven || state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal ||
                state->IsSwimming() || state->IsWeaponDrawn() || actor->IsOnMount()) {
                continue;
            }
            const auto pos = actor->GetPosition();
            if (std::abs(pos.z - p.z) > 150 || !IntrudesConversation({pos.x, pos.y}, {p.x, p.y}, {s.x, s.y},
                                                                     settings.conversationClearance)) {
                continue;
            }

            auto* quest = questCache;
            if (!quest || !quest->IsRunning()) {
                continue;
            }
            auto* followerAlias = GetRefAlias(quest, FollowerAlias(f.slot));
            auto* destinationAlias = GetRefAlias(quest, 30 + f.slot);
            if (!followerAlias || followerAlias->GetReference() != actor.get() || !destinationAlias ||
                destinationAlias->GetReference() != target.get()) {
                continue;
            }
            auto* data = RE::TESDataHandler::GetSingleton();
            auto* current = actor->GetCurrentPackage();
            auto* travel = data->LookupForm<RE::TESPackage>(PackageLocal(f.slot), settings.pluginName);
            auto* rest = data->LookupForm<RE::TESPackage>(SandboxPackageLocal(f.slot), settings.pluginName);
            auto* stroll = data->LookupForm<RE::TESPackage>(0x870 + f.slot, settings.pluginName);
            if ((current != travel && current != rest) || !stroll) {
                continue;
            }
            f.awarenessTried = true;
            const auto desired =
                ConversationExit({pos.x, pos.y}, {p.x, p.y}, {s.x, s.y}, settings.conversationClearance, id);
            auto point = navigation.Resolve(pos, desired, Normalize({desired.x - pos.x, desired.y - pos.y}));
            if (!point || IntrudesConversation({point->x, point->y}, {p.x, p.y}, {s.x, s.y},
                                               settings.conversationClearance + 40)) {
                continue;
            }
            float water{};
            if (actor->GetParentCell()->GetWaterHeight(*point, water) && water >= point->z - 2) {
                continue;
            }
            bool clear = true;
            for (const auto& [otherID, other] : managed) {
                if (otherID != id) {
                    if (auto otherActor = other.handle.get();
                        otherActor && Distance2D(otherActor->GetPosition(), *point) < 100) {
                        clear = false;
                        break;
                    }
                }
            }
            if (!clear) {
                continue;
            }
            target->MoveTo(actor.get());
            target->SetPosition(*point);
            stroll->packData.packFlags.set(RE::PACKAGE_DATA::GeneralFlag::kPreferredSpeed);
            stroll->packData.maxSpeed = RE::PACKAGE_DATA::PreferredSpeed::kWalk;
            f.awarenessPreviousPhase = f.socialPhase;
            f.awarenessActive = f.awarenessWalking = true;
            f.awarenessTime = 0;
            SetSocialFlag(f.slot, 2);
            actor->EvaluatePackage(false, true);
            logger::info("[ConversationSpace] {} steps aside; protected speaker={:08X}", actor->GetName(),
                         speaker->GetFormID());
            break;
        }
    }

    void FormationController::UpdateLookout(RE::PlayerCharacter& player, float dt)
    {
        const auto& settings = Settings::GetSingleton().Get();
        const auto context = ContextFor(player);
        int present = 0;
        std::vector<WatchCandidate> candidates;
        for (const auto& [id, f] : managed) {
            if (auto actor = f.handle.get(); actor && f.sandboxActive && IsActorUsable(*actor, player)) {
                auto* npc = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13794);
                if (!actor->GetRace() || !npc || !actor->GetRace()->HasKeyword(npc)) {
                    continue;
                }
                ++present;
                auto* state = actor->AsActorState();
                RE::NiPoint3 motion{};
                actor->GetLinearVelocity(motion);
                bool driven = false;
                actor->GetGraphVariableBool("bAnimationDriven", driven);
                if (!state || actor->IsInCombat() || actor->IsDead() || state->IsWeaponDrawn() ||
                    actor->IsSneaking() || actor->IsOnMount() || state->IsSwimming() ||
                    actor->GetOccupiedFurniture().get() ||
                    state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal || actor->GetCurrentScene() ||
                    IsSpeaking(*actor) || f.restPose.kind || f.restRecovery.walking || social.Involves(id) ||
                    f.patronTimer > 0 || Length({motion.x, motion.y}) > 12 || driven) {
                    continue;
                }
                auto* current = actor->GetCurrentPackage();
                auto* expected = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                    id == lookout.actor ? PosePackageLocal(f.slot) : SandboxPackageLocal(f.slot),
                    settings.pluginName);
                auto* native = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                    SandboxPackageLocal(f.slot), settings.pluginName);
                if (current != expected && !(id == lookout.actor && current == native)) {
                    continue;
                }
                candidates.push_back({id, WatchWeight(PersonalityFor(*actor))});
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](auto a, auto b) { return a.id < b.id; });
        const auto previous = lookout.actor;
        lookout.Update(dt,
                       LookoutPermitted(settings.lookouts, sandbox.active, sandbox.expanded,
                                        context == RestContext::Wilderness || context == RestContext::Dungeon,
                                        present, player.IsInCombat()),
                       sandbox.activeTime, settings.lookoutAfter, candidates);
        if (previous && previous != lookout.actor) {
            if (auto found = managed.find(previous); found != managed.end()) {
                SetPoseFlag(found->second.slot, found->second.restPose.kind);
                logger::info("[Lookout] {:08X} finished watching", previous);
            }
        }
        if (lookout.actor && previous != lookout.actor) {
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(lookout.actor)) {
                const auto pos = actor->GetPosition();
                const auto p = player.GetPosition();
                const auto facing =
                    Normalize({pos.x - p.x, pos.y - p.y}, ForwardFromYaw(actor->GetAngle().z));
                Engine::SetHeading(*actor, std::atan2(facing.x, facing.y));
                logger::info("[Lookout] {} keeping watch for {:.0f}s", actor->GetName(), lookout.duration);
            }
        }
    }

    RE::TESObjectREFR* FormationController::PrepareAuxMarker(ManagedFollower& f, RE::Actor& actor,
                                                             RE::NiPoint3 point, bool move)
    {
        auto& pooled = gatherPool[f.slot];
        if (!pooled.get()) {
            if (auto* alias = GetRefAlias(EnsureQuest(), 30 + f.slot)) {
                auto* saved = alias->GetReference();
                if (saved && !saved->IsDeleted() && saved->GetBaseObject() &&
                    saved->GetBaseObject()->GetFormID() == 0x3B) {
                    pooled = saved->GetHandle();
                }
            }
        }
        if (!pooled.get()) {
            auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(0x3B);
            if (!base) {
                return nullptr;
            }
            pooled = RE::TESDataHandler::GetSingleton()->CreateReferenceAtLocation(
                base, point, RE::NiPoint3{}, actor.GetParentCell(), actor.GetWorldspace(), nullptr, nullptr,
                RE::ObjectRefHandle{}, true, false);
        }
        auto marker = pooled.get();
        if (!marker) {
            return nullptr;
        }
        if (move) {
            marker->MoveTo(&actor);
            marker->SetPosition(point);
        }
        return marker.get();
    }

    RE::NiPoint3 FormationController::RestCenter(const ManagedFollower& f, const RE::TESObjectREFR& anchor)
    {
        auto center = f.restHomeSet ? f.restHome : anchor.GetPosition();
        if (f.restAnchorZSet) {
            center.z = f.restAnchorZ;
        }
        return center;
    }

    void FormationController::ClearRestAnchor(ManagedFollower& f)
    {
        f.anchorSet = f.restAnchorZSet = f.restHomeSet = f.focused = f.poseTurn = false;
        f.feature = f.pendingFeature = AreaFeature::None;
        f.featureRef = f.pendingFeatureRef = {};
    }

    void FormationController::FocusRest(ManagedFollower& f, RE::Actor& actor, RE::TESObjectREFR& anchor,
                                        float radius)
    {
        anchor.MoveTo(&actor);
        anchor.SetPosition(actor.GetPosition());
        f.focused = true;
        f.focusRadius = radius;
    }

    int FormationController::FeatureUsers(RE::FormID self, RE::FormID feature) const
    {
        int users = 0;
        for (const auto& [id, other] : managed) {
            if (id == self) {
                continue;
            }
            auto at = other.featureRef.get();
            auto heading = other.restRecovery.walking ? other.pendingFeatureRef.get() : RE::NiPointer<RE::TESObjectREFR>{};
            users += (at && at->GetFormID() == feature) || (heading && heading->GetFormID() == feature);
        }
        return users;
    }

    void FormationController::BeginRestPose(ManagedFollower& f, RE::Actor& actor, int kind, AreaFeature at)
    {
        auto& pose = f.restPose;
        const auto id = actor.GetFormID();
        if (f.activity == RestActivity::Stand) {
            const bool occupied = std::any_of(managed.begin(), managed.end(), [](const auto& entry) {
                return entry.second.restPose.kind == 1 && entry.second.activity == RestActivity::Stand;
            });
            if (occupied || handsBackCooldown > 0) {
                ++pose.sequence;
                pose.cooldown = 12.0F + RestPose::Mix(id + pose.sequence) % 13;
                return;
            }
            handsBackCooldown = 90.0F + RestPose::Mix(id + pose.sequence) % 61;
        }
        pose.Begin(kind, id);
        f.restIdleRequested = false;
        pose.duration = RestPoseSeconds(f.activity, at, RestPose::Mix(id + pose.sequence * 31));
        SetPoseFlag(f.slot, kind);
        if (at != AreaFeature::None || f.activity >= RestActivity::LookFar) {
            logger::info("[AreaActivity] {} {}{}{} in the {}", actor.GetName(), RestActivityName(f.activity),
                         at != AreaFeature::None ? " at the " : "", at != AreaFeature::None ? FeatureName(at) : "",
                         PlaceName(PlaceFor(actor)));
        }
    }

    std::optional<RE::NiPoint3> FormationController::FindRestActivity(ManagedFollower& f, RE::Actor& actor,
                                                                      RE::TESObjectREFR& anchor)
    {
        auto* cell = actor.GetParentCell();
        if (!cell) {
            return std::nullopt;
        }
        const auto origin = actor.GetPosition();
        const auto center = RestCenter(f, anchor);
        const auto& tuning = Settings::GetSingleton().Get().sandbox;
        const float height = tuning.activityHeight;
        const auto place = PlaceFor(actor);
        const bool cold = TraitsFor(actor).cold;
        f.pendingFeature = AreaFeature::None;
        f.pendingFeatureRef = {};
        std::unordered_set<RE::FormID> occupied;
        std::vector<RE::NiPoint3> people;
        struct Opportunity
        {
            RE::ObjectRefHandle handle;
            float score;
            bool patron;
            AreaFeature feature;
        };
        std::vector<Opportunity> choices;
        auto claimed = [&](RE::FormID id) {
            return std::any_of(managed.begin(), managed.end(), [&](const auto& entry) {
                auto ref = entry.second.activityTarget.get();
                return entry.second.restRecovery.walking && ref && ref->GetFormID() == id;
            });
        };
        const bool settledSocial = SettledRest(ContextFor(actor)) && tuning.social;
        if (auto* lists = RE::ProcessLists::GetSingleton()) {
            Engine::ForEachHighActor(*lists, [&](RE::Actor& other) {
                if (SocialSpace(other) != SocialSpace(actor) || !other.Is3DLoaded()) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                if (auto furniture = other.GetOccupiedFurniture().get()) {
                    occupied.insert(furniture->GetFormID());
                }
                if (other.GetFormID() != actor.GetFormID()) {
                    people.push_back(other.GetPosition());
                }
                if (!settledSocial || other.IsPlayerRef() || managed.contains(other.GetFormID()) ||
                    other.IsDead() || other.IsInCombat() || other.GetCurrentScene() || IsSpeaking(other) ||
                    other.IsHostileToActor(&actor) || claimed(other.GetFormID())) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                auto* npc = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13794);
                if (!npc || !other.GetRace() || !other.GetRace()->HasKeyword(npc)) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                RE::NiPoint3 velocity{};
                other.GetLinearVelocity(velocity);
                const auto pos = other.GetPosition();
                const float distance = std::hypot(Distance2D(origin, pos), pos.z - origin.z);
                if (Length({velocity.x, velocity.y}) < 12 && distance > 280 &&
                    Distance2D(center, pos) < f.sandboxRadius - 180 && std::abs(pos.z - center.z) <= height) {
                    choices.push_back({other.GetHandle(),
                                       distance + 250 +
                                           (RestSpotCrowded(actor.GetFormID(), pos, 260) ? 900.0F : 0.0F),
                                       true, AreaFeature::None});
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }
        std::unordered_set<RE::TESObjectCELL*> cells{cell};
        if (!cell->IsInteriorCell()) {
            if (auto* world = RE::TES::GetSingleton()) {
                for (float x : {-f.sandboxRadius, 0.0F, f.sandboxRadius}) {
                    for (float y : {-f.sandboxRadius, 0.0F, f.sandboxRadius}) {
                        auto* neighbor = world->GetCell({center.x + x, center.y + y, center.z});
                        if (neighbor && neighbor->IsAttached()) {
                            cells.insert(neighbor);
                        }
                    }
                }
            }
        }
        auto currentFeature = f.featureRef.get();
        for (auto* searchCell : cells) {
            Engine::ForEachReferenceInRange(
                *searchCell, center, std::hypot(f.sandboxRadius, height), [&](RE::TESObjectREFR& ref) {
                    auto* base = ref.GetBaseObject();
                    if (!base || ref.IsDisabled() || ref.IsDeleted()) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    const auto pos = ref.GetPosition();
                    const float distance = std::hypot(Distance2D(origin, pos), pos.z - origin.z);
                    if (std::abs(pos.z - center.z) > height || Distance2D(center, pos) > f.sandboxRadius - 40) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    // Fires, shrines and stones take several visitors, so they are not "claimed".
                    if (const auto feature = tuning.areaActivities ? FeatureFor(*base) : AreaFeature::None;
                        feature != AreaFeature::None) {
                        if (distance >= 140 && currentFeature.get() != &ref &&
                            FeatureUsers(actor.GetFormID(), ref.GetFormID()) < FeatureCapacity(feature)) {
                            const float variation = static_cast<float>(
                                RestPose::Mix(ref.GetFormID() + actor.GetFormID() + f.activitySearch.sequence) %
                                200);
                            choices.push_back({ref.GetHandle(), distance + variation + FeatureScore(feature, place, cold),
                                               false, feature});
                        }
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    if (ref.GetIgnoredBySandbox() || occupied.contains(ref.GetFormID()) ||
                        claimed(ref.GetFormID())) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    auto* furniture = base->As<RE::TESFurniture>();
                    if (!furniture && base->GetFormType() != RE::FormType::IdleMarker) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }

                    if (furniture &&
                        (furniture->furnFlags.any(RE::TESFurniture::ActiveMarker::kCanSleep,
                                                  RE::TESFurniture::ActiveMarker::kDisablesActivation,
                                                  RE::TESFurniture::ActiveMarker::kIsPerch) ||
                         furniture->workBenchData.benchType !=
                             RE::TESFurniture::WorkBenchData::BenchType::kNone ||
                         UnsuitableRestFurniture(*furniture))) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    if (distance < 160) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    // Vary the pick and steer away from spots another companion already has.
                    const float variation = static_cast<float>(
                        RestPose::Mix(ref.GetFormID() + actor.GetFormID() + f.activitySearch.sequence) % 320);
                    const float crowding = RestSpotCrowded(actor.GetFormID(), pos, 260) ? 900.0F : 0.0F;
                    choices.push_back({ref.GetHandle(), distance + variation + crowding, false, AreaFeature::None});
                    return RE::BSContainer::ForEachResult::kContinue;
                });
        }
        std::sort(choices.begin(), choices.end(),
                  [](const auto& a, const auto& b) { return a.score < b.score; });
        const auto area = choices.empty() ? RestReachability{}
                                          : navigation.RestArea(origin, center, f.sandboxRadius, height);
        static constexpr std::array<float, 3> beside{0.0F, .7F, -.7F};
        static constexpr std::array<float, 5> around{0.0F, 1.0F, -1.0F, 2.0F, -2.0F}; // A ring round a fire.
        for (std::size_t i = 0; i < std::min<std::size_t>(choices.size(), 12); ++i) {
            auto ref = choices[i].handle.get();
            if (!ref) {
                continue;
            }
            const auto feature = choices[i].feature;
            const auto pos = ref->GetPosition();
            const float angle = std::atan2(origin.y - pos.y, origin.x - pos.x);
            float standOff = choices[i].patron ? 175.0F : 95.0F;
            if (feature != AreaFeature::None) {
                const auto low = ref->GetBoundMin(), high = ref->GetBoundMax();
                const float extent =
                    std::max({std::abs(low.x), std::abs(low.y), std::abs(high.x), std::abs(high.y)}) *
                    ref->GetScale();
                standOff = std::clamp(extent + (feature == AreaFeature::Fire ? 70.0F : 60.0F), 90.0F, 260.0F);
            }
            const std::span<const float> turns =
                feature == AreaFeature::Fire ? std::span<const float>(around) : std::span<const float>(beside);
            for (float turn : turns) {
                const RE::NiPoint3 desired{pos.x + std::cos(angle + turn) * standOff,
                                           pos.y + std::sin(angle + turn) * standOff, pos.z};
                const auto route = navigation.FindRestDestination(area, desired);
                if (!route) {
                    continue;
                }
                const RE::NiPoint3 point{route->point.x, route->point.y, route->point.z};
                if (std::hypot(Distance2D(origin, point), point.z - origin.z) < 120 ||
                    Distance2D(point, pos) > standOff + 45 ||
                    Distance2D(center, point) > f.sandboxRadius - 30 ||
                    std::abs(point.z - center.z) > height) {
                    continue;
                }
                float water{};
                auto* targetCell = ref->GetParentCell();
                if (!targetCell || (targetCell->GetWaterHeight(point, water) && water >= point.z - 2)) {
                    continue;
                }
                if (std::any_of(people.begin(), people.end(), [&](auto person) {
                        return std::abs(person.z - point.z) < 120 && Distance2D(person, point) < 100;
                    }) ||
                    (feature != AreaFeature::None && RestSpotCrowded(actor.GetFormID(), point, 90))) {
                    continue;
                }
                f.activityTarget = ref->GetHandle();
                f.activityWalkTime = RestWalkBudget(route->routeLength);
                if (feature != AreaFeature::None) {
                    f.pendingFeature = feature;
                    f.pendingFeatureRef = ref->GetHandle();
                    f.pendingFeaturePoint = pos;
                }
                logger::info("[RestActivity] {} approaching {} {:08X}; distance={:.0f} range={:.0f} "
                             "heightDelta={:.0f} route={:.0f} timeout={:.0f} place={}",
                             actor.GetName(),
                             choices[i].patron             ? "patron"
                             : feature != AreaFeature::None ? FeatureName(feature)
                                                            : "marker",
                             ref->GetFormID(), Distance2D(origin, point), f.sandboxRadius, point.z - origin.z,
                             route->routeLength, f.activityWalkTime, PlaceName(place));
                return point;
            }
        }
        logger::info("[RestActivity] {} no reachable unoccupied activity destination; candidates={} "
                     "reachable={} limited={} place={}",
                     actor.GetName(), choices.size(), area.distance.size(), area.limited, PlaceName(place));
        return std::nullopt;
    }

    bool FormationController::RestSpotCrowded(RE::FormID self, RE::NiPoint3 point, float clearance) const
    {
        for (const auto& [id, other] : managed) {
            if (id == self) {
                continue;
            }
            if (auto companion = other.handle.get();
                companion && Distance2D(companion->GetPosition(), point) < clearance) {
                return true;
            }
            if (other.restRecovery.walking && other.slot >= 0 &&
                static_cast<std::size_t>(other.slot) < gatherPool.size()) {
                if (auto destination = gatherPool[static_cast<std::size_t>(other.slot)].get();
                    destination && Distance2D(destination->GetPosition(), point) < clearance) {
                    return true;
                }
            }
        }
        return false;
    }

    std::optional<RE::NiPoint3> FormationController::FindRestSpot(ManagedFollower& f, RE::Actor& actor,
                                                                  RE::TESObjectREFR& anchor, float distance)
    {
        auto* cell = actor.GetParentCell();
        if (!cell) {
            return std::nullopt;
        }
        const auto origin = actor.GetPosition();
        const auto center = RestCenter(f, anchor);
        const auto& tuning = Settings::GetSingleton().Get().sandbox;
        const float height = tuning.activityHeight;
        const auto place = PlaceFor(actor);
        // Outdoors, the water's edge and high ground are worth a longer walk.
        const bool outdoors = tuning.areaActivities && OutdoorPlace(place) && !cell->IsInteriorCell();
        const float outer = std::min(distance * 1.35F, f.sandboxRadius - 40);
        const float inner = std::min(distance * .6F, outer * .5F);
        const auto area = navigation.RestArea(origin, center, f.sandboxRadius, height);
        const auto spots = navigation.RestSpots(area, center, outdoors ? std::min(inner, 250.0F) : inner,
                                                outdoors ? f.sandboxRadius - 40 : outer);
        auto* tes = RE::TES::GetSingleton();
        auto waterAt = [&](RE::NiPoint3 p) -> std::optional<float> {
            auto* at = !cell->IsInteriorCell() && tes ? tes->GetCell(p) : cell;
            float water{};
            if (at && at->GetWaterHeight(p, water) && water > -100000.0F) {
                return water;
            }
            return std::nullopt;
        };
        // Each companion leans toward its own bearing from the anchor so the party fans out.
        const float bearing =
            static_cast<float>(RestPose::Mix(actor.GetFormID() + f.restRecovery.sequence * 7919) % 6283) * .001F;
        struct Ranked
        {
            float score;
            RestDestination spot;
            AreaFeature feature;
        };
        std::vector<Ranked> ranked;
        for (const auto& spot : spots) {
            const RE::NiPoint3 point{spot.point.x, spot.point.y, spot.point.z};
            if (std::abs(point.z - center.z) > height || Distance2D(origin, point) < 140) {
                continue;
            }
            auto kind = AreaFeature::None;
            float bonus = 0;
            if (outdoors) {
                if (const auto water = waterAt(point)) {
                    const float above = point.z - *water;
                    if (above < 2) {
                        continue;
                    }
                    if (above <= 40) {
                        kind = AreaFeature::Water;
                        bonus = -650;
                    }
                }
                if (const float rise = point.z - center.z; kind == AreaFeature::None &&
                                                          place == RestPlace::Wild && rise >= 180) {
                    kind = AreaFeature::Vista;
                    bonus = -std::min(rise, 500.0F) * 1.2F;
                }
            }
            const float reach = Distance2D(center, point);
            if (kind == AreaFeature::None && (reach < inner || reach > outer)) {
                continue;
            }
            const auto away = Normalize({point.x - center.x, point.y - center.y});
            const float alignment = away.x * std::cos(bearing) + away.y * std::sin(bearing);
            const auto hash =
                RestPose::Mix(static_cast<std::uint32_t>(static_cast<std::int32_t>(point.x)) * 73856093U ^
                              static_cast<std::uint32_t>(static_cast<std::int32_t>(point.y)) * 19349663U ^
                              actor.GetFormID());
            const float score = (kind == AreaFeature::None ? std::abs(reach - distance) : 0.0F) +
                                (1 - alignment) * 300 + static_cast<float>(hash % 200) + spot.routeLength * .15F +
                                (RestSpotCrowded(actor.GetFormID(), point, 260) ? 1500.0F : 0.0F) + bonus;
            ranked.push_back({score, spot, kind});
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.score < b.score; });
        for (std::size_t i = 0; i < std::min<std::size_t>(ranked.size(), 12); ++i) {
            const auto& spot = ranked[i].spot;
            const RE::NiPoint3 point{spot.point.x, spot.point.y, spot.point.z};
            float water{};
            if ((cell->GetWaterHeight(point, water) && water >= point.z - 2) ||
                RestSpotCrowded(actor.GetFormID(), point, 220)) {
                continue;
            }
            f.activityWalkTime = RestWalkBudget(spot.routeLength);
            const auto kind = ranked[i].feature;
            if (kind != AreaFeature::None) {
                // Face away from the land: at the shore that is the water, on a rise the view.
                Vec2 landward{};
                for (const auto& other : spots) {
                    const Vec2 offset{other.point.x - point.x, other.point.y - point.y};
                    const float length = Length(offset);
                    if (length < 40 || length > 320) {
                        continue;
                    }
                    const RE::NiPoint3 at{other.point.x, other.point.y, other.point.z};
                    const auto level = kind == AreaFeature::Water ? waterAt(at) : std::nullopt;
                    if (kind == AreaFeature::Water && level && other.point.z - *level <= 40) {
                        continue; // Shoreline too, not land behind.
                    }
                    if (kind == AreaFeature::Vista && other.point.z > point.z - 40) {
                        continue; // Only the slope climbed, not the summit.
                    }
                    landward = {landward.x + offset.x / length, landward.y + offset.y / length};
                }
                auto outward = Length(landward) > .5F ? Normalize({-landward.x, -landward.y})
                                                      : Normalize({point.x - center.x, point.y - center.y});
                f.pendingFeature = kind;
                f.pendingFeatureRef = {};
                f.pendingFeaturePoint = {point.x + outward.x * 400, point.y + outward.y * 400, point.z};
            }
            logger::info("[RestActivity] {} heading for {}; distance={:.0f} anchorDistance={:.0f} "
                         "range={:.0f} route={:.0f} place={}",
                         actor.GetName(),
                         kind == AreaFeature::Water   ? "the water's edge"
                         : kind == AreaFeature::Vista ? "high ground"
                                                      : "a quiet spot",
                         Distance2D(origin, point), Distance2D(center, point), f.sandboxRadius, spot.routeLength,
                         PlaceName(place));
            return point;
        }
        logger::info("[RestActivity] {} found no open ground to spread out; spots={} reachable={} limited={}",
                     actor.GetName(), spots.size(), area.distance.size(), area.limited);
        return std::nullopt;
    }

    void FormationController::BeginRestWalk(ManagedFollower& f, float budget)
    {
        if (auto* stroll = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                0x870 + f.slot, Settings::GetSingleton().Get().pluginName)) {
            stroll->packData.packFlags.set(RE::PACKAGE_DATA::GeneralFlag::kPreferredSpeed);
            stroll->packData.maxSpeed = RE::PACKAGE_DATA::PreferredSpeed::kWalk;
        }
        f.restRecovery.Begin(budget);
    }

    void FormationController::UpdateRestRecovery(ManagedFollower& f, RE::Actor& actor,
                                                 RE::TESObjectREFR& anchor, float dt)
    {
        auto& recovery = f.restRecovery;
        auto* state = actor.AsActorState();
        RE::NiPoint3 motion{};
        actor.GetLinearVelocity(motion);
        bool animationDriven = false;
        actor.GetGraphVariableBool("bAnimationDriven", animationDriven);
        const bool busy = !state || state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal ||
                          actor.GetOccupiedFurniture().get() || actor.GetCurrentScene() ||
                          actor.IsInCombat() || f.restPose.kind || lookout.actor == actor.GetFormID() ||
                          social.Holds(actor.GetFormID()) || f.patronTimer > 0 || animationDriven ||
                          IsSpeaking(actor);
        const auto& settings = Settings::GetSingleton().Get();
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* native = data->LookupForm<RE::TESPackage>(SandboxPackageLocal(f.slot), settings.pluginName);
        auto* stroll = data->LookupForm<RE::TESPackage>(0x870 + f.slot, settings.pluginName);
        if (recovery.walking) {
            if (f.socialWalk) {
                // UpdateSocial ends this walk on arrival; only a stall or interruption ends it here.
                if (recovery.Done(dt, false, busy || !gatherPool[f.slot].get())) {
                    logger::info("[Social] {} did not reach their conversation partner; elapsed={:.1f}",
                                 actor.GetName(), recovery.elapsed);
                    EndSocialWalk(f, true);
                }
                return;
            }
            auto target = gatherPool[f.slot].get();
            const auto actorPos = actor.GetPosition();
            const auto targetPos = target ? target->GetPosition() : RE::NiPoint3{};
            const bool arrived = target && RestArrived({actorPos.x, actorPos.y, actorPos.z},
                                                       {targetPos.x, targetPos.y, targetPos.z});
            if (recovery.Done(dt, arrived,
                              busy || !target ||
                                  (!f.activityTarget.get() && !recovery.dispersal &&
                                   settings.sandbox.idleRecoveryAfter <= 0))) {
                // Rest where they went. Native sandbox otherwise walks straight back to the
                // stopping point: the marker becomes the centre, with a small radius. A seat
                // taken just short of the destination counts.
                const bool closeBy = target && Distance2D(actorPos, targetPos) < 250;
                const bool atFeature = arrived && f.pendingFeature != AreaFeature::None;
                if (arrived || closeBy) {
                    FocusRest(f, actor, anchor,
                              f.activityTarget.get() && f.pendingFeature == AreaFeature::None ? 200.0F : 320.0F);
                }
                if (!arrived) {
                    f.pendingFeature = AreaFeature::None;
                }
                if (arrived && (atFeature || !f.activityTarget.get())) {
                    // At a fire, shrine or open spot: settle into a pose soon after arriving.
                    f.restPose.Init(actor.GetFormID());
                    f.restPose.cooldown = std::min(
                        f.restPose.cooldown,
                        atFeature ? .5F
                                  : 2.0F + static_cast<float>(RestPose::Mix(actor.GetFormID() + recovery.sequence) % 4));
                }
                logger::info("[RestRecovery] {} returning to native sandbox; elapsed={:.1f} {}", actor.GetName(),
                             recovery.elapsed,
                             arrived || closeBy ? fmt::format("settling here (radius {:.0f})", f.focusRadius)
                                             : std::string("interrupted"));
                recovery.Finish();
                f.activityTarget = {};
            }
            return;
        }
        // Dispersal: go somewhere to relax as soon as rest begins or widens. Unlike the
        // quiet-time searches below, companions may set off together.
        if (f.activitySearch.Primed(dt, stroll && !busy && actor.GetCurrentPackage() == native)) {
            if (activityDispatchCooldown > 0) {
                return;
            }
            f.activitySearch.Attempt(actor.GetFormID());
            activityDispatchCooldown = .35F;
            auto point = FindRestActivity(f, actor, anchor);
            if (!point) {
                f.activityTarget = {};
                point = FindRestSpot(f, actor, anchor, std::clamp(f.sandboxRadius * .45F, 250.0F, 900.0F));
            }
            if (point && PrepareAuxMarker(f, actor, *point, true)) {
                BeginRestWalk(f, f.activityWalkTime);
                recovery.dispersal = true;
                return;
            }
            f.activityTarget = {};
            f.pendingFeature = AreaFeature::None;
            return;
        }
        const bool standing =
            !busy && actor.GetCurrentPackage() == native && Length({motion.x, motion.y}) < 12;
        const bool activityReady = f.activitySearch.Ready(dt, standing, actor.GetFormID());
        const bool recoveryReady =
            recovery.Ready(dt, standing, sandbox.expanded,
                           settings.sandbox.idleRecoveryAfter * RecoveryPreference(PersonalityFor(actor)),
                           actor.GetFormID());
        if (!activityReady && !recoveryReady) {
            return;
        }
        for (const auto& [id, other] : managed) {
            if (id != actor.GetFormID() && other.restRecovery.walking) {
                return;
            }
        }
        if (!stroll) {
            recovery.Finish();
            return;
        }
        if (activityReady && activityDispatchCooldown <= 0) {
            f.activitySearch.Attempt(actor.GetFormID());
            activityDispatchCooldown = 2.5F;
            auto point = FindRestActivity(f, actor, anchor);
            if (!point && settings.sandbox.areaActivities && OutdoorPlace(PlaceFor(actor))) {
                // Outdoors there is usually open water or high ground to wander to next.
                f.activityTarget = {};
                point = FindRestSpot(f, actor, anchor, std::clamp(f.sandboxRadius * .45F, 250.0F, 900.0F));
            }
            if (point) {
                if (PrepareAuxMarker(f, actor, *point, true)) {
                    BeginRestWalk(f, f.activityWalkTime);
                    return;
                }
                f.activityTarget = {};
                f.pendingFeature = AreaFeature::None;
            }

            actor.EvaluatePackage(false, true);
        }
        if (!recoveryReady) {
            return;
        }
        const auto origin = actor.GetPosition(), center = RestCenter(f, anchor);
        const float radius = std::min(600.0F, f.sandboxRadius * .5F);
        for (int attempt = 0; attempt < 8; ++attempt) {
            const auto roll = RestPose::Mix(actor.GetFormID() + recovery.sequence * 7919 + attempt * 3571);
            const float angle = static_cast<float>(roll % 6283) * .001F;
            const Vec2 desired{center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
            const auto direction = Normalize({desired.x - origin.x, desired.y - origin.y});
            const auto point = navigation.Resolve(origin, desired, direction);
            if (!point || Distance2D(origin, *point) < 140 ||
                Distance2D(center, *point) > f.sandboxRadius - 35 ||
                (f.restAnchorZSet && std::abs(point->z - f.restAnchorZ) > settings.sandbox.activityHeight)) {
                continue;
            }
            float water{};
            if (actor.GetParentCell()->GetWaterHeight(*point, water) && water >= point->z - 2) {
                continue;
            }
            if (RestSpotCrowded(actor.GetFormID(), *point, 120)) {
                continue;
            }
            if (!PrepareAuxMarker(f, actor, *point, true)) {
                break;
            }
            BeginRestWalk(f);
            logger::info("[RestRecovery] {} taking a short walk; distance={:.0f} anchorDistance={:.0f}",
                         actor.GetName(), Distance2D(origin, *point), Distance2D(center, *point));
            return;
        }
        recovery.Finish();
    }

    void FormationController::ApplySandbox(ManagedFollower& f, RE::Actor& actor, RE::PlayerCharacter& player,
                                           float dt)
    {
        f.individual.ResetMotion();
        static_cast<void>(player);
        f.recoveryReleasePending = false;
        const auto& settings = Settings::GetSingleton().Get();
        auto* quest = EnsureQuest();
        auto* package = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
            SandboxPackageLocal(f.slot), settings.pluginName);
        if (!quest || !quest->IsRunning() || !package) {
            ReleaseTravel(f, &actor);
            return;
        }
        auto* custom = skyrim_cast<RE::TESCustomPackageData*>(package->data);
        RE::PackageLocation* location = nullptr;
        if (custom && custom->data.data && custom->data.uids) {
            for (std::uint16_t i = 0; i < custom->data.dataSize; ++i) {
                if (custom->data.uids[i] != 0) {
                    continue;
                }
                auto* input = skyrim_cast<RE::BGSPackageDataLocation*>(custom->data.data[i]);
                if (input) {
                    location = input->pointer;
                }
                break;
            }
        }
        if (!location || location->locType != RE::PackageLocation::Type::kAlias_Reference) {
            LogDiagnostic(actor.GetFormID(), actor.GetName(),
                          "Sandbox location input unavailable; verify matching ESP");
            ReleaseTravel(f, &actor);
            return;
        }
        auto* marker = EnsureMarker(f, actor);
        if (!marker) {
            ReleaseTravel(f, &actor);
            return;
        }
        if (settings.conversationAwareness) {
            PrepareAuxMarker(f, actor, actor.GetPosition());
        }
        const bool entering = !f.sandboxActive;

        const bool moved = !f.anchorSet;

        if (moved) {
            marker->MoveTo(&actor);
            marker->SetPosition(actor.GetPosition());
            f.anchorSet = true;
            f.restHome = actor.GetPosition();
            f.restHomeSet = true;
            f.focused = false;
            f.feature = f.pendingFeature = AreaFeature::None;
        }
        if (!f.restHomeSet) {
            f.restHome = marker->GetPosition(); // A Relax order restored from a save.
            f.restHomeSet = true;
        }
        if (!f.restAnchorZSet) {
            f.restAnchorZ = marker->GetPosition().z;
            f.restAnchorZSet = true;
        }
        const auto context = ContextFor(actor);
        const float activityRadius = ActivityRadius(context, sandbox.radius, settings.sandbox);
        if (entering || f.sandboxRadius != activityRadius) {
            logger::info("[Sandbox] {} activity radius {:.0f} -> {:.0f}; context={}; phase={}; current "
                         "action retained",
                         actor.GetName(), f.sandboxRadius, activityRadius, static_cast<int>(context),
                         sandbox.expanded ? "free roam" : "nearby");
        }
        // Head straight for somewhere to relax when rest begins or its area widens,
        // rather than loitering at the stopping point. Staggered so they spread out.
        if (activityRadius > f.sandboxRadius + 1) {
            f.activitySearch.Prime(.3F + .6F * static_cast<float>(f.slot) +
                                   static_cast<float>(RestPose::Mix(actor.GetFormID()) % 5) * .1F);
        }

        f.distantCatchup = false;
        package->packData.packFlags.set(RE::PACKAGE_DATA::GeneralFlag::kPreferredSpeed);
        package->packData.maxSpeed = RE::PACKAGE_DATA::PreferredSpeed::kWalk;
        f.sandboxActive = f.travelActive = true;

        f.sandboxRadius = activityRadius;
        // Skyrim's own sandbox eats whatever food they carry, anywhere; only allow it where a meal
        // fits the place (an inn, a home, a hall, a campfire), and on a short stop only at the inn.
        const bool atFire =
            f.feature == AreaFeature::Fire && Distance2D(actor.GetPosition(), f.featurePoint) < 420.0F;
        const int meals = NativeMeals(PlaceFor(actor), context, atFire, IsRelaxing(), settings.sandbox) ? 1 : 0;
        if (f.mealsState != meals) {
            auto* source = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                meals ? SocialPackageLocal(f.slot) : PosePackageLocal(f.slot), settings.pluginName);
            auto* destination = BoolInput(package, 1);
            auto* value = BoolInput(source, meals ? 4 : 1);
            if (destination && value) {
                destination->Assign(value);
                f.mealsState = meals;
            }
        }
        const int previousPose = f.restPose.kind;
        const bool previouslyWalking = f.restRecovery.walking;
        UpdateRestRecovery(f, actor, *marker, dt);
        UpdateRestPose(f, actor, dt);
        // After a walk the marker sits where they went, with a small radius; otherwise the
        // whole area. Set after the updates above so an arrival applies this same tick.
        location->rad = static_cast<std::uint32_t>(f.focused ? std::min(f.focusRadius, activityRadius)
                                                             : activityRadius);
        if (lookout.actor == actor.GetFormID()) {
            SetPoseFlag(f.slot, 1);
        }

        const bool previousHold = f.socialHold;
        f.socialHold = social.Holds(actor.GetFormID());
        f.socialPhase = f.restRecovery.walking ? 2 : f.socialHold ? 1 : 0;
        SetSocialFlag(f.slot, f.socialPhase);
        auto* activePackage = package;
        if (f.restRecovery.walking) {
            activePackage = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                0x870 + f.slot, settings.pluginName);
        }
        else if (f.socialHold) {
            activePackage = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                SocialPackageLocal(f.slot), settings.pluginName);
        }
        if (f.restPose.kind) {
            activePackage = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                f.restPose.kind == 2 ? SeatPackageLocal(f.slot) : PosePackageLocal(f.slot),
                settings.pluginName);
        }
        else if (lookout.actor == actor.GetFormID()) {
            activePackage = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                PosePackageLocal(f.slot), settings.pluginName);
        }
        f.sandboxRefresh += dt;
        if (entering) {
            f.goal.Release();
        }
        const bool owns = actor.GetCurrentPackage() == activePackage;
        auto* alias = GetRefAlias(quest, FollowerAlias(f.slot));
        bool animationDriven = false;
        actor.GetGraphVariableBool("bAnimationDriven", animationDriven);
        auto* actorState = actor.AsActorState();
        const bool nativeBusy =
            actor.GetOccupiedFurniture().get() || animationDriven ||
            (actorState && actorState->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal);
        const bool ownPoseTransition =
            previousPose != f.restPose.kind && previousPose != 0 &&
            actor.GetCurrentPackage() ==
                RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                    previousPose == 2 ? SeatPackageLocal(f.slot) : PosePackageLocal(f.slot),
                    settings.pluginName);
        if (alias && alias->GetReference() == &actor && !IsSpeaking(actor) && !actor.GetCurrentScene() &&
            (!nativeBusy || ownPoseTransition) &&
            (entering || previousPose != f.restPose.kind || previouslyWalking != f.restRecovery.walking ||
             previousHold != f.socialHold || moved || (!owns && f.sandboxRefresh >= 1.0F))) {
            actor.EvaluatePackage(false, true);
            f.sandboxRefresh = 0;
        }
        f.speedScale = SmoothSpeedScale(f.speedScale, 1.0F, dt);
        TravelSpeed::Set(static_cast<std::uint32_t>(f.slot), actor.GetFormID(), f.speedScale);
        if (settings.traceMovement && traceDue) {
            RE::NiPoint3 motion{};
            actor.GetLinearVelocity(motion);
            logger::info(
                "[Sandbox] {} radius={:.0f} ownPackage={} alias={} social={} speedScale={:.2f} phase={} "
                "restAge={:.1f} speed={:.0f} anchorDistance={:.0f} furniture={} scene={} pose={}",
                actor.GetName(), f.sandboxRadius, owns, alias && alias->GetReference() == &actor,
                social.Listener(actor.GetFormID()) != 0, f.speedScale,
                sandbox.expanded ? "free roam" : "nearby", sandbox.activeTime, Length({motion.x, motion.y}),
                Distance2D(actor.GetPosition(), marker->GetPosition()),
                bool(actor.GetOccupiedFurniture().get()), actor.GetCurrentScene() != nullptr,
                f.restPose.kind);
            if (!owns) {
                logger::info("[SandboxOwner] {} active={}", actor.GetName(),
                             DescribePackage(actor.GetCurrentPackage()));
            }
        }
    }

    RE::Actor* FormationController::GetSlotFollower(std::uint32_t slot)
    {
        std::lock_guard lock(stateMutex);
        if (!enabled || mode == FormationMode::kVanilla || loadReleaseTime > 0.0F) {
            return nullptr;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || (!IsRelaxing() && GlobalRejection(*player))) {
            return nullptr;
        }
        for (auto& [id, follower] : managed) {
            if (static_cast<std::uint32_t>(follower.slot) != slot || !follower.travelActive) {
                continue;
            }
            if (HandHolding::OwnsMovement(id)) {
                return nullptr;
            }
            if (!sandbox.active && !follower.scout.Active() && !follower.pathfinder.Active() &&
                !HandHolding::TravelRequested(intent.CanTravel(), mode == FormationMode::kCompanion,
                                              Settings::GetSingleton().Get().handHolding, id,
                                              HandHolding::GetApproach().actor)) {
                return nullptr;
            }
            auto actor = follower.handle.get();
            if (!actor || !UsabilityRejection(*actor, *player).empty()) {
                return nullptr;
            }
            return actor.get();
        }
        return nullptr;
    }

    RE::TESObjectREFR* FormationController::GetSlotMarker(std::uint32_t slot)
    {
        std::lock_guard lock(stateMutex);
        if (!GetSlotFollower(slot)) {
            return nullptr;
        }
        return slot < markerPool.size() ? markerPool[slot].get().get() : nullptr;
    }

    RE::TESQuest* FormationController::EnsureQuest()
    {
        if (!questCache) {
            auto* data = RE::TESDataHandler::GetSingleton();
            if (data) {
                questCache = data->LookupForm<RE::TESQuest>(WAYFARER_QUEST_LOCAL_ID,
                                                            Settings::GetSingleton().Get().pluginName);
            }
        }
        if (!questCache) {
            if (!questWarned) {
                logger::error("[Quest] Wayfarer.esp is missing or disabled");
                questWarned = true;
            }
            return nullptr;
        }

        const auto& settings = Settings::GetSingleton().Get();
        const auto priority =
            (settings.enforceNFF || settings.enforceCustomFollowers || !manualRegistrations.empty() ||
             !dialogueRegistrations.empty() || !packageRegistrations.empty() || sandbox.active)
                ? 100
                : 96;
        if (questCache->data.priority != priority) {
            logger::info("[Quest] Runtime priority {} -> {}", static_cast<int>(questCache->data.priority),
                         priority);
            questCache->data.priority = static_cast<std::int8_t>(priority);
        }
        if (!questCache->IsRunning() && !questCache->IsStarting() && !questCache->IsStopping()) {
            questCache->Start();
        }
        return questCache;
    }

    bool FormationController::AliasSyncAllowed(std::int32_t ticket, std::int32_t session)
    {
        std::lock_guard lock(stateMutex);
        return ticket != 0 && ticket == aliasSyncTicket && session == aliasSyncSession &&
               aliasSyncQueue.Pending() && !ShouldSuspendForDialogue();
    }

    void FormationController::ReportAliasLook(std::int32_t slot, RE::Actor* actor, RE::Actor* target,
                                              std::int32_t ticket, std::int32_t session)
    {
        std::lock_guard lock(stateMutex);
        if (slot < 0 || slot >= PARTY_CAPACITY || ticket == 0 || ticket != aliasSyncTicket ||
            session != aliasSyncSession || !aliasSyncQueue.Pending()) {
            return;
        }
        aliasLookActors[slot] = actor && target ? actor->GetFormID() : 0;
        aliasLookTargets[slot] = actor && target ? target->GetFormID() : 0;
    }

    void FormationController::ReportAliasCleanup(std::int32_t slot, bool pending, std::int32_t ticket,
                                                 std::int32_t session)
    {
        std::lock_guard lock(stateMutex);
        if (slot >= 0 && slot < PARTY_CAPACITY && ticket != 0 && ticket == aliasSyncTicket &&
            session == aliasSyncSession && aliasSyncQueue.Pending()) {
            aliasCleanupPending[slot] = pending;
        }
    }

    void FormationController::CompleteAliasSync(std::uint64_t ticket, std::int32_t session, bool completed)
    {
        std::lock_guard lock(stateMutex);
        if (session != aliasSyncSession || !aliasSyncQueue.Complete(ticket, completed)) {
            return;
        }
        aliasSyncTicket = 0;
        aliasSyncRetryDelay = completed ? 0.25F : std::min(8.0F, aliasSyncRetryDelay * 2.0F);
        aliasSyncTimer = 0;
    }

    void FormationController::UpdateAliasSync(float dt)
    {
        std::lock_guard lock(stateMutex);
        if (!initialized || !std::isfinite(dt) || dt <= 0 || dt > 1 || aliasSyncQueue.Pending()) {
            return;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        if (!player || !player->Is3DLoaded() || !player->GetParentCell() || !ui || ui->GameIsPaused() ||
            ShouldSuspendForDialogue()) {
            return;
        }
        aliasSyncTimer += dt;
        if (aliasSyncTimer < aliasSyncRetryDelay) {
            return;
        }
        aliasSyncTimer = 0;
        auto* quest = EnsureQuest();
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
        if (!quest || !quest->IsRunning() || !policy) {
            return;
        }

        // Inspect native state cheaply. Papyrus only runs for changed slots or ready actions.
        // In particular, disabled cleanup still runs, then the bridge becomes idle.
        AliasSyncSnapshotSet snapshot{};
        std::uint16_t work = 0;
        const bool resting = HasRestActivities();
        const auto id = [](const RE::TESForm* form) { return form ? form->GetFormID() : 0; };
        const auto differs = [quest, &id](std::uint32_t aliasID, std::uint32_t desired) {
            auto* alias = GetRefAlias(quest, aliasID);
            return alias && id(alias->GetReference()) != desired;
        };
        for (std::uint32_t slot = 0; slot < PARTY_CAPACITY; ++slot) {
            auto& state = snapshot[slot];
            auto* actor = GetSlotFollower(slot);
            auto* marker = actor ? markerPool[slot].get().get() : nullptr;
            if (!marker) {
                actor = nullptr;
            }
            state.follower = id(actor);
            state.marker = id(marker);
            state.gather = id(GetGatherMarker(slot));
            state.seat = resting ? id(GetRestSeat(slot)) : 0;
            state.social = resting && actor ? id(GetSocialTarget(slot)) : 0;
            state.handoff = GetTetherHandoffTicket(slot);
            bool needed =
                aliasCleanupPending[slot] || aliasLookActors[slot] != (state.social ? state.follower : 0) ||
                aliasLookTargets[slot] != state.social || differs(FollowerAlias(slot), state.follower) ||
                (actor && differs(MarkerAlias(slot), state.marker)) ||
                (state.gather && differs(GatherAlias(slot), state.gather)) ||
                (state.seat && differs(SeatAlias(slot), state.seat));
            // Marker, seat and gathering aliases intentionally retain reusable references
            // when no target is requested. Comparing them to None would loop forever.
            if (resting && actor) {
                auto& follower = managed.at(actor->GetFormID());
                if (follower.tableIdle) {
                    if (auto chair = follower.tableSeat.get()) {
                        needed |= SeatedIn(*actor, chair.get()) && !IsSpeaking(*actor);
                    }
                    else {
                        follower.tableIdle = 0;
                    }
                }
                else if (follower.restPose.kind == 1 && !follower.restPose.started &&
                         !follower.restIdleRequested) {
                    auto* pose = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                        PosePackageLocal(slot), Settings::GetSingleton().Get().pluginName);
                    needed |= actor->GetCurrentPackage() == pose;
                }
                if (state.social) {
                    const int index = social.Index(actor->GetFormID());
                    needed |= (follower.patronTimer > 0 && follower.patronGesture) ||
                              (index >= 0 && !social.gathering && social.pending[index]);
                }
            }
            if (needed) {
                work |= static_cast<std::uint16_t>(1U << slot);
            }
        }
        const auto slots = aliasSyncQueue.DirtySlots(snapshot, work);
        if (!slots) {
            return;
        }
        const auto ticket = aliasSyncQueue.Begin(snapshot, slots);
        if (!ticket) {
            return;
        }
        aliasSyncTicket = static_cast<std::int32_t>(ticket % 0x7FFFFFFF) + 1;
        const auto session = aliasSyncSession;
        struct Completion final : RE::BSScript::IStackCallbackFunctor
        {
            std::function<void(bool)> done;
            explicit Completion(std::function<void(bool)> callback) : done(std::move(callback)) {}
            void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
            void operator()(RE::BSScript::Variable value) override
            {
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    const bool completed = value.IsBool() && value.GetBool();
                    tasks->AddTask([run = done, completed] { run(completed); });
                }
            }
        };
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result{new Completion(
            [this, ticket, session](bool completed) { CompleteAliasSync(ticket, session, completed); })};
        const auto handle = policy->GetHandleForObject(RE::FormType::Quest, quest);
        const bool dispatched = vm->DispatchMethodCall(
            handle, RE::BSFixedString("WayfarerQuestScript"), RE::BSFixedString("SyncRequested"),
            RE::MakeFunctionArguments(static_cast<std::int32_t>(slots), std::int32_t{aliasSyncTicket},
                                      std::int32_t{session}),
            result);
        if (!dispatched) {
            CompleteAliasSync(ticket, session, false);
            if (!questWarned) {
                questWarned = true;
                logger::error("[Quest] Alias sync could not dispatch. Verify the matching DLL, ESP and "
                              "WayfarerQuestScript.pex are installed.");
            }
        }
    }

    RE::BGSRefAlias* FormationController::GetRefAlias(RE::TESQuest* a_quest, std::uint32_t a_aliasID)
    {
        if (!a_quest) {
            return nullptr;
        }
        for (auto* alias : a_quest->aliases) {
            if (alias && alias->aliasID == a_aliasID) {
                return skyrim_cast<RE::BGSRefAlias*>(alias);
            }
        }
        return nullptr;
    }

    RE::TESObjectREFR* FormationController::EnsureMarker(ManagedFollower& a_follower, RE::Actor& a_player)
    {
        auto& pooled = markerPool[static_cast<std::size_t>(a_follower.slot)];
        if (auto existing = pooled.get()) {
            return existing.get();
        }
        if (auto* alias = GetRefAlias(EnsureQuest(), MarkerAlias(a_follower.slot))) {
            if (auto* saved = alias->GetReference(); saved && saved->GetBaseObject() &&
                                                     saved->GetBaseObject()->GetFormID() == 0x3B &&
                                                     !saved->IsDeleted()) {
                pooled = saved->GetHandle();
                return saved;
            }
        }

        auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(0x3B);
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!base || !dataHandler) {
            if (!a_follower.markerLogged) {
                a_follower.markerLogged = true;
                logger::error("[Marker] Base form {:08X} could not be resolved; check uMarkerBaseFormID.",
                              0x3B);
            }
            return nullptr;
        }

        pooled = dataHandler->CreateReferenceAtLocation(base, a_player.GetPosition(), RE::NiPoint3{},
                                                        a_player.GetParentCell(), a_player.GetWorldspace(),
                                                        nullptr, nullptr, RE::ObjectRefHandle{}, true, false);

        auto* created = pooled.get().get();
        if (created) {
            logger::info("[Marker] Created {:08X} for slot {}", created->GetFormID(), a_follower.slot);
        }
        else if (!a_follower.markerLogged) {
            a_follower.markerLogged = true;
            logger::error("[Marker] CreateReferenceAtLocation failed for slot {}.", a_follower.slot);
        }
        return created;
    }

    void FormationController::ReleaseTravel(ManagedFollower& f, RE::Actor* actor, bool tetherHandoff)
    {
        EndLootScout(f, actor, Loot::End::Released);
        EndPathfinder(f, actor, Pathfinder::End::Interrupted);
        SetHandApproachRadius(f, false);
        f.avoidance.Reset();
        if (actor && !tetherHandoff) {
            HandHolding::ReleaseActor(actor->GetFormID());
        }
        f.individual.ResetMotion();
        EndRestPose(f, actor);
        f.haveRoleOffset = false;
        f.restRecovery.Reset();
        f.activitySearch.Reset();
        f.activityTarget = {};
        f.awarenessActive = f.awarenessWalking = f.awarenessTried = false;
        f.awarenessTime = 0;

        f.travelActive = false;
        f.socialHold = f.socialWalk = false;
        f.socialPhase = 0;
        f.patron = {};
        f.patronGesture = false;
        SetSocialFlag(f.slot, 0);
        f.sandboxActive = f.distantCatchup = false;
        f.sandboxRadius = f.sandboxRefresh = 0;
        if (!IsRelaxing()) {
            ClearRestAnchor(f);
        }
        TravelSpeed::Clear(static_cast<std::uint32_t>(f.slot));
        f.speedScale = 1.0F;
        f.goal.Release();
    }

    void FormationController::ReleaseAll(bool keepLootScouts)
    {
        HandHolding::Reset();
        RestoreTravelBanter();
        lookout.Reset();
        conversationSpeaker = 0;
        activityDispatchCooldown = 0;
        social.Reset();
        SetSandboxFlag(false);
        for (int slot = 0; slot < PARTY_CAPACITY; ++slot) {
            SetSocialFlag(slot, false);
            SetPoseFlag(slot, 0);
        }
        for (auto& [formID, follower] : managed) {
            static_cast<void>(formID);
            if (keepLootScouts && (follower.scout.Active() || follower.pathfinder.Active())) {
                continue;
            }
            auto actorPointer = follower.handle.get();
            ReleaseTravel(follower, actorPointer.get());
        }
    }

    bool FormationController::ShouldSuspendForDialogue()
    {
        std::lock_guard lock(stateMutex);
        auto* ui = RE::UI::GetSingleton();
        const auto* topics = RE::MenuTopicManager::GetSingleton();
        const bool active = (ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) ||
                            (topics && Engine::IsDialogueTransition(*topics));
        return dialogueGuard.Check(active, GetTickCount64());
    }

    bool FormationController::SocialEligible(RE::Actor& actor) const
    {
        if (!sandbox.AllowsSocial(Settings::GetSingleton().Get().sandbox) || !actor.Is3DLoaded() ||
            actor.IsInCombat() || actor.IsDead() || actor.IsSneaking() || actor.IsOnMount() ||
            actor.GetCurrentScene()) {
            return false;
        }
        auto* state = actor.AsActorState();
        if (!state || state->IsWeaponDrawn() || state->IsSwimming() ||
            state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal || actor.GetOccupiedFurniture().get()) {
            return false;
        }
        auto it = managed.find(actor.GetFormID());
        // A companion walking over to talk stays part of the pair while under way.
        const bool walkingOver = it != managed.end() && it->second.socialWalk &&
                                 it->second.restRecovery.walking && social.Walker() == actor.GetFormID();
        if (it == managed.end() || !it->second.sandboxActive || it->second.restPose.kind ||
            (it->second.restRecovery.walking && !walkingOver) || lookout.actor == actor.GetFormID()) {
            return false;
        }
        auto* data = RE::TESDataHandler::GetSingleton();
        const auto& plugin = Settings::GetSingleton().Get().pluginName;
        auto* package = data->LookupForm<RE::TESPackage>(SandboxPackageLocal(it->second.slot), plugin);
        auto* conversation = data->LookupForm<RE::TESPackage>(SocialPackageLocal(it->second.slot), plugin);
        auto* stroll = data->LookupForm<RE::TESPackage>(0x870 + it->second.slot, plugin);
        auto* current = actor.GetCurrentPackage();
        if (current != package && !(social.Holds(actor.GetFormID()) && current == conversation) &&
            !(walkingOver && current == stroll)) {
            return false;
        }
        if (!social.Involves(actor.GetFormID()) && IsSpeaking(actor)) {
            return false;
        }
        auto* race = actor.GetRace();
        auto* npc = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13794);
        if (!race || !npc || !race->HasKeyword(npc)) {
            return false;
        }
        RE::NiPoint3 velocity{};
        actor.GetLinearVelocity(velocity);
        return walkingOver || Length({velocity.x, velocity.y}) < 140;
    }

    RE::TESIdleForm* FormationController::ConsumeRestIdle(std::uint32_t slot)
    {
        std::lock_guard lock(stateMutex);
        if (ShouldSuspendForDialogue()) {
            return nullptr;
        }
        auto* actor = GetSlotFollower(slot);
        if (!actor) {
            return nullptr;
        }
        auto& f = managed.at(actor->GetFormID());
        if (f.tableIdle) {
            // A drink or meal at the inn table, played in the chair.
            auto chair = f.tableSeat.get();
            if (!chair) {
                f.tableIdle = 0;
            }
            else if (SeatedIn(*actor, chair.get()) && !IsSpeaking(*actor)) {
                f.tableIdleActive = true;
                return RE::TESForm::LookupByID<RE::TESIdleForm>(std::exchange(f.tableIdle, 0));
            }
            return nullptr;
        }
        auto* package = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
            PosePackageLocal(slot), Settings::GetSingleton().Get().pluginName);
        if (f.restPose.kind != 1 || f.restPose.started || f.restIdleRequested ||
            actor->GetCurrentPackage() != package) {
            return nullptr;
        }
        f.restIdleRequested = true;
        return RE::TESForm::LookupByID<RE::TESIdleForm>(RestIdleID(f.activity));
    }
    void FormationController::ReportRestIdle(std::uint32_t slot, RE::Actor* actor, RE::TESIdleForm* idle,
                                             bool accepted)
    {
        std::lock_guard lock(stateMutex);
        if (ShouldSuspendForDialogue() || !actor || !idle) {
            return;
        }
        auto it = managed.find(actor->GetFormID());
        if (it == managed.end()) {
            return;
        }
        auto& f = it->second;
        if (idle->GetFormID() == CHAIR_DRINK_IDLE || idle->GetFormID() == CHAIR_EAT_IDLE) {
            logger::info("[Tavern] {} {} at the table accepted={}", actor->GetName(),
                         idle->GetFormID() == CHAIR_DRINK_IDLE ? "drinks" : "eats", accepted);
            if (!accepted) {
                f.tableIdleActive = false;
            }
            return;
        }
        if (f.slot != static_cast<int>(slot) || f.restPose.kind != 1 || !f.restIdleRequested ||
            RestIdleID(f.activity) != idle->GetFormID()) {
            return;
        }
        auto* package = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
            PosePackageLocal(slot), Settings::GetSingleton().Get().pluginName);
        if (actor->GetCurrentPackage() != package || actor->IsInCombat() || actor->GetCurrentScene()) {
            return;
        }
        logger::info("[RestPose] {} activity={} idle={:08X} accepted={} duration={:.0f}s", actor->GetName(),
                     static_cast<int>(f.activity), idle->GetFormID(), accepted, f.restPose.duration);
        if (accepted) {
            f.restPose.started = true;
            f.restPose.elapsed = 0;
        }
        else {
            EndRestPose(f, actor);
        }
    }
    RE::TESObjectREFR* FormationController::GetGatherMarker(std::uint32_t slot)
    {
        std::lock_guard lock(stateMutex);
        if (!enabled || slot >= PARTY_CAPACITY) {
            return nullptr;
        }
        for (const auto& [id, f] : managed) {
            if (f.slot == static_cast<int>(slot) && f.travelActive &&
                (Settings::GetSingleton().Get().conversationAwareness || f.restRecovery.walking ||
                 f.awarenessActive)) {
                return gatherPool[slot].get().get();
            }
        }
        return nullptr;
    }
    void FormationController::UpdatePatron(ManagedFollower& f, RE::Actor& actor, float dt)
    {
        f.patronCooldown -= dt;
        const bool eligible =
            SocialEligible(actor) && !social.Involves(actor.GetFormID()) && SettledRest(ContextFor(actor));
        if (!eligible) {
            f.patron = {};
            f.patronTimer = 0;
            f.patronGesture = false;
            return;
        }
        if (f.patronTimer > 0) {
            f.patronTimer -= dt;
            if (f.patronTimer <= 0) {
                f.patron = {};
                f.patronGesture = false;
            }
            return;
        }
        if (f.patronCooldown > 0) {
            return;
        }
        f.patronCooldown = 25.0F + RestPose::Mix(actor.GetFormID() + social.opportunities) % 21;
        RE::NiPoint3 velocity{};
        actor.GetLinearVelocity(velocity);
        if (Length({velocity.x, velocity.y}) > 20) {
            return;
        }
        auto* lists = RE::ProcessLists::GetSingleton();
        if (!lists) {
            return;
        }
        auto* npc = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13794);
        float nearest = 240;
        Engine::ForEachHighActor(*lists, [&](RE::Actor& other) {
            if (other.IsPlayerRef() || managed.contains(other.GetFormID()) || other.IsDead() ||
                other.IsInCombat() || other.GetCurrentScene() || IsSpeaking(other) ||
                SocialSpace(actor) != SocialSpace(other) || other.IsHostileToActor(&actor) ||
                !other.GetRace() || !npc || !other.GetRace()->HasKeyword(npc)) {
                return RE::BSContainer::ForEachResult::kContinue;
            }
            RE::NiPoint3 motion{};
            other.GetLinearVelocity(motion);
            const float distance = Distance2D(actor.GetPosition(), other.GetPosition());
            bool sightDetail = false;
            if (distance < nearest && Length({motion.x, motion.y}) < 20 &&
                actor.HasLineOfSight(&other, sightDetail)) {
                nearest = distance;
                f.patron = other.GetHandle();
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        if (f.patron.get()) {
            f.patronTimer = 6;
            f.patronGesture = true;
            logger::info("[Social] {} greets a nearby NPC", actor.GetName());
        }
    }

    RE::TESObjectREFR* FormationController::FreeTableChair(RE::Actor& actor, RE::NiPoint3 center) const
    {
        auto* cell = actor.GetParentCell();
        if (!cell) {
            return nullptr;
        }
        // Seats in use: whoever sits there, plus companions already on their way to one.
        std::unordered_map<RE::FormID, int> used;
        if (auto* lists = RE::ProcessLists::GetSingleton()) {
            Engine::ForEachHighActor(*lists, [&](RE::Actor& other) {
                if (auto chair = other.GetOccupiedFurniture().get()) {
                    ++used[chair->GetFormID()];
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }
        for (const auto& [id, other] : managed) {
            auto chair = other.tableSeat.get();
            auto companion = other.handle.get();
            if (chair && companion && companion->GetOccupiedFurniture().get() != chair) {
                ++used[chair->GetFormID()];
            }
        }
        RE::TESObjectREFR* best = nullptr;
        float nearest = 280;
        Engine::ForEachReferenceInRange(*cell, center, 300, [&](RE::TESObjectREFR& ref) {
            if (ref.IsDisabled() || ref.IsDeleted() || !SitFurniture(ref)) {
                return RE::BSContainer::ForEachResult::kContinue;
            }
            if (auto found = used.find(ref.GetFormID()); found != used.end() && found->second >= SeatCount(ref)) {
                return RE::BSContainer::ForEachResult::kContinue;
            }
            const auto pos = ref.GetPosition();
            if (const float distance = Distance2D(pos, center); std::abs(pos.z - center.z) < 100 && distance < nearest) {
                nearest = distance;
                best = &ref;
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        return best;
    }

    void FormationController::StartTableDrink(ManagedFollower& f, RE::Actor& actor, bool meal)
    {
        if (!f.tableSeat.get() || f.tableIdleActive) {
            return;
        }
        f.tableIdle = meal ? CHAIR_EAT_IDLE : CHAIR_DRINK_IDLE;
        f.tableIdleTime = 18.0F + static_cast<float>(RestPose::Mix(actor.GetFormID() + table.beats.sequence) % 13);
    }

    bool FormationController::CallTableWorker(const std::vector<RE::FormID>& seated)
    {
        auto* server = RE::TESForm::LookupByID<RE::TESFaction>(0xDEE93); // JobInnServer
        auto* lists = RE::ProcessLists::GetSingleton();
        if (!server || !lists || seated.empty()) {
            return false;
        }
        const auto callerID = seated[RestPose::Mix(table.beats.sequence * 13) % seated.size()];
        const auto found = managed.find(callerID);
        if (found == managed.end()) {
            return false;
        }
        auto caller = found->second.handle.get();
        auto chair = found->second.tableSeat.get();
        if (!caller || !chair) {
            return false;
        }
        // Servers only: an innkeeper belongs behind the bar.
        RE::Actor* worker = nullptr;
        float nearest = 3000;
        Engine::ForEachHighActor(*lists, [&](RE::Actor& other) {
            auto* state = other.AsActorState();
            if (other.IsPlayerRef() || managed.contains(other.GetFormID()) || !other.Is3DLoaded() || other.IsDead() ||
                other.IsInCombat() || other.GetCurrentScene() || IsSpeaking(other) || !state ||
                state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal || other.GetOccupiedFurniture().get() ||
                SocialSpace(other) != table.space || Engine::FactionRank(other, server) < 0) {
                return RE::BSContainer::ForEachResult::kContinue;
            }
            if (const float distance = Distance2D(other.GetPosition(), chair->GetPosition()); distance < nearest) {
                nearest = distance;
                worker = &other;
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        if (!worker) {
            return false;
        }
        // Open floor beside the caller's chair, on the server's side.
        const auto seat = chair->GetPosition();
        const auto from = worker->GetPosition();
        const float toward = std::atan2(from.y - seat.y, from.x - seat.x);
        std::optional<RE::NiPoint3> spot;
        for (float turn : {0.0F, .6F, -.6F, 1.2F, -1.2F, 1.8F, -1.8F}) {
            const RE::NiPoint3 desired{seat.x + std::cos(toward + turn) * 110, seat.y + std::sin(toward + turn) * 110,
                                       seat.z};
            const auto point = navigation.RestPoint(desired);
            if (!point || std::abs(point->z - seat.z) > 64) {
                continue;
            }
            bool clear = true;
            Engine::ForEachHighActor(*lists, [&](RE::Actor& other) {
                const auto p = other.GetPosition();
                if (std::abs(p.z - point->z) < 100 && Distance2D(p, *point) < 55) {
                    clear = false;
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            if (clear) {
                spot = point;
                break;
            }
        }
        if (!spot) {
            return false;
        }
        auto* mark = PrepareAuxMarker(found->second, *caller, *spot, true);
        if (!mark || !CallActorMethod(*worker, "PathToReference", static_cast<RE::TESObjectREFR*>(mark), 1.0F)) {
            return false;
        }
        table.worker = worker->GetHandle();
        table.workerMark = mark->GetHandle();
        table.caller = callerID;
        table.workerTime = 0;
        found->second.tableLook = worker->GetFormID();
        found->second.tableLookTime = 40;
        logger::info("[Tavern] {} calls {} over for drinks; distance={:.0f}", caller->GetName(), worker->GetName(),
                     nearest);
        return true;
    }

    void FormationController::DissolveTable(const char* reason)
    {
        for (auto& [id, f] : managed) {
            if (!f.tableSeat.get() && !f.tableLook) {
                continue;
            }
            auto actor = f.handle.get();
            if (f.restPose.kind == 2 && f.activity == RestActivity::TableSeat) {
                EndRestPose(f, actor.get());
                if (actor && !actor->GetCurrentScene() && !IsSpeaking(*actor)) {
                    actor->EvaluatePackage(false, true);
                }
            }
            f.tableSeat = {};
            f.tableIdle = 0;
            f.tableIdleActive = false;
            f.tableLook = 0;
        }
        if (table.host) {
            logger::info("[Tavern] the table breaks up: {}", reason);
        }
        table.Reset();
    }

    void FormationController::UpdateTavernTable(RE::PlayerCharacter& player, float dt)
    {
        const auto& tuning = Settings::GetSingleton().Get().sandbox;
        if (!sandbox.active || !tuning.areaActivities || PlaceFor(player) != RestPlace::Tavern) {
            if (table.host) {
                DissolveTable("left the inn or stopped resting");
            }
            return;
        }
        // Census: whoever sits near the table is at it, however they got to the chair.
        std::vector<RE::FormID> seated;
        bool joining = false;
        for (auto& [id, f] : managed) {
            auto actor = f.handle.get();
            if (!actor || !actor->Is3DLoaded() || !f.sandboxActive) {
                continue;
            }
            auto chair = actor->GetOccupiedFurniture().get();
            const bool sitting = chair && SeatedIn(*actor, chair.get()) && SitFurniture(*chair);
            if (!table.host && sitting && !social.Involves(id)) {
                table.host = id;
                table.center = chair->GetPosition();
                table.space = SocialSpace(*actor);
                table.beats.Reset();
                table.emptyTime = 0;
                logger::info("[Tavern] {} sits down at a table; the others will join", actor->GetName());
            }
            if (!table.host) {
                continue;
            }
            const bool atTable = sitting && SocialSpace(*actor) == table.space &&
                                 Distance2D(chair->GetPosition(), table.center) < 300 &&
                                 std::abs(chair->GetPosition().z - table.center.z) < 120;
            const bool walkingToChair = f.restPose.kind == 2 && f.activity == RestActivity::TableSeat;
            if (atTable) {
                f.tableSeat = chair->GetHandle();
                seated.push_back(id);
            }
            else if (walkingToChair) {
                joining = true;
            }
            else if (f.tableSeat.get()) {
                f.tableSeat = {}; // Got up.
                f.tableIdle = 0;
                f.tableIdleActive = false;
                f.tableLook = 0;
            }
        }
        if (!table.host) {
            return;
        }
        table.emptyTime = seated.empty() ? table.emptyTime + dt : 0;
        if (table.emptyTime > (joining ? 40.0F : 8.0F)) {
            DissolveTable("everyone got up");
            return;
        }
        if (seated.empty()) {
            return;
        }
        // Everyone else in the inn comes over and takes the nearest open seat, including
        // those already sitting at another table, strolling, or in the middle of a pose.
        // Only a conversation in progress, or having just left this table, keeps them away.
        auto* host = RE::TESForm::LookupByID<RE::Actor>(seated.front());
        for (auto& [id, f] : managed) {
            f.tableRest = std::max(0.0F, f.tableRest - dt);
            auto actor = f.handle.get();
            auto* state = actor ? actor->AsActorState() : nullptr;
            if (!actor || !state || f.tableSeat.get() || f.tableRest > 0 || !f.sandboxActive || f.socialWalk ||
                social.Involves(id) || lookout.actor == id || f.scout.Active() || !actor->Is3DLoaded() ||
                SocialSpace(*actor) != table.space || IsSpeaking(*actor) || actor->GetCurrentScene() ||
                state->IsWeaponDrawn() || (f.restPose.kind == 1 && f.activity == RestActivity::Applaud)) {
                continue;
            }
            auto* chair = FreeTableChair(*actor, table.center);
            if (!chair) {
                break; // The table is full.
            }
            auto elsewhere = actor->GetOccupiedFurniture().get();
            if (f.restPose.kind) {
                EndRestPose(f, actor.get());
            }
            if (f.restRecovery.walking) {
                f.restRecovery.Finish();
                f.activityTarget = {};
                f.pendingFeature = AreaFeature::None;
            }
            f.poseTurn = false;
            f.tableSeat = chair->GetHandle();
            f.activity = RestActivity::TableSeat;
            BeginRestPose(f, *actor, 2, AreaFeature::None);
            f.restPose.prepareLimit = 45; // Getting up from another seat and crossing the room.
            actor->EvaluatePackage(false, true);
            logger::info("[Tavern] {} joins {} at the table{}; seat {:08X} distance={:.0f}", actor->GetName(),
                         host ? host->GetName() : "the party", elsewhere ? ", leaving another seat" : "",
                         chair->GetFormID(), Distance2D(actor->GetPosition(), chair->GetPosition()));
        }
        auto pick = [&](std::uint32_t salt) {
            return seated[RestPose::Mix(table.beats.sequence * 7919 + salt) % seated.size()];
        };
        switch (table.beats.Update(dt, static_cast<int>(seated.size()))) {
        case TableBeats::Beat::Round:
            if (!table.worker.get() && !CallTableWorker(seated)) {
                // No server free: they brought drinks from the bar themselves.
                for (auto id : seated) {
                    if (auto actor = managed.at(id).handle.get()) {
                        StartTableDrink(managed.at(id), *actor, false);
                    }
                }
                logger::info("[Tavern] a round at the table; no server was free");
            }
            break;
        case TableBeats::Beat::Drink: {
            auto& f = managed.at(pick(1));
            if (auto actor = f.handle.get()) {
                StartTableDrink(f, *actor, tuning.meals && RestPose::Mix(table.beats.sequence) % 4 == 0);
            }
            break;
        }
        case TableBeats::Beat::Talk: {
            const auto a = pick(2);
            auto b = pick(3);
            for (std::uint32_t salt = 4; b == a && salt < 12; ++salt) {
                b = pick(salt);
            }
            if (a != b) {
                const float time = 20.0F + static_cast<float>(RestPose::Mix(table.beats.sequence + a) % 16);
                managed.at(a).tableLook = b;
                managed.at(b).tableLook = a;
                managed.at(a).tableLookTime = managed.at(b).tableLookTime = time;
                auto* first = RE::TESForm::LookupByID<RE::Actor>(a);
                auto* second = RE::TESForm::LookupByID<RE::Actor>(b);
                if (first && second) {
                    logger::info("[Tavern] {} and {} talk at the table for {:.0f}s", first->GetName(),
                                 second->GetName(), time);
                }
                // Talking over a drink.
                if (auto actor = managed.at(a).handle.get(); actor && RestPose::Mix(a + b) % 2) {
                    StartTableDrink(managed.at(a), *actor, false);
                }
            }
            break;
        }
        default:
            break;
        }
        // The server on the way.
        if (auto worker = table.worker.get()) {
            table.workerTime += dt;
            auto mark = table.workerMark.get();
            if (mark && Distance2D(worker->GetPosition(), mark->GetPosition()) < 120) {
                CallActorMethod(*worker, "PlayIdle", RE::TESForm::LookupByID<RE::TESIdleForm>(SERVE_IDLE));
                for (auto id : seated) {
                    if (auto actor = managed.at(id).handle.get()) {
                        StartTableDrink(managed.at(id), *actor, false);
                    }
                }
                if (auto found = managed.find(table.caller); found != managed.end()) {
                    found->second.tableLookTime = std::min(found->second.tableLookTime, 4.0F);
                }
                logger::info("[Tavern] {} brings a round to the table", worker->GetName());
                table.worker = {};
                table.workerMark = {};
            }
            else if (table.workerTime > 35 || worker->IsDead() || worker->IsInCombat() || worker->GetCurrentScene()) {
                if (auto found = managed.find(table.caller); found != managed.end()) {
                    found->second.tableLook = 0;
                }
                logger::info("[Tavern] {} never made it to the table", worker->GetName());
                table.worker = {};
                table.workerMark = {};
            }
        }
        // Mugs go down and heads turn back when their time is up.
        for (auto& [id, f] : managed) {
            if (f.tableIdleActive && (f.tableIdleTime -= dt) <= 0) {
                f.tableIdleActive = false;
                auto actor = f.handle.get();
                auto chair = f.tableSeat.get();
                if (actor && chair && SeatedIn(*actor, chair.get()) && !IsSpeaking(*actor)) {
                    actor->NotifyAnimationGraph("IdleStop");
                }
            }
            if (f.tableLook && (f.tableLookTime -= dt) <= 0) {
                f.tableLook = 0;
            }
        }
    }

    void FormationController::UpdateBardApplause(RE::PlayerCharacter& player)
    {
        const auto& tuning = Settings::GetSingleton().Get().sandbox;
        auto* bard = RE::TESForm::LookupByID<RE::TESFaction>(0x53514); // JobBardFaction
        auto* lists = RE::ProcessLists::GetSingleton();
        if (!sandbox.active || !tuning.areaActivities || !bard || !lists || PlaceFor(player) != RestPlace::Tavern) {
            bardPerforming = false;
            return;
        }
        bool performing = false;
        Engine::ForEachHighActor(*lists, [&](RE::Actor& other) {
            if (!performing && other.Is3DLoaded() && !other.IsDead() && other.GetParentCell() == player.GetParentCell() &&
                other.GetCurrentScene() && Engine::FactionRank(other, bard) >= 0) {
                performing = true;
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        if (bardPerforming && !performing) {
            // A song just ended: those standing about applaud.
            int applauding = 0;
            for (auto& [id, f] : managed) {
                auto actor = f.handle.get();
                auto* state = actor ? actor->AsActorState() : nullptr;
                if (!actor || !state || !f.sandboxActive || f.restRecovery.walking || f.restPose.kind ||
                    social.Involves(id) || f.tableSeat.get() || actor->GetOccupiedFurniture().get() ||
                    !actor->Is3DLoaded() || IsSpeaking(*actor) || actor->GetCurrentScene() ||
                    state->IsWeaponDrawn() || state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) {
                    continue;
                }
                f.activity = RestActivity::Applaud;
                BeginRestPose(f, *actor, 1, AreaFeature::None);
                actor->EvaluatePackage(false, true);
                ++applauding;
            }
            if (applauding) {
                logger::info("[Tavern] the bard finishes a song; {} companion(s) applaud", applauding);
            }
        }
        bardPerforming = performing;
    }

    bool FormationController::StartSocialWalk()
    {
        const auto walkerID = social.Walker();
        const auto walkerEntry = managed.find(walkerID);
        const auto hostEntry = managed.find(social.Partner(walkerID));
        if (walkerEntry == managed.end() || hostEntry == managed.end()) {
            return false;
        }
        auto& f = walkerEntry->second;
        auto walker = f.handle.get();
        auto host = hostEntry->second.handle.get();
        if (!walker || !host || f.restRecovery.walking || f.slot < 0 ||
            static_cast<std::size_t>(f.slot) >= markerPool.size()) {
            return false;
        }
        auto anchor = markerPool[static_cast<std::size_t>(f.slot)].get();
        const auto& settings = Settings::GetSingleton().Get();
        if (!anchor || !RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(0x870 + f.slot,
                                                                                         settings.pluginName)) {
            return false;
        }
        const auto origin = walker->GetPosition();
        const auto target = host->GetPosition();
        auto center = anchor->GetPosition();
        if (f.restAnchorZSet) {
            center.z = f.restAnchorZ;
        }
        // The partner can rest outside the walker's own area; search a disc covering both.
        const float radius = std::max({f.sandboxRadius, Distance2D(center, target) + 250,
                                       Distance2D(center, origin) + 50});
        const float height = std::max(settings.sandbox.activityHeight, std::abs(target.z - center.z) + 128);
        const auto area = navigation.RestArea(origin, center, radius, height);
        const float approach = std::atan2(origin.y - target.y, origin.x - target.x);
        for (float turn : {0.0F, .6F, -.6F, 1.2F, -1.2F}) {
            const RE::NiPoint3 desired{target.x + std::cos(approach + turn) * 95,
                                       target.y + std::sin(approach + turn) * 95, target.z};
            const auto route = navigation.FindRestDestination(area, desired);
            if (!route) {
                continue;
            }
            const RE::NiPoint3 point{route->point.x, route->point.y, route->point.z};
            if (Distance2D(point, target) > 140 || std::abs(point.z - target.z) > 96) {
                continue;
            }
            if (!PrepareAuxMarker(f, *walker, point, true)) {
                return false;
            }
            f.activityTarget = {};
            f.pendingFeature = AreaFeature::None;
            f.pendingFeatureRef = {};
            f.socialWalk = true;
            BeginRestWalk(f, RestWalkBudget(route->routeLength));
            // Started outside ApplySandbox, so switch to the walking package here.
            f.socialHold = false;
            f.socialPhase = 2;
            SetSocialFlag(f.slot, 2);
            if (!IsSpeaking(*walker) && !walker->GetCurrentScene()) {
                walker->EvaluatePackage(false, true);
            }
            logger::info("[Social] {} walks over to talk with {}; distance={:.0f} route={:.0f}",
                         walker->GetName(), host->GetName(), Distance2D(origin, target), route->routeLength);
            return true;
        }
        logger::info("[Social] {} has no route to {}; reachable={} limited={}", walker->GetName(),
                     host->GetName(), area.distance.size(), area.limited);
        return false;
    }

    void FormationController::EndSocialWalk(ManagedFollower& f, bool cancelPair)
    {
        f.socialWalk = false;
        if (cancelPair) {
            social.Cancel();
            social.cooldown = 10;
        }
        if (!f.restRecovery.walking) {
            return;
        }
        f.restRecovery.Finish();
        auto actor = f.handle.get();
        // Arrival: hold them for the talk straight away rather than handing back to native sandbox.
        f.socialHold = actor && social.Holds(actor->GetFormID());
        f.socialPhase = f.socialHold ? 1 : 0;
        SetSocialFlag(f.slot, f.socialPhase);
        if (actor && !IsSpeaking(*actor) && !actor->GetCurrentScene()) {
            actor->EvaluatePackage(false, true);
        }
    }

    void FormationController::UpdateSocial(float dt)
    {
        // Whoever held a drink through a conversation puts it down when it ends.
        auto putDownDrinks = [&]() {
            for (auto& [id, f] : managed) {
                if (f.socialLoop && !social.Involves(id)) {
                    f.socialLoop = false;
                    if (auto actor = f.handle.get(); actor && actor->Is3DLoaded() && !IsSpeaking(*actor) &&
                                                      !actor->GetCurrentScene()) {
                        actor->NotifyAnimationGraph("IdleStop");
                    }
                }
            }
        };
        const auto& tuning = Settings::GetSingleton().Get().sandbox;
        if (!sandbox.AllowsSocial(tuning)) {
            social.Reset();
            for (auto& [id, f] : managed) {
                f.patron = {};
                f.patronTimer = 0;
                f.patronGesture = false;
                if (f.socialWalk) {
                    EndSocialWalk(f, false);
                }
            }
            putDownDrinks();
            return;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto place = player ? PlaceFor(*player) : RestPlace::Wild;
        social.style = StyleFor(place, tuning.areaActivities);
        std::vector<SocialCandidate> candidates;
        for (auto& [id, f] : managed) {
            if (auto actor = f.handle.get(); actor && SocialEligible(*actor)) {
                const auto p = actor->GetPosition();
                RE::NiPoint3 velocity{};
                actor->GetLinearVelocity(velocity);
                candidates.push_back({id,
                                      {p.x, p.y},
                                      Length({velocity.x, velocity.y}) < 20,
                                      SocialSpace(*actor),
                                      SocialWeight(PersonalityFor(*actor))});
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](auto& a, auto& b) { return a.id < b.id; });
        const int before = social.count;
        const bool wasGathering = social.gathering;
        social.Update(dt, candidates, [](RE::FormID a, RE::FormID b) {
            auto* first = RE::TESForm::LookupByID<RE::Actor>(a);
            auto* second = RE::TESForm::LookupByID<RE::Actor>(b);
            bool detail = false;
            return first && second && first->HasLineOfSight(second, detail);
        });
        auto* first = RE::TESForm::LookupByID<RE::Actor>(social.members[0]);
        auto* second = RE::TESForm::LookupByID<RE::Actor>(social.members[1]);
        if (!before && social.count && first && second) {
            logger::info("[Social] conversation: {} and {}; {}", first->GetName(), second->GetName(),
                         social.gathering ? "walking over first" : "already together");
            if (social.gathering && !StartSocialWalk()) {
                social.Cancel();
                social.cooldown = 10;
            }
        }
        if (social.count && !social.gathering && (!before || wasGathering) && first && second) {
            logger::info("[Social] {} and {} talking for {:.0f}s in the {}{}", first->GetName(), second->GetName(),
                         social.duration, PlaceName(place), social.style.shareDrink ? ", over a drink" : "");
        }
        putDownDrinks();
        // The planner stops gathering on arrival (talk begins) or cancellation; end the walk either way.
        for (auto& [id, f] : managed) {
            if (f.socialWalk && social.Walker() != id) {
                EndSocialWalk(f, false);
            }
        }
        for (auto& [id, f] : managed) {
            if (auto actor = f.handle.get()) {
                UpdatePatron(f, *actor, dt);
            }
        }
        if (Settings::GetSingleton().Get().traceMovement && traceDue && !social.count) {
            logger::info(
                "[Social] eligible={} settled={} cooldown={:.1f}", candidates.size(),
                std::count_if(candidates.begin(), candidates.end(), [](auto& c) { return c.settled; }),
                social.cooldown);
        }
    }

    RE::Actor* FormationController::GetSocialTarget(std::uint32_t slot)
    {
        std::lock_guard lock(stateMutex);
        if (ShouldSuspendForDialogue()) {
            social.ClearGestures();
            return nullptr;
        }
        auto* actor = GetSlotFollower(slot);
        if (!actor) {
            return nullptr;
        }
        const auto found = managed.find(actor->GetFormID());
        if (found != managed.end() && found->second.tableLook && found->second.tableSeat.get()) {
            // Seated at the inn table: turned to a companion, or watching for the server.
            auto* other = RE::TESForm::LookupByID<RE::Actor>(found->second.tableLook);
            if (other && other->Is3DLoaded() && !other->IsDead() && SocialSpace(*other) == SocialSpace(*actor) &&
                Distance2D(actor->GetPosition(), other->GetPosition()) < 3000) {
                return other;
            }
            return nullptr;
        }
        if (!SocialEligible(*actor)) {
            return nullptr;
        }
        if (found != managed.end() && found->second.patronTimer > 0 && !social.Involves(actor->GetFormID())) {
            auto patron = found->second.patron.get();
            if (patron && !patron->IsDead() && !patron->IsInCombat() && !patron->GetCurrentScene() &&
                SocialSpace(*actor) == SocialSpace(*patron) &&
                Distance2D(actor->GetPosition(), patron->GetPosition()) <= 260) {
                RE::NiPoint3 motion{};
                actor->GetLinearVelocity(motion);
                if (Length({motion.x, motion.y}) < 20) {
                    return patron.get();
                }
            }
        }
        auto* package = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
            SandboxPackageLocal(slot), Settings::GetSingleton().Get().pluginName);
        auto* conversation = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
            SocialPackageLocal(slot), Settings::GetSingleton().Get().pluginName);
        if (found == managed.end() ||
            (actor->GetCurrentPackage() != package && actor->GetCurrentPackage() != conversation)) {
            return nullptr;
        }
        RE::NiPoint3 velocity{};
        actor->GetLinearVelocity(velocity);
        if (Length({velocity.x, velocity.y}) > 20) {
            return nullptr;
        }
        // The waiting partner watches the other approach once they are close.
        const auto partner = social.Holds(actor->GetFormID()) ? social.Partner(actor->GetFormID()) : 0;
        const float reach = social.gathering ? 600.0F : SocialPlanner::leaveDistance;
        auto* target = RE::TESForm::LookupByID<RE::Actor>(partner);
        return target && SocialEligible(*target) && SocialSpace(*actor) == SocialSpace(*target) &&
                       Distance2D(actor->GetPosition(), target->GetPosition()) <= reach
                   ? target
                   : nullptr;
    }

    RE::TESIdleForm* FormationController::ConsumeSocialIdle(std::uint32_t slot)
    {
        std::lock_guard lock(stateMutex);
        auto* actor = GetSlotFollower(slot);
        if (!actor || !GetSocialTarget(slot)) {
            return nullptr;
        }
        auto& follower = managed.at(actor->GetFormID());
        if (follower.patronTimer > 0 && follower.patronGesture) {
            follower.patronGesture = false;
            return RE::TESForm::LookupByID<RE::TESIdleForm>((social.turn + social.sequence) % 3 == 0 ? 0x58B46
                                                                                                     : 0x58B45);
        }
        if (!social.TakeGesture(actor->GetFormID())) {
            return nullptr;
        }
        if (social.style.shareDrink) {
            // At the inn the pair talk over a drink; once it is in hand, turns pass without gestures.
            return std::exchange(follower.socialLoop, true)
                       ? nullptr
                       : RE::TESForm::LookupByID<RE::TESIdleForm>(SHARED_DRINK_IDLE);
        }
        const auto& tuning = Settings::GetSingleton().Get().sandbox;
        return RE::TESForm::LookupByID<RE::TESIdleForm>(
            ConversationIdle(PlaceFor(*actor),
                             RestPose::Mix(static_cast<std::uint32_t>(social.turn) + social.sequence * 31 +
                                           actor->GetFormID()),
                             tuning.areaActivities));
    }

    void FormationController::ReportSocialIdle(RE::Actor* actor, bool accepted)
    {
        if (actor) {
            logger::info("[Social] gesture {} ({:08X}) accepted={}", actor->GetName(), actor->GetFormID(),
                         accepted);
        }
    }

    void FormationController::RemoveManaged(RE::FormID a_formID)
    {
        const auto found = managed.find(a_formID);
        if (found == managed.end()) {
            return;
        }
        auto actorPointer = found->second.handle.get();
        ReleaseTravel(found->second, actorPointer.get());
        managed.erase(found);
        diagnosticReasons.erase(a_formID);
        logger::info("[Controller] Released {:08X}", a_formID);
    }

    void FormationController::LogDiagnostic(RE::FormID a_formID, const char* a_name, const char* a_reason)
    {
        if (!Settings::GetSingleton().Get().logDiagnostics) {
            return;
        }
        std::string reason = a_reason ? a_reason : "eligible";
        const auto found = diagnosticReasons.find(a_formID);
        if (found != diagnosticReasons.end() && found->second == reason) {
            return;
        }
        logger::info("[Diag] {:08X} ({}): {}", a_formID, a_name && *a_name ? a_name : "unnamed", reason);
        diagnosticReasons[a_formID] = std::move(reason);
    }

    void FormationController::LogGlobalDiagnostic(const char* a_reason)
    {
        if (!Settings::GetSingleton().Get().logDiagnostics) {
            return;
        }
        std::string reason = a_reason ? a_reason : "travelling";
        if (lastGlobalReason == reason) {
            return;
        }
        logger::info("[Diag] global: {}", reason);
        lastGlobalReason = std::move(reason);
    }

    void FormationController::LogScanSummary(std::size_t a_high, std::size_t a_teammates,
                                             std::size_t a_inFaction)
    {
        if (!Settings::GetSingleton().Get().logDiagnostics) {
            return;
        }
        auto summary = fmt::format("high actors={} teammates={} in CurrentFollowerFaction={} managed={}",
                                   a_high, a_teammates, a_inFaction, managed.size());
        if (lastScanSummary == summary) {
            return;
        }
        logger::info("[Diag] scan: {}", summary);
        lastScanSummary = std::move(summary);
    }

    bool FormationController::IsNFFFollower(RE::Actor& actor)
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data || !data->LookupModByName("nwsFollowerFramework.esp")) {
            return false;
        }
        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(0x750BA);
        if (!quest || !quest->IsRunning()) {
            return false;
        }

        for (std::uint32_t slot = 0; slot < 12; ++slot) {
            auto* alias = GetRefAlias(quest, slot);
            if (alias && alias->GetReference() == &actor) {
                return true;
            }
        }
        return false;
    }

    void FormationController::GiveOrder(PartyOrder value)
    {
        std::lock_guard lock(stateMutex);
        if (!enabled) {
            Notify("Enable Walk With Me before issuing party orders");
            return;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->Is3DLoaded() || !player->GetParentCell()) {
            return;
        }
        order = value;
        sandbox.Reset();
        for (auto& [id, f] : managed) {
            ClearRestAnchor(f);
        }
        SetMode(value == PartyOrder::kRoam    ? FormationMode::kSandbox
                : value == PartyOrder::kScout ? FormationMode::kLead
                : value == PartyOrder::kRear  ? FormationMode::kRear
                                              : Settings::GetSingleton().Get().mode,
                false);
        order = value;

        if (!intent.IsMoving()) {
            const auto pos = player->GetPosition();
            intent.BeginOrder({pos.x, pos.y}, ForwardFromYaw(player->GetAngle().z));
        }
        scanTimer = Settings::GetSingleton().Get().scanInterval;
    }

    bool FormationController::ApplyPreferences(const SettingsData& value)
    {
        std::lock_guard lock(stateMutex);
        const auto previous = Settings::GetSingleton().Get();
        if (!Settings::GetSingleton().Save(value)) {
            return false;
        }
        const auto& settings = Settings::GetSingleton().Get();
        if (!settings.walkingBanter) {
            RestoreTravelBanter();
        }

        if (settings.enabled != enabled) {
            SetEnabled(settings.enabled, false);
        }
        if (settings.mode != previous.mode) {
            SetMode(settings.mode, false);
        }
        if (settings.maxFollowers != previous.maxFollowers) {
            std::vector<RE::FormID> excess;
            for (const auto& [id, follower] : managed) {
                if (follower.slot >= settings.maxFollowers) {
                    excess.push_back(id);
                }
            }
            for (auto id : excess) {
                RemoveManaged(id);
            }
        }
        if (settings.maxFollowers != previous.maxFollowers || settings.enforceNFF != previous.enforceNFF ||
            settings.enforceCustomFollowers != previous.enforceCustomFollowers ||
            settings.autoDiscover != previous.autoDiscover ||
            settings.disableIndoors != previous.disableIndoors) {
            scanTimer = settings.scanInterval;
            diagnosticReasons.clear();
        }
        if (settings.travel.spacing != previous.travel.spacing ||
            settings.travel.naturalStragglerDistance != previous.travel.naturalStragglerDistance ||
            settings.preferredSide != previous.preferredSide) {
            roleTimer = 1.0F;
            roleCooldown = 0;
        }
        if (!(settings.customFormation == previous.customFormation)) {
            SetCustomFormation(settings.customFormation);
            logger::info("[Formation] Custom formation saved: {} place{}", CustomFormation().count,
                         CustomFormation().count == 1 ? "" : "s");
            if (mode == FormationMode::kCustom) {
                if (!CustomFormation().Defined()) {
                    SetMode(FormationMode::kDynamic);
                }
                roleTimer = 1.0F;
                roleCooldown = 0;
                for (auto& [id, f] : managed) {
                    static_cast<void>(id);
                    f.role = -1;
                }
            }
        }
        return true;
    }

    std::vector<CommandGesture::FollowerBearing> FormationController::GestureFollowerBearings(
        RE::PlayerCharacter& player)
    {
        std::lock_guard lock(stateMutex);
        std::vector<CommandGesture::FollowerBearing> result;
        if (!enabled || mode == FormationMode::kVanilla) {
            return result;
        }
        const auto forward = ForwardFromYaw(player.GetAngleZ());
        const auto right = RightFromForward(forward);
        const auto origin = player.GetPosition();
        for (const auto& [id, follower] : managed) {
            auto actor = follower.handle.get();
            if (!actor || !actor->Is3DLoaded() || actor->IsDead() || actor->IsDisabled() ||
                actor->IsInCombat() || explicitExclusions.contains(id) ||
                !InSameTravelSpace(*actor, player)) {
                continue;
            }
            const auto delta = actor->GetPosition() - origin;
            if (std::abs(delta.z) > 200) {
                continue;
            }
            result.push_back(
                {delta.x * right.x + delta.y * right.y, delta.x * forward.x + delta.y * forward.y});
        }
        return result;
    }

    bool FormationController::LootScoutPermitted() const
    {
        return Settings::GetSingleton().Get().loot.enabled && enabled && mode != FormationMode::kVanilla &&
               !IsRelaxing() && order != PartyOrder::kRoam && !sandbox.active && loadReleaseTime <= 0;
    }

    bool FormationController::HasLootScouts() const
    {
        return std::any_of(managed.begin(), managed.end(),
                           [](const auto& entry) { return entry.second.scout.Active(); });
    }

    void FormationController::ForgetLootArea()
    {
        lootSeen.clear();
        lootBackoff.clear();
        lootContents.clear();
        lootRoutes.clear();
        lootSurvey = {};
        lootArea = {};
        lootAreaSpace = 0;
    }

    void FormationController::UpdateLootScouts(RE::PlayerCharacter& player, float dt)
    {
        const auto& tuning = Settings::GetSingleton().Get().loot;
        lootClock += dt;
        const auto opened = Loot::ConsumeActivations();
        lootSeen.insert(opened.begin(), opened.end());
        // Never in someone's house, an inn, a temple, a shop or a castle; a scout already out
        // is called back on entering one.
        const bool inhabited = !tuning.inhabitedInteriors && InhabitedInterior(PlaceFor(player));
        if (inhabited != lootInhabited) {
            lootInhabited = inhabited;
            logger::info("[Loot] {} the {}", inhabited ? "paused inside" : "resumed; left", PlaceName(PlaceFor(player)));
        }
        const bool permitted = LootScoutPermitted() && !inhabited;
        for (auto& [id, f] : managed) {
            f.scoutCooldown = std::max(0.0F, f.scoutCooldown - dt);
            if (!f.scout.Active()) {
                continue;
            }
            auto actor = f.handle.get();
            auto target = f.scoutTarget.ref.get();
            // Losing control ends the visit at once; EndLootScout still stands a kneeling follower up.
            bool released =
                !actor || !permitted || HandHolding::OwnsMovement(id) ||
                HandHolding::GetApproach().actor == id ||
                (!tuning.finder.empty() && !Loot::MayScout(tuning.finder, HandHolding::ReferenceKey(*actor)));
            if (!released) {
                if (const auto reason = UsabilityRejection(*actor, player); !reason.empty()) {
                    LogDiagnostic(id, actor->GetName(), reason.c_str());
                    released = true;
                }
            }
            if (released) {
                EndLootScout(f, actor.get(), Loot::End::Released); // Includes choosing a new finder.
                ReleaseTravel(f, actor.get());
                continue;
            }
            // Every other reason lets a kneeling follower stand up before walking away.
            auto external = Loot::End::None;
            if (!target || target->IsDisabled() || target->IsDeleted() || !target->Is3DLoaded() ||
                !InSameTravelSpace(*target, player)) {
                external = Loot::End::Lost;
            }
            else if (std::find(opened.begin(), opened.end(), f.scoutTarget.id) != opened.end()) {
                external = Loot::End::Claimed;
            }
            else if (f.scoutTarget.kind != Loot::Kind::Item && (f.scoutInspect -= dt) <= 0) {
                // Catches loot taken without an activation event, such as a quick-loot menu.
                f.scoutInspect = 1.0F;
                const auto contents = Loot::Inspect(*target, f.scoutTarget.kind, tuning.valuableGold);
                if (!Loot::WorthPointingOut(f.scoutTarget.kind, contents)) {
                    external = Loot::End::Emptied;
                }
                f.scoutTarget.valuable = f.scout.valuable = contents.valuable;
            }
            // Bodies can still be settling when they are spotted.
            const auto center = target && f.scoutTarget.kind == Loot::Kind::Body ? Loot::Center(*target)
                                                                                 : f.scoutTarget.center;
            const auto pos = actor->GetPosition();
            const bool facing = f.scout.phase == Loot::Phase::Turn && Loot::Face(*actor, center, dt);
            const auto previousPhase = f.scout.phase;
            auto end = f.scout.Tick(dt, Distance2D(pos, center) - (f.scoutTarget.reach + Loot::arrivalSlack),
                                    Distance2D(player.GetPosition(), center), tuning.leaveDistance, facing,
                                    tuning.searchSeconds, external);
            if (end == Loot::End::None && previousPhase != Loot::Phase::Present &&
                f.scout.phase == Loot::Phase::Present) {
                logger::info("[Loot] {} shows {:08X}; rejoining in {:.0f}s if left unopened", actor->GetName(),
                             f.scoutTarget.id, Loot::presentSeconds);
            }
            if (end == Loot::End::None && f.scout.reached && !f.scoutParked &&
                (!target || !Loot::Reaches(*actor, *target, center))) {
                // Stopped on the wrong side of something (a wall, bars, a railing): do not kneel at it.
                end = Loot::End::Stalled;
            }
            if (end == Loot::End::None && f.scout.reached && !f.scoutParked) {
                // Settle the travel package where they stopped, so it never tugs them out of the pose.
                // Reaching a find also spends the area: no more searching until the player moves on.
                f.scoutParked = true;
                f.scoutTarget.marker = pos;
                lootArea.Finish(center.x, center.y);
                lootAreaSpace = SocialSpace(player);
                logger::info("[Loot] {} reached {:08X}; no more finds until you move {:.0f} units away",
                             actor->GetName(), f.scoutTarget.id, tuning.areaDistance);
            }
            if (end == Loot::End::None && f.scout.ConsumeSearch()) {
                f.scoutSawIdle = false;
                f.scoutIdleGap = 0;
                f.scoutRetries = 0;
                static_cast<void>(Loot::Search(*actor, f.scoutTarget));
            }
            if (end == Loot::End::None && f.scout.phase == Loot::Phase::Search) {
                // A package re-evaluation elsewhere can knock them out of the loop; resume it.
                if (Loot::IdlePlaying(*actor)) {
                    f.scoutSawIdle = true;
                    f.scoutIdleGap = 0;
                }
                else if (f.scoutSawIdle && (f.scoutIdleGap += dt) > .75F && f.scoutRetries < 2) {
                    ++f.scoutRetries;
                    f.scoutSawIdle = false;
                    f.scoutIdleGap = 0;
                    logger::info("[Loot] {} was interrupted while searching; resuming", actor->GetName());
                    static_cast<void>(Loot::Search(*actor, f.scoutTarget));
                }
            }
            if (end == Loot::End::None && f.scout.ConsumeRise()) {
                Loot::Stand(*actor);
            }
            if (end == Loot::End::None && !DriveLootScout(f, *actor, player, false)) {
                end = Loot::End::Released;
            }
            if (end != Loot::End::None) {
                EndLootScout(f, actor.get(), end);
                ReleaseTravel(f, actor.get());
            }
        }
        lootScanTimer += dt;
        if (permitted && lootScanTimer >= 1.0F) {
            lootScanTimer = 0;
            StartLootScout(player);
        }
    }

    void FormationController::StartLootScout(RE::PlayerCharacter& player)
    {
        const auto& tuning = Settings::GetSingleton().Get().loot;
        // Only one companion is ever away at a find.
        if (HasLootScouts()) {
            lootSurvey = {};
            return;
        }
        const auto playerPos = player.GetPosition();
        if (lootArea.done && lootAreaSpace != SocialSpace(player)) {
            lootArea = {};
        }
        // One find per area: after the finder reaches one, wait until the player has moved on.
        if (lootArea.Holds(playerPos.x, playerPos.y, tuning.areaDistance)) {
            lootSurvey = {};
            return;
        }
        std::erase_if(lootBackoff, [&](const auto& entry) { return entry.second <= lootClock; });
        if (lootSeen.size() > 4096 || lootContents.size() > 4096 || lootRoutes.size() > 4096) {
            lootSeen.clear();
            lootContents.clear();
            lootRoutes.clear();
        }
        struct Scout
        {
            ManagedFollower* follower{};
            RE::NiPointer<RE::Actor> actor;
            std::optional<RestReachability> ground; // Navmesh they can walk to, explored on first need.
        };
        std::vector<Scout> free;
        const auto handPartner = HandHolding::GetApproach().actor;
        for (auto& [id, f] : managed) {
            if (f.scout.Active() || f.scoutCooldown > 0 || f.pathfinder.Active() || HandHolding::OwnsMovement(id) ||
                handPartner == id ||
                std::any_of(travelBanterPairs.begin(), travelBanterPairs.end(),
                            [&](const auto& pair) { return pair.Contains(id); })) {
                continue;
            }
            auto actor = f.handle.get();
            if (actor &&
                Loot::MayScout(tuning.finder,
                               tuning.finder.empty() ? "" : HandHolding::ReferenceKey(*actor)) &&
                !actor->GetCurrentScene() && !IsSpeaking(*actor) &&
                UsabilityRejection(*actor, player).empty()) {
                free.push_back({&f, actor});
            }
        }
        if (free.empty()) {
            lootSurvey = {};
            return;
        }
        std::vector<std::pair<float, RE::NiPointer<RE::TESObjectREFR>>> finds;
        for (const auto& handle : Loot::Collect(player, tuning.leaveDistance * .8F, tuning)) {
            auto ref = handle.get();
            if (ref && !lootSeen.contains(ref->GetFormID()) && !lootBackoff.contains(ref->GetFormID())) {
                finds.emplace_back(Distance2D(playerPos, Loot::Center(*ref)), ref);
            }
        }
        // Nearest first, so the ray and inventory budget goes where the player is.
        std::sort(finds.begin(), finds.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        struct Option
        {
            RE::NiPointer<RE::TESObjectREFR> ref;
            Scout* scout{};
            Loot::Kind kind{};
            Loot::Contents contents;
            float playerDistance{}, followerDistance{};
        };
        std::vector<Option> options;
        int rays = 0, reads = 0, routes = 0;
        bool navigationReady = false;
        for (const auto& [playerDistance, ref] : finds) {
            if (rays >= 8) {
                break;
            }
            const auto center = Loot::Center(*ref);
            Scout* scout = nullptr;
            float nearest = std::numeric_limits<float>::infinity();
            for (auto& candidate : free) {
                const auto pos = candidate.actor->GetPosition();
                const Loot::Placement placement{Distance2D(pos, center), playerDistance, center.z - pos.z};
                if (Loot::InReach(placement, tuning) && placement.followerDistance < nearest) {
                    nearest = placement.followerDistance;
                    scout = &candidate;
                }
            }
            if (!scout) {
                continue;
            }
            ++rays;
            if (!Loot::Sees(*scout->actor, *ref, center)) {
                lootBackoff[ref->GetFormID()] = lootClock + 3.0F;
                continue;
            }
            const auto kind = Loot::KindOf(*ref);
            auto cached = lootContents.find(ref->GetFormID());
            if (cached == lootContents.end()) {
                if (reads >= 6) {
                    continue; // Read the rest next second.
                }
                ++reads;
                cached =
                    lootContents.emplace(ref->GetFormID(), Loot::Inspect(*ref, kind, tuning.valuableGold))
                        .first;
            }
            if (!Loot::WorthPointingOut(kind, cached->second)) {
                lootSeen.insert(ref->GetFormID());
                continue;
            }
            // Seeing it is not enough: there must be a short walk to a spot beside it.
            auto memo = lootRoutes.find(ref->GetFormID());
            if (memo == lootRoutes.end() || memo->second.until <= lootClock) {
                if (routes >= 4) {
                    continue; // Measure the rest next second.
                }
                ++routes;
                if (!navigationReady) {
                    navigation.Collect(player);
                    navigationReady = true;
                }
                if (!scout->ground) {
                    const auto from = scout->actor->GetPosition();
                    scout->ground =
                        navigation.RestArea(from, from, tuning.searchRadius + 200, Loot::floorHeight + 100);
                }
                const auto measured = Loot::Measure(navigation, *scout->ground, *scout->actor, *ref,
                                                    Loot::Plan(*ref, kind, cached->second.valuable));
                memo = lootRoutes.insert_or_assign(ref->GetFormID(), LootRouteMemo{measured, lootClock + 5})
                           .first;
            }
            const auto& route = memo->second.route;
            if (route.Disconnected()) {
                // Behind a wall, bars or a drop with no walkable way round: never try it here.
                lootSeen.insert(ref->GetFormID());
                logger::info("[Loot] Skipped {:08X} \"{}\": no walkable way to it", ref->GetFormID(),
                             ref->GetDisplayFullName());
                continue;
            }
            if (!Loot::Walkable(route, nearest, tuning)) {
                lootBackoff[ref->GetFormID()] = lootClock + 4.0F; // Only a long way round, for now.
                continue;
            }
            options.push_back({ref, scout, kind, cached->second, playerDistance, nearest});
        }
        // Look around for a moment before choosing, so a richer find a few steps further in still wins.
        if (!lootSurvey.Update(1.0F, !options.empty())) {
            return;
        }
        lootSurvey = {};
        const auto& best =
            *std::min_element(options.begin(), options.end(), [](const auto& a, const auto& b) {
                return Loot::Richer(a.contents, a.playerDistance, b.contents, b.playerDistance);
            });
        auto& f = *best.scout->follower;
        auto& actor = *best.scout->actor;
        auto& ref = *best.ref;
        // Leave formation travel cleanly; the slot's travel package now walks them to the find.
        f.avoidance.Reset();
        f.goal.Release();
        f.individual.ResetMotion();
        f.haveRoleOffset = false;
        f.distantCatchup = false;
        TravelSpeed::Clear(static_cast<std::uint32_t>(f.slot));
        f.speedScale = 1.0F;
        f.scoutTarget = Loot::Plan(ref, best.kind, best.contents.valuable);
        f.scout.Start(best.contents.valuable);
        f.scoutInspect = 1.0F;
        lootSeen.insert(ref.GetFormID());
        if (!DriveLootScout(f, actor, player, true)) {
            f.scout.Stop();
            f.scoutTarget = {};
            return;
        }
        constexpr const char* kinds[]{"container", "body", "item"};
        logger::info("[Loot] {} chose {} {:08X} \"{}\" worth {} gold (best item {}) from {} finds in view, "
                     "{:.0f} units away{}",
                     actor.GetName(), kinds[static_cast<int>(best.kind)], ref.GetFormID(),
                     ref.GetDisplayFullName(), best.contents.total, best.contents.bestValue, options.size(),
                     best.followerDistance, best.contents.valuable ? " (valuable)" : "");
    }

    bool FormationController::DriveLootScout(ManagedFollower& f, RE::Actor& actor,
                                             RE::PlayerCharacter& player, bool retarget)
    {
        const auto& settings = Settings::GetSingleton().Get();
        auto* quest = EnsureQuest();
        auto* package = quest && quest->IsRunning()
                            ? RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                                  PackageLocal(f.slot), settings.pluginName)
                            : nullptr;
        auto* marker = package ? EnsureMarker(f, player) : nullptr;
        if (!marker) {
            return false;
        }
        const auto goal = f.scoutTarget.marker;
        if (retarget || marker->GetPosition().GetDistance(goal) > 8) {
            if (marker->GetParentCell() != player.GetParentCell()) {
                marker->MoveTo(&player);
            }
            marker->SetPosition(goal);
            retarget = true;
        }
        const bool distant =
            f.scout.phase == Loot::Phase::Approach && Distance2D(actor.GetPosition(), goal) > 600;
        package->packData.packFlags.set(RE::PACKAGE_DATA::GeneralFlag::kPreferredSpeed);
        package->packData.maxSpeed =
            static_cast<RE::PACKAGE_DATA::PreferredSpeed>(distant ? TravelPace::kJog : TravelPace::kWalk);
        f.travelActive = true;
        if (retarget && actor.GetCurrentPackage() == package) {
            actor.EvaluatePackage(false, true);
        }
        return true;
    }

    void FormationController::EndLootScout(ManagedFollower& f, RE::Actor* actor, Loot::End reason)
    {
        if (!f.scout.Active()) {
            return;
        }
        // Never leave a follower frozen in the kneel, whatever ended the visit.
        if (actor && f.scout.phase == Loot::Phase::Search && actor->Is3DLoaded() && !actor->IsDead()) {
            Loot::Stand(*actor);
        }
        logger::info("[Loot] {} leaves {:08X} after {:.1f}s: {}", actor ? actor->GetName() : "Companion",
                     f.scoutTarget.id, f.scout.elapsed, Loot::EndName(reason));
        f.scout.Stop();
        f.scoutTarget = {};
        f.scoutParked = f.scoutSawIdle = false;
        f.scoutIdleGap = 0;
        f.scoutRetries = 0;
        f.scoutCooldown = Loot::Cooldown(reason);
    }

    // Pathfinder. Only in the styles where the party walks together (Natural, Lead, Companion,
    // Custom), outdoors, and never while fighting, sneaking or riding. Returns why not, or null.
    const char* FormationController::PathfinderRejection(RE::PlayerCharacter& player) const
    {
        if (!Settings::GetSingleton().Get().pathfinder.enabled) {
            return "switched off";
        }
        if (!enabled) {
            return "Walk With Me is off";
        }
        if (mode != FormationMode::kDynamic && mode != FormationMode::kLead && mode != FormationMode::kCompanion &&
            mode != FormationMode::kCustom) {
            return "not in this travel style";
        }
        if (sandbox.active || regroup.active || loadReleaseTime > 0) {
            return "the party is resting, regrouping or just loaded";
        }
        const auto* cell = player.GetParentCell();
        if (!cell || cell->IsInteriorCell() || !player.GetWorldspace()) {
            return "indoors";
        }
        if (player.IsInCombat()) {
            return "in combat";
        }
        if (player.IsSneaking()) {
            return "sneaking";
        }
        if (player.IsOnMount()) {
            return "riding";
        }
        return nullptr;
    }

    // A quiet Pathfinder says why in the log: at once when the kind of reason changes, and at
    // most every 30 seconds while only its details (distances, counts) move.
    void FormationController::NotePathfinder(std::string key, std::string detail)
    {
        if (key == pathfinderNoteKey && (detail == pathfinderNote || pathfinderNoteAge < 30)) {
            return;
        }
        pathfinderNoteKey = std::move(key);
        pathfinderNote = std::move(detail);
        pathfinderNoteAge = 0;
        if (!pathfinderNote.empty()) {
            logger::info("[Pathfinder] waiting: {}", pathfinderNote);
        }
    }

    // Where to run: a little past the stopping distance from the player toward the place, since
    // the leg ends on the distance from the player. A straight line often meets a rock, a stream
    // bank or the end of the navmesh gathered around the player, so lines either side of it are
    // tried too, and whichever ends nearest the place wins.
    std::optional<RE::NiPoint3> FormationController::PathfinderGround(RE::PlayerCharacter& player,
                                                                      RE::NiPoint3 place) const
    {
        const auto from = player.GetPosition();
        const Vec2 toPlace{place.x - from.x, place.y - from.y};
        const float length = Pathfinder::RunLength(
            Length(toPlace), Settings::GetSingleton().Get().pathfinder.run + Pathfinder::overshoot);
        if (length < Pathfinder::minRun) {
            return std::nullopt;
        }
        TravelNavigation ground; // The usual 1200-unit gathering stops short of a long run.
        ground.Collect(player, length + 400.0F);
        std::optional<RE::NiPoint3> best;
        float left = std::numeric_limits<float>::infinity();
        for (const float degrees : {0.0F, 15.0F, -15.0F, 30.0F, -30.0F}) {
            const auto point = Pathfinder::RunPoint({from.x, from.y}, intent.Direction(), toPlace, length, degrees);
            const auto hit = ground.Reach(from, point);
            if (!hit || Distance2D(*hit, from) < Pathfinder::minRun) {
                continue;
            }
            if (const float distance = Distance2D(*hit, place); distance < left) {
                left = distance;
                best = hit;
            }
            if (degrees == 0.0F && Length({point.x - hit->x, point.y - hit->y}) < 1.0F) {
                break; // The straight line runs the whole way.
            }
        }
        return best;
    }

    void FormationController::StartPathfinder(RE::PlayerCharacter& player)
    {
        const auto& tuning = Settings::GetSingleton().Get().pathfinder;
        if (pathfinderCooldown > 0) {
            return;
        }
        for (const auto& [id, f] : managed) {
            static_cast<void>(id);
            if (f.pathfinder.Active() || f.scout.Active()) {
                return; // One companion away from the party at a time.
            }
        }
        // The nearest undiscovered place worth pointing out. Map markers are persistent: they live
        // in the worldspace's persistent cell, and the player keeps the current worldspace's list
        // for the map and compass. Both are read, in case a worldspace only fills one.
        const auto pos = player.GetPosition();
        RE::TESObjectREFR* place = nullptr;
        int type = 0, markers = 0, undiscovered = 0;
        float nearest = std::numeric_limits<float>::infinity();
        std::unordered_set<RE::FormID> checked;
        const auto consider = [&](RE::TESObjectREFR& ref) {
            if (!checked.insert(ref.GetFormID()).second) {
                return;
            }
            auto* extra = ref.extraList.GetByType<RE::ExtraMapMarker>();
            const auto* data = extra ? extra->mapData : nullptr;
            const auto at = ref.GetPosition();
            const Vec2 toPlace{at.x - pos.x, at.y - pos.y};
            const float distance = Length(toPlace);
            if (!data || !(distance <= tuning.range)) {
                return;
            }
            ++markers;
            const int kind = static_cast<int>(data->type.get());
            if (ref.IsDisabled() || data->flags.all(RE::MapMarkerData::Flag::kCanTravelTo) ||
                !Pathfinder::IsPlace(kind)) {
                return;
            }
            ++undiscovered;
            if (!pathfinderSeen.contains(ref.GetFormID()) && distance < nearest &&
                Pathfinder::WorthPointing(intent.Direction(), toPlace, distance, tuning.range)) {
                nearest = distance;
                place = &ref;
                type = kind;
            }
        };
        if (auto* world = player.GetWorldspace(); world && world->persistentCell) {
            Engine::ForEachReferenceInRange(*world->persistentCell, pos, tuning.range, [&](RE::TESObjectREFR& ref) {
                consider(ref);
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }
        if (const auto* list = Engine::CurrentMapMarkers(player); list && list->size() < 20000) {
            for (const auto& handle : *list) {
                if (const auto ref = handle.get()) {
                    consider(*ref);
                }
            }
        }
        if (!place) {
            NotePathfinder("place",
                           undiscovered ? fmt::format("{} undiscovered place{} within {:.0f} units, none ahead of "
                                                      "you and at least {:.0f} away",
                                                      undiscovered, undiscovered == 1 ? "" : "s", tuning.range,
                                                      Pathfinder::spotMin)
                                        : fmt::format("no undiscovered place within {:.0f} units ({} map marker{} "
                                                      "nearby)",
                                                      tuning.range, markers, markers == 1 ? "" : "s"));
            return;
        }
        const auto target = place->GetPosition();
        const Vec2 toPlace{target.x - pos.x, target.y - pos.y};
        const auto spotted = fmt::format("{} ({:08X}) {:.0f} units {}", Pathfinder::Noun(type), place->GetFormID(),
                                         nearest, Pathfinder::Bearing(toPlace));
        // Seen from a companion's eyes or the player's, at three heights: a tower or a fort wall
        // shows over a rise that hides a cave mouth.
        const auto sees = [&](RE::TESObjectCELL* cell, RE::NiPoint3 eye) {
            for (const float lift : {250.0F, 700.0F, 1400.0F}) {
                const RE::NiPoint3 aim{target.x, target.y, target.z + lift};
                if (Pathfinder::Visible(Loot::SightFraction(cell, eye, aim), eye.GetDistance(aim))) {
                    return true;
                }
            }
            return false;
        };
        auto playerEye = pos;
        playerEye.z += 120;
        const bool playerSees = sees(player.GetParentCell(), playerEye);
        // Someone travelling close by: the chosen pathfinder, or anyone with a curious companion first.
        constexpr float reach = 800.0F;
        const auto dash = Pathfinder::RunPoint({pos.x, pos.y}, intent.Direction(), toPlace,
                                               Pathfinder::RunLength(nearest, tuning.run));
        ManagedFollower* chosen = nullptr;
        RE::Actor* chosenActor = nullptr;
        float best = std::numeric_limits<float>::infinity();
        std::string unavailable;
        int ready = 0;
        for (auto& [id, f] : managed) {
            auto actor = f.handle.get();
            if (!actor || (!tuning.companion.empty() && HandHolding::ReferenceKey(*actor) != tuning.companion)) {
                continue;
            }
            std::string reason;
            if (!actor->Is3DLoaded()) {
                reason = "not nearby";
            }
            else if (!f.travelActive) {
                reason = "not travelling in formation";
            }
            else if (f.scout.Active()) {
                reason = "scouting loot";
            }
            else if (HandHolding::OwnsMovement(id) || HandHolding::GetApproach().actor == id) {
                reason = "holding hands";
            }
            else if (Distance2D(actor->GetPosition(), pos) > reach) {
                reason = "too far from you";
            }
            else {
                reason = UsabilityRejection(*actor, player);
            }
            if (!reason.empty()) {
                if (unavailable.empty()) {
                    unavailable = fmt::format("{}: {}", actor->GetName(), reason);
                }
                continue;
            }
            ++ready;
            auto eye = actor->GetPosition();
            eye.z += 120;
            if (!playerSees && !sees(actor->GetParentCell(), eye)) {
                continue;
            }
            const auto at = actor->GetPosition();
            const float score = Length({at.x - dash.x, at.y - dash.y}) -
                                (PersonalityFor(*actor) == Personality::Curious ? 400.0F : 0.0F);
            if (score < best) {
                best = score;
                chosen = &f;
                chosenActor = actor.get();
            }
        }
        if (!chosen) {
            // Asked again on the next scan.
            if (ready) {
                NotePathfinder("sight", fmt::format("{} ahead, but hidden from view", spotted));
            }
            else if (!unavailable.empty()) {
                NotePathfinder("companion", fmt::format("{} ahead, but {}", spotted, unavailable));
            }
            else if (!tuning.companion.empty()) {
                NotePathfinder("companion", fmt::format("{} ahead, but {} is not in the party", spotted,
                                                        tuning.companionName));
            }
            else {
                NotePathfinder("companion", fmt::format("{} ahead, but no companion is travelling with you", spotted));
            }
            return;
        }
        const auto ground = PathfinderGround(player, target);
        if (!ground) {
            NotePathfinder("ground", fmt::format("{} ahead, but no open ground to run to", spotted));
            return;
        }
        NotePathfinder({}, {});
        pathfinderSeen.insert(place->GetFormID());
        pathfinderCooldown = tuning.cooldown;
        const float toGoal = Distance2D(chosenActor->GetPosition(), *ground);
        chosen->pathfinder.Start(toGoal, Distance2D(chosenActor->GetPosition(), target));
        chosen->pathfinderRef = place->CreateRefHandle();
        chosen->pathfinderPlace = target;
        chosen->pathfinderGoal = *ground;
        chosen->pathfinderFrom = pos;
        chosen->pathfinderNoun = Pathfinder::Noun(type);
        chosen->goal.Release();
        logger::info("[Pathfinder] {} spotted {}; running {:.0f} units toward it, to {:.0f} from it ({})",
                     chosenActor->GetName(), spotted, toGoal, Distance2D(*ground, target),
                     playerSees ? "in plain view" : "they saw it first");
    }

    void FormationController::UpdatePathfinder(RE::PlayerCharacter& player, float dt)
    {
        pathfinderCooldown = std::max(0.0F, pathfinderCooldown - dt);
        pathfinderNoteAge += dt;
        const char* rejection = PathfinderRejection(player);
        const bool permitted = !rejection;
        const auto playerPos = player.GetPosition();
        const auto& settings = Settings::GetSingleton().Get();
        const auto& tuning = settings.pathfinder;
        const auto rules = Pathfinder::RulesFor(tuning);
        // Once the player discovers the place, the banner says the rest.
        const auto discovered = [](RE::ObjectRefHandle handle) {
            const auto ref = handle.get();
            auto* extra = ref ? ref->extraList.GetByType<RE::ExtraMapMarker>() : nullptr;
            return extra && extra->mapData && extra->mapData->flags.all(RE::MapMarkerData::Flag::kCanTravelTo);
        };
        for (auto& [id, f] : managed) {
            if (!f.pathfinder.Active()) {
                continue;
            }
            auto actor = f.handle.get();
            if (!actor || !permitted || f.scout.Active() || HandHolding::OwnsMovement(id) ||
                HandHolding::GetApproach().actor == id || !UsabilityRejection(*actor, player).empty()) {
                EndPathfinder(f, actor.get(), Pathfinder::End::Interrupted);
                continue;
            }
            if (discovered(f.pathfinderRef)) {
                EndPathfinder(f, actor.get(), Pathfinder::End::Found);
                continue;
            }
            const auto pos = actor->GetPosition();
            const bool dashing = f.pathfinder.phase == Pathfinder::Phase::Dash;
            if (dashing && Distance2D(playerPos, f.pathfinderFrom) > Pathfinder::retargetDistance) {
                // The player walked on: the running point stays the same distance out from them.
                f.pathfinderFrom = playerPos;
                if (const auto ground = PathfinderGround(player, f.pathfinderPlace)) {
                    f.pathfinderGoal = *ground;
                    f.pathfinder.Retarget(Distance2D(pos, *ground));
                }
            }
            // Turn to the place to point it out; otherwise to the player, to wave them on and watch
            // them come.
            const bool facing =
                !dashing && Loot::Face(*actor, f.pathfinder.phase == Pathfinder::Phase::Point ? f.pathfinderPlace
                                                                                            : playerPos,
                                       dt);
            const Pathfinder::Sense sense{Distance2D(pos, f.pathfinderGoal), Distance2D(pos, playerPos),
                                          Distance2D(pos, f.pathfinderPlace),
                                          Distance2D(playerPos, f.pathfinderPlace), facing};
            Pathfinder::Cue cue;
            auto end = f.pathfinder.Tick(dt, sense, rules, cue);
            if (dashing && f.pathfinder.phase != Pathfinder::Phase::Dash) {
                const char* why = sense.toGoal <= Pathfinder::arrival ? "arrived"
                                  : sense.fromPlayer >= rules.stopAt - Pathfinder::farEnough
                                      ? "far enough ahead"
                                  : f.pathfinder.stalled >= Pathfinder::stallSeconds ? "no longer gaining ground"
                                                                                     : "out of time";
                logger::info("[Pathfinder] {} stops {:.0f} units from you and {:.0f} from it after {:.1f}s ({}{})",
                             actor->GetName(), sense.fromPlayer, sense.toPlace, f.pathfinder.elapsed, why,
                             f.pathfinder.last && tuning.lead ? "; the last stop" : "");
                f.pathfinderGoal = pos; // Stop here for the wave and the point.
            }
            if (cue.leadOn) {
                // The player came close: on toward the place, the same distance out from them again,
                // unless that would gain nothing.
                f.pathfinderFrom = playerPos;
                const auto ground = PathfinderGround(player, f.pathfinderPlace);
                const float gain = ground ? sense.toPlace - Distance2D(*ground, f.pathfinderPlace) : 0.0F;
                if (ground && gain >= Pathfinder::minLeg) {
                    f.pathfinderGoal = *ground;
                    f.pathfinder.Lead(Distance2D(pos, *ground), sense.toPlace);
                    logger::info("[Pathfinder] {} leads on, {:.0f} units closer to it", actor->GetName(), gain);
                }
                else {
                    f.pathfinder.Close();
                    logger::info("[Pathfinder] {} can get no closer ({:.0f} units from it)", actor->GetName(),
                                 sense.toPlace);
                }
            }
            if (cue.wave || cue.point) {
                constexpr RE::FormID idleWave = 0x3EA32, idlePointFar[]{0xB8F1E, 0xB8F1F};
                const bool played =
                    Loot::PlayGesture(*actor, cue.wave ? idleWave : idlePointFar[RestPose::Mix(id) % 2]);
                // Named once, when first pointed out; the point on arrival needs no words.
                if (cue.point && !f.pathfinder.closing && tuning.notice) {
                    const Vec2 toPlace{f.pathfinderPlace.x - playerPos.x, f.pathfinderPlace.y - playerPos.y};
                    Notify(fmt::format("{} points out {} to the {}.", actor->GetName(), f.pathfinderNoun,
                                       Pathfinder::Bearing(toPlace)));
                }
                logger::info("[Pathfinder] {} {} ({})", actor->GetName(),
                             cue.point                                          ? "points"
                             : f.pathfinder.phase == Pathfinder::Phase::Wait    ? "waves you on again"
                                                                                : "waves",
                             played ? "idle accepted" : "idle refused");
            }
            if (end == Pathfinder::End::None) {
                // Borrow the slot's own travel package, as loot scouting does: its marker runs ahead.
                auto* quest = EnsureQuest();
                auto* package = quest && quest->IsRunning()
                                    ? RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(
                                          PackageLocal(f.slot), settings.pluginName)
                                    : nullptr;
                auto* marker = package ? EnsureMarker(f, player) : nullptr;
                if (!marker) {
                    end = Pathfinder::End::Interrupted;
                }
                else {
                    bool retarget = false;
                    if (marker->GetPosition().GetDistance(f.pathfinderGoal) > 8) {
                        if (marker->GetParentCell() != player.GetParentCell()) {
                            marker->MoveTo(&player);
                        }
                        marker->SetPosition(f.pathfinderGoal);
                        retarget = true;
                    }
                    const bool run = f.pathfinder.phase == Pathfinder::Phase::Dash;
                    package->packData.packFlags.set(RE::PACKAGE_DATA::GeneralFlag::kPreferredSpeed);
                    package->packData.maxSpeed =
                        static_cast<RE::PACKAGE_DATA::PreferredSpeed>(run ? TravelPace::kRun : TravelPace::kWalk);
                    f.speedScale = SmoothSpeedScale(f.speedScale, run ? 1.35F : 1.0F, dt);
                    TravelSpeed::Set(static_cast<std::uint32_t>(f.slot), actor->GetFormID(), f.speedScale);
                    f.travelActive = true;
                    if (retarget && actor->GetCurrentPackage() == package) {
                        actor->EvaluatePackage(false, true);
                    }
                }
            }
            if (end != Pathfinder::End::None) {
                EndPathfinder(f, actor.get(), end);
            }
        }
        pathfinderScan += dt;
        if (intent.IsMoving() && pathfinderScan >= Pathfinder::scanSeconds) {
            pathfinderScan = 0;
            if (permitted) {
                StartPathfinder(player);
            }
            else {
                NotePathfinder(rejection, rejection);
            }
        }
    }

    void FormationController::EndPathfinder(ManagedFollower& f, RE::Actor* actor, Pathfinder::End reason)
    {
        if (!f.pathfinder.Active()) {
            return;
        }
        logger::info("[Pathfinder] {} rejoins after {:.1f}s and {} stop{}: {}", actor ? actor->GetName() : "Companion",
                     f.pathfinder.elapsed, f.pathfinder.stops, f.pathfinder.stops == 1 ? "" : "s",
                     Pathfinder::EndName(reason));
        f.pathfinder.Stop();
        f.pathfinderRef = {};
        f.goal.Release(); // Travel recommits to their place in the formation.
        // A trip can outlast the cooldown; the next sighting waits a full one from here.
        pathfinderCooldown = std::max(pathfinderCooldown, Settings::GetSingleton().Get().pathfinder.cooldown);
    }

    bool FormationController::HasPathfinder() const
    {
        return std::any_of(managed.begin(), managed.end(),
                           [](const auto& entry) { return entry.second.pathfinder.Active(); });
    }

    std::vector<FormationController::LootScoutView> FormationController::LootScouts() const
    {
        std::lock_guard lock(stateMutex);
        std::vector<LootScoutView> result;
        for (const auto& [id, f] : managed) {
            if (f.scout.Active() && !explicitExclusions.contains(id)) {
                result.push_back({f.handle, f.scout.valuable, f.scout.elapsed});
            }
        }
        return result;
    }

    std::vector<FormationController::LootScoutView> FormationController::Pathfinders() const
    {
        std::lock_guard lock(stateMutex);
        std::vector<LootScoutView> result;
        for (const auto& [id, f] : managed) {
            // The trip ends as soon as the last point is held, or the moment it is called off, so
            // the emblem goes with it.
            if (f.pathfinder.Active() && !explicitExclusions.contains(id)) {
                result.push_back({f.handle, false, f.pathfinder.elapsed});
            }
        }
        return result;
    }

    std::vector<RE::ActorHandle> FormationController::RestDialogueActors() const
    {
        std::lock_guard lock(stateMutex);
        std::vector<RE::ActorHandle> result;
        if (!enabled || !sandbox.active) {
            return result;
        }
        for (const auto& [id, follower] : managed) {
            if (follower.sandboxActive && !explicitExclusions.contains(id)) {
                result.push_back(follower.handle);
            }
        }
        return result;
    }

    PartyView FormationController::Snapshot()
    {
        std::lock_guard lock(stateMutex);
        PartyView view;
        view.settings = Settings::GetSingleton().Get();
        view.enabled = enabled;
        view.order = order;
        view.mode = mode;
        view.generation = generation;
        view.sandboxActive = sandbox.active;
        view.sandboxRadius = sandbox.radius;
        auto* data = RE::TESDataHandler::GetSingleton();
        view.nffInstalled = data && data->LookupModByName("nwsFollowerFramework.esp");
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        view.inGame = initialized && player && player->Is3DLoaded() && player->GetParentCell() && ui &&
                      !ui->IsMenuOpen(RE::MainMenu::MENU_NAME) && !ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
        if (!view.inGame) {
            return view;
        }
        view.recallCount = static_cast<int>(RecallCandidates().size());
        view.recallPending = recallRequest.pending;
        if (const auto* reason = RecallPlayerRejection(*player)) {
            view.recallReason = reason;
        }
        else if (!view.recallCount) {
            view.recallReason = "No travelling companions available";
        }
        view.gameplay = !ui->GameIsPaused();
        for (const auto* menuName :
             {"Dialogue Menu", "Console", "TweenMenu", "InventoryMenu", "MagicMenu", "ContainerMenu",
              "BarterMenu", "MapMenu", "Journal Menu", "Crafting Menu", "StatsMenu", "FavoritesMenu",
              "RaceSex Menu", "Book Menu", "Sleep/Wait Menu", "MessageBoxMenu"}) {
            if (ui->IsMenuOpen(menuName)) {
                view.gameplay = false;
            }
        }
        auto reason = GlobalRejection(*player);
        view.state = !enabled                          ? "Disabled"
                     : mode == FormationMode::kVanilla ? "Vanilla follower AI"
                     : reason                          ? reason
                     : sandbox.active                  ? "Sandboxing"
                     : intent.CanTravel()              ? "Travelling"
                                                       : "Ready - move to travel";
        if (auto* pick = RE::CrosshairPickData::GetSingleton()) {
#ifdef WAYFARER_MODERN_COMMONLIB
            auto target = pick->GetActiveTarget().get();
#else
            auto target = pick->target.get();
#endif
            if (auto* actor = target ? target->As<RE::Actor>() : nullptr) {
                view.crosshairActor = actor->GetFormID();
            }
        }
        auto addPackageTarget = [&](RE::FormID id, RE::Actor* actor) {
            PackageControlView row;
            row.id = id;
            row.name = actor ? actor->GetName() : fmt::format("Unavailable NPC ({:08X})", id);
            const auto registration = packageRegistrations.find(id);
            row.controlled = registration != packageRegistrations.end();
            row.manualReleaseRequired = row.controlled
                                            ? registration->second == 1
                                            : actor && !IsNFFFollower(*actor) && !actor->IsPlayerTeammate();
            const auto* why = actor ? PackageControlStartRejection(*actor) : "NPC is not loaded";
            row.canTake = !row.controlled && !why;
            const auto* pushWhy = actor ? CompanionPushRejection(*actor) : "NPC is not loaded";
            row.pushReason = pushWhy                                ? pushWhy
                             : companionPush.Busy(GetTickCount64()) ? "Wait a moment before another push"
                                                                    : "";
            row.canPush = row.pushReason.empty();
            if (row.controlled) {
                const auto status = actor ? UsabilityRejection(*actor, *player) : "NPC is not loaded";
                const char* lost = actor ? PackageControlLoss(*actor, registration->second) : nullptr;
                row.state = !enabled                          ? "Control paused: Walk With Me is disabled"
                            : mode == FormationMode::kVanilla ? "Control paused: Vanilla mode"
                            : PackageControlSuspended(id)
                                ? fmt::format("Paused until they follow you again{}",
                                              lost ? fmt::format(" ({})", lost) : std::string{})
                            : !status.empty()       ? "Control paused: " + status
                            : !managed.contains(id) ? "Waiting for a free party slot"
                                                    : "Walk With Me controls travel and rest";
            }
            else {
                row.state = why ? why : "Ready for explicit travel control";
            }
            view.packageTargets.push_back(std::move(row));
        };
        for (const auto& [id, flags] : packageRegistrations) {
            static_cast<void>(flags);
            addPackageTarget(id, RE::TESForm::LookupByID<RE::Actor>(id));
        }
        // A focused popup keeps its target even if the crosshair moves. No nearby-NPC picker.
        for (const auto id : {view.crosshairActor, Menu::CompanionTarget()}) {
            if (id && std::none_of(view.packageTargets.begin(), view.packageTargets.end(),
                                   [id](const auto& row) { return row.id == id; })) {
                if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(id)) {
                    addPackageTarget(id, actor);
                }
            }
        }
        if (auto* lists = RE::ProcessLists::GetSingleton()) {
            Engine::ForEachHighActor(*lists, [&](RE::Actor& actor) {
                if (actor.IsPlayerRef() || actor.IsDead() || !InSameTravelSpace(actor, *player)) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                const bool nff = IsNFFFollower(actor);
                const auto id = actor.GetFormID();
                const bool eligible = IsDialogueFollower(actor);
                // A teammate from a framework Walk With Me cannot read is listed too, so it can be added.
                const bool teammate = actor.IsPlayerTeammate() && !actor.IsHostileToActor(player);
                if (!eligible && !teammate && !dialogueRegistrations.contains(id) &&
                    !manualRegistrations.contains(id) && !packageRegistrations.contains(id)) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                CompanionView row;
                row.id = id;
                row.name = actor.GetName();
                row.nff = nff;
                row.handHoldKey = HandHolding::ReferenceKey(actor);
                row.personalityKey = PersonalityKey(actor);
                row.personality = PersonalityName(PersonalityFor(actor));
                if (auto pref = view.settings.personalityOverrides.find(row.personalityKey);
                    pref != view.settings.personalityOverrides.end()) {
                    row.personalityOverride = pref->second;
                }
                if (lookout.actor == id) {
                    row.personality += " - keeping watch";
                }
                row.managed = managed.contains(id);
                row.excluded = explicitExclusions.contains(id);
                row.manuallyAdded = dialogueRegistrations.contains(id);
                row.apiRegistered = manualRegistrations.contains(id);
                row.packageControlled = packageRegistrations.contains(id);
                row.eligible = eligible;
                row.teammate = teammate;
                const bool added = row.manuallyAdded || row.apiRegistered || row.packageControlled;
                if (row.excluded) {
                    row.state = "Left to follower AI";
                }
                else if (!enabled || mode == FormationMode::kVanilla) {
                    row.state = added ? "Added - joins when a travel style is chosen" : "Left to follower AI";
                }
                else if (row.packageControlled && PackageControlSuspended(id)) {
                    row.state = "Added - paused until they follow you again";
                }
                else if (!eligible && !added) {
                    row.state = "Following you - not added";
                }
                else if (row.manuallyAdded && !eligible) {
                    row.state = "Added - not currently following";
                }
                else if (!row.manuallyAdded && !row.apiRegistered && !row.packageControlled &&
                         !view.settings.autoDiscover) {
                    row.state = "Detected - not added";
                }
                else if (auto why = CandidateRejection(actor); why && !IsRegisteredFollower(id)) {
                    row.state = why;
                }
                else if (!row.managed) {
                    row.state = "Waiting for a free party slot";
                }
                else if (auto unavailable = UsabilityRejection(actor, *player); !unavailable.empty()) {
                    row.state = unavailable;
                }
                else if (managed.at(id).travelActive) {
                    const auto& f = managed.at(id);
                    row.state = f.scout.Active()
                                    ? (f.scout.valuable ? "Found something valuable" : "Found something")
                                : f.sandboxActive ? "Sandboxing"
                                                  : "Travelling with you";
                    ++view.activeCount;
                }
                else {
                    row.state = "Ready";
                }
                view.party.push_back(std::move(row));
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }
        std::sort(view.party.begin(), view.party.end(),
                  [](const auto& a, const auto& b) { return a.id < b.id; });
        std::sort(view.packageTargets.begin(), view.packageTargets.end(), [](const auto& a, const auto& b) {
            if (a.controlled != b.controlled) {
                return a.controlled;
            }
            return a.id < b.id;
        });
        return view;
    }

    void FormationController::RegisterSerialization()
    {
        auto* serial = SKSE::GetSerializationInterface();
        if (!serial) {
            return;
        }
        serial->SetUniqueID(0x57415946);
        serial->SetRevertCallback([](SKSE::SerializationInterface*) {
            CommandGesture::RestoreSelector(false);
            CommandGesture::HeadTracking::RestoreOwnership(0);
            auto& c = GetSingleton();
            std::lock_guard lock(c.stateMutex);
            c.ResetVictoryCelebration(false);
            c.explicitExclusions.clear();
            c.dialogueRegistrations.clear();
            c.packageRegistrations.clear();
            c.packageLossSince.clear();
            c.packageSeenFollowing.clear();
            c.restoredRelax = false;
            c.restoredRelaxSlots.clear();
            c.restoredVanilla = false;
        });
        serial->SetSaveCallback([](SKSE::SerializationInterface* api) {
            const std::uint32_t gestureOwned = CommandGesture::SavedOwnership();
            const auto headOwned = CommandGesture::HeadTracking::SavedOwnership();
            if (headOwned && !api->WriteRecord(0x57484544, 1, &headOwned, sizeof(headOwned))) {
                logger::error("[Save] Could not write gesture head cleanup ownership");
            }
            if (gestureOwned && !api->WriteRecord(0x57475354, 3, &gestureOwned, sizeof(gestureOwned))) {
                logger::error("[Save] Could not write gesture cleanup ownership");
            }
            auto& c = GetSingleton();
            std::lock_guard lock(c.stateMutex);
            if (c.mode == FormationMode::kVanilla) {
                const std::uint32_t vanilla = 1;
                if (!api->WriteRecord(0x56414E4C, 1, &vanilla, sizeof(vanilla))) {
                    logger::error("[Save] Could not write Vanilla handoff state");
                }
            }
            if (c.IsRelaxing()) {
                std::vector<std::uint32_t> record{0};
                for (const auto& [id, f] : c.managed) {
                    if (f.anchorSet) {
                        ++record[0];
                        record.push_back(id);
                        record.push_back(f.slot);
                    }
                }
                if (!api->WriteRecord(0x52454C58, 1, record.data(),
                                      static_cast<std::uint32_t>(record.size() * sizeof(std::uint32_t)))) {
                    logger::error("[Save] Could not write Relax anchors");
                }
            }
            for (const auto id : c.dialogueRegistrations) {
                if (!api->WriteRecord(0x444C474D, 1, &id, sizeof(id))) {
                    logger::error("[Save] Could not write manual dialogue follower");
                }
            }
            for (const auto& [id, flags] : c.packageRegistrations) {
                const std::array<std::uint32_t, 2> record{id, flags};
                if (!api->WriteRecord(0x5043544C, 1, record.data(), sizeof(record))) {
                    logger::error("[Save] Could not write explicit package control");
                }
            }
            for (const auto id : c.explicitExclusions) {

                const std::array<std::uint32_t, 6> record{id,         0xFFFFFFFF, 0xFFFFFFFF,
                                                          0xFFFFFFFF, 0xFFFFFFFF, 1};
                if (!api->WriteRecord(0x5052464C, 1, record.data(), sizeof(record))) {
                    logger::error("[Save] Could not write companion exclusion");
                }
            }
        });
        serial->SetLoadCallback([](SKSE::SerializationInterface* api) {
            CommandGesture::RestoreSelector(false);
            CommandGesture::HeadTracking::RestoreOwnership(0);
            auto& c = GetSingleton();
            std::lock_guard lock(c.stateMutex);
            c.explicitExclusions.clear();
            c.dialogueRegistrations.clear();
            c.packageRegistrations.clear();
            c.packageLossSince.clear();
            c.packageSeenFollowing.clear();
            c.restoredRelax = false;
            c.restoredRelaxSlots.clear();
            c.restoredVanilla = false;
            std::uint32_t type{}, version{}, length{};
            while (api->GetNextRecordInfo(type, version, length)) {
                if (type == 0x5043544C && version == 1 && length == 8) {
                    std::array<std::uint32_t, 2> record{};
                    RE::FormID resolved{};
                    if (api->ReadRecordData(record.data(), sizeof(record)) == sizeof(record) &&
                        ValidPackageControlFlags(record[1]) && api->ResolveFormID(record[0], resolved)) {
                        c.packageRegistrations[resolved] = record[1];
                    }
                    continue;
                }
                if (type == 0x444C474D && version == 1 && length == 4) {
                    RE::FormID saved{}, resolved{};
                    if (api->ReadRecordData(&saved, 4) == 4 && api->ResolveFormID(saved, resolved)) {
                        c.dialogueRegistrations.insert(resolved);
                    }
                    continue;
                }
                if (type == 0x56414E4C && version == 1 && length == 4) {
                    std::uint32_t saved{};
                    if (api->ReadRecordData(&saved, 4) == 4) {
                        c.restoredVanilla = saved == 1;
                    }
                    continue;
                }
                if (type == 0x57484544 && version == 1 && length == 4) {
                    std::uint32_t saved{};
                    if (api->ReadRecordData(&saved, 4) == 4) {
                        CommandGesture::HeadTracking::RestoreOwnership(saved);
                    }
                    continue;
                }
                if (type == 0x57475354 && (version >= 1 && version <= 3) && length == 4) {
                    std::uint32_t saved{};
                    if (api->ReadRecordData(&saved, 4) == 4) {
                        CommandGesture::RestoreSelector(version == 1 ? (saved == 1 ? 1U : 0U) : saved);
                    }
                    continue;
                }
                if (type == 0x52454C58 && version == 1 && length >= 4 && length <= PARTY_CAPACITY * 8 + 4 &&
                    (length - 4) % 8 == 0) {
                    std::vector<std::uint32_t> record(length / 4);
                    if (api->ReadRecordData(record.data(), length) != length ||
                        record[0] != (length - 4) / 8) {
                        continue;
                    }
                    c.restoredRelax = true;
                    std::set<std::uint32_t> slots;
                    for (std::size_t i = 1; i < record.size(); i += 2) {
                        RE::FormID id{};
                        if (record[i + 1] < PARTY_CAPACITY && slots.insert(record[i + 1]).second &&
                            api->ResolveFormID(record[i], id)) {
                            c.restoredRelaxSlots[id] = static_cast<int>(record[i + 1]);
                        }
                    }
                    continue;
                }
                if (type != 0x5052464C || version != 1 || length != 24) {
                    continue;
                }
                std::array<std::uint32_t, 6> record{};
                RE::FormID id{};
                if (api->ReadRecordData(record.data(), sizeof(record)) != sizeof(record) ||
                    !api->ResolveFormID(record[0], id)) {
                    continue;
                }
                if (record[5] == 1) {
                    c.explicitExclusions.insert(id);
                }
            }
            logger::info("[Save] Restored {} companion exclusions; legacy manual placements ignored",
                         c.explicitExclusions.size());
        });
    }

    void FormationController::Notify(std::string_view a_message)
    {
        Engine::Notify(std::string(a_message).c_str());
    }
} // namespace Wayfarer

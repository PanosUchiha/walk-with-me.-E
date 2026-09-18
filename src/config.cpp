#include "config.h"
#include "custom_followers.h"
#include "follower_settings.h"
#include "hotkey_settings.h"

namespace
{
    std::string Lower(std::string a_value)
    {
        std::transform(a_value.begin(), a_value.end(), a_value.begin(), [](unsigned char a_character) {
            return static_cast<char>(std::tolower(a_character));
        });
        return a_value;
    }

    std::string Trim(std::string a_value)
    {
        const auto first = a_value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return {};
        }
        const auto last = a_value.find_last_not_of(" \t\r\n");
        return a_value.substr(first, last - first + 1);
    }

    Wayfarer::FormationMode ReadMode(long a_value)
    {
        switch (a_value) {
        case 1:
            return Wayfarer::FormationMode::kLead;
        case 3:
            return Wayfarer::FormationMode::kRear;
        case 2:
            return Wayfarer::FormationMode::kCompanion;
        case 4:
            return Wayfarer::FormationMode::kSandbox;
        case 5:
            return Wayfarer::FormationMode::kVanilla;
        default:
            return Wayfarer::FormationMode::kDynamic;
        }
    }
} // namespace

namespace Wayfarer
{
    Settings& Settings::GetSingleton()
    {
        static Settings singleton;
        return singleton;
    }

    void Settings::Load()
    {
        data = SettingsData{};

        wchar_t modulePath[MAX_PATH]{};
        if (const auto module = GetModuleHandleW(L"Wayfarer.dll");
            module && GetModuleFileNameW(module, modulePath, MAX_PATH) > 0) {
            path = std::filesystem::path(modulePath).parent_path() / "Wayfarer.ini";
        }
        else {
            path = std::filesystem::path("Data/SKSE/Plugins/Wayfarer.ini");
        }

        CSimpleIniA ini;
        ini.SetUnicode();
        if (ini.LoadFile(path.string().c_str()) < 0) {
            logger::warn("[Settings] {} was not found; using built-in defaults.", path.string());
            return;
        }

        if (MigrateFollowerSettings(ini)) {
            if (ini.SaveFile(path.string().c_str()) < 0) {
                logger::warn("[Settings] Could not save automatic enrollment upgrade");
            }
            logger::info("[Settings] Manual follower enrollment is now the default; automatic enrollment can "
                         "be enabled in Party settings");
        }

        data.enabled = ini.GetBoolValue("General", "bEnabled", data.enabled);
        data.conversationAwareness =
            ini.GetBoolValue("PartyLife", "bConversationAwareness", data.conversationAwareness);
        data.walkingBanter = ini.GetBoolValue("PartyLife", "bWalkingBanter", data.walkingBanter);
        data.victoryCelebrations =
            ini.GetBoolValue("PartyLife", "bVictoryCelebrations", data.victoryCelebrations);
        data.personalities = ini.GetBoolValue("PartyLife", "bPersonalities", data.personalities);
        data.lookouts = ini.GetBoolValue("PartyLife", "bLookouts", data.lookouts);
        data.conversationClearance =
            std::clamp(static_cast<float>(ini.GetDoubleValue("PartyLife", "fConversationClearance", 150)),
                       80.0F, 300.0F);
        data.lookoutAfter = std::clamp(
            static_cast<float>(ini.GetDoubleValue("PartyLife", "fLookoutAfter", 60)), 30.0F, 180.0F);
        CSimpleIniA::TNamesDepend personalityKeys;
        ini.GetAllKeys("Personalities", personalityKeys);
        for (const auto& key : personalityKeys) {
            const long choice = ini.GetLongValue("Personalities", key.pItem, -1);
            if (choice >= 0 && choice <= 4) {
                data.personalityOverrides[key.pItem] = static_cast<int>(choice);
            }
        }
        data.autoDiscover = ini.GetBoolValue("General", "bAutoDiscover", data.autoDiscover);
        data.requirePlayerTeammate =
            ini.GetBoolValue("General", "bRequirePlayerTeammate", data.requirePlayerTeammate);
        data.maxFollowers =
            std::clamp(static_cast<int>(ini.GetLongValue("General", "iMaxFollowers", data.maxFollowers)), 1,
                       PARTY_CAPACITY);
        data.mode = ReadMode(ini.GetLongValue("General", "iFormationMode", static_cast<long>(data.mode)));
        data.preferredSide = ini.GetLongValue("General", "iPreferredSide", data.preferredSide) < 0 ? -1 : 1;

        data.releaseInCombat = ini.GetBoolValue("Safety", "bReleaseInCombat", data.releaseInCombat);
        data.releaseWhenSneaking =
            ini.GetBoolValue("Safety", "bReleaseWhenSneaking", data.releaseWhenSneaking);
        data.releaseWhenWeaponDrawn =
            ini.GetBoolValue("Safety", "bReleaseWhenWeaponDrawn", data.releaseWhenWeaponDrawn);
        data.releaseWhenControlsDisabled =
            ini.GetBoolValue("Safety", "bReleaseWhenControlsDisabled", data.releaseWhenControlsDisabled);
        data.requireTravelPackage =
            ini.GetBoolValue("Safety", "bRequireTravelPackage", data.requireTravelPackage);
        data.disableIndoors = ini.GetBoolValue("Safety", "bDisableIndoors", data.disableIndoors);
        data.logDiagnostics = ini.GetBoolValue("Debug", "bLogDiagnostics", data.logDiagnostics);
        data.enforceNFF = ini.GetBoolValue("Compatibility", "bEnforceNFF", data.enforceNFF);
        data.enforceCustomFollowers =
            ini.GetBoolValue("Compatibility", "bEnforceCustomFollowers", data.enforceCustomFollowers);
        data.showOrderHUD = ini.GetBoolValue("Interface", "bShowOrderHUD", data.showOrderHUD);
        data.hudPanel = ini.GetBoolValue("Interface", "bHUDPanel", data.hudPanel);
        data.commandGestures = ini.GetBoolValue("Interface", "bCommandGestures", data.commandGestures);
        data.pointingSignal = ini.GetBoolValue("Interface", "bPointingSignal", data.pointingSignal);
        data.companionSignal = ini.GetBoolValue("Interface", "bCompanionSignal", data.companionSignal);
        data.gestureAwareness = ini.GetBoolValue("Interface", "bGestureAwareness", data.gestureAwareness);
        data.gamepadBinding = std::clamp(
            static_cast<int>(ini.GetLongValue("Hotkeys", "iGamepadWheelBinding", data.gamepadBinding)), 0, 3);
        data.traceMovement = ini.GetBoolValue("Debug", "bTraceMovement", data.traceMovement);
        auto number = [&](const char* section, const char* key, float fallback, float low, float high) {
            const double value = ini.GetDoubleValue(section, key, fallback);
            return std::isfinite(value) ? static_cast<float>(std::clamp(value, static_cast<double>(low),
                                                                        static_cast<double>(high)))
                                        : fallback;
        };
        data.hudScale = number("Interface", "fHUDScale", data.hudScale, 0.55F, 1.3F);
        auto& hands = data.handHolding;
        hands.enabled = ini.GetBoolValue("HandHolding", "bEnabled", hands.enabled);
        hands.partner = Lower(Trim(ini.GetValue("HandHolding", "sPartner", "")));
        hands.partnerName = ini.GetValue("HandHolding", "sPartnerName", "");
        hands.delay = number("HandHolding", "fLeadInSeconds", hands.delay, .5F, 10.0F);
        hands.connectDistance =
            number("HandHolding", "fConnectDistance", hands.connectDistance, 60.0F, 110.0F);
        hands.releaseDistance = number("HandHolding", "fReleaseDistance", hands.releaseDistance,
                                       hands.connectDistance + 10.0F, 160.0F);
        // Palm contact point and how close the palms must already be before the
        // constraint is created. Both depend on the installed body/hand meshes,
        // so they are adjustable without a rebuild; the log prints the measured
        // wrist gap against the limit each second while acquiring.
        hands.palmX = number("HandHolding", "fPalmX", hands.palmX, -15.0F, 15.0F);
        hands.palmY = number("HandHolding", "fPalmY", hands.palmY, -15.0F, 15.0F);
        hands.palmZ = number("HandHolding", "fPalmZ", hands.palmZ, -15.0F, 15.0F);
        hands.gripGap = number("HandHolding", "fGripWristGap", hands.gripGap, 8.0F, 45.0F);
        data.hudVertical = number("Interface", "fHUDVertical", data.hudVertical, 0.1F, 0.85F);
        data.releaseDistance = number("Safety", "fReleaseDistance", data.releaseDistance, 500.0F, 10000.0F);
        data.teleportCatchup = ini.GetBoolValue("Movement", "bTeleportCatchup", data.teleportCatchup);
        data.forwardCollision = ini.GetBoolValue("Movement", "bForwardCollision", data.forwardCollision);
        data.teleportDistance =
            number("Movement", "fTeleportDistance", data.teleportDistance, 1500.0F, 15000.0F);
        data.updateInterval = number("Movement", "fUpdateInterval", data.updateInterval, 0.10F, 0.50F);
        data.scanInterval = number("Movement", "fScanInterval", data.scanInterval, 0.25F, 5.0F);
        auto& t = data.travel;
        t.startSpeed = number("Movement", "fStartSpeed", t.startSpeed, 5.0F, 100.0F);
        t.stopSpeed = number("Movement", "fStopSpeed", t.stopSpeed, 1.0F, t.startSpeed - 1.0F);
        t.startDelay = number("Movement", "fStartDelay", t.startDelay, 0.1F, 2.0F);
        t.stopDelay = number("Movement", "fStopDelay", t.stopDelay, 0.1F, 2.0F);
        t.idleRelease = number("Movement", "fIdleReleaseSeconds", t.idleRelease, t.stopDelay + 0.5F, 60.0F);
        t.directionSharpness = number("Movement", "fDirectionSharpness", t.directionSharpness, 1.0F, 12.0F);
        t.turnDelay = number("Movement", "fTurnResponseDelay", t.turnDelay, 0.0F, .6F);
        t.individuality = number("Movement", "fCompanionIndividuality", t.individuality, 0.0F, 1.0F);
        t.lookAhead = number("Movement", "fLookAheadSeconds", t.lookAhead, 0.2F, 1.5F);
        t.maxPrediction = number("Movement", "fMaxPredictionDistance", t.maxPrediction, 80.0F, 450.0F);
        t.spacing = number("Movement", "fSpacingScale", t.spacing, 0.6F, 3.0F);
        t.naturalStragglerDistance =
            number("Movement", "fNaturalStragglerDistance", t.naturalStragglerDistance, 0.0F, 800.0F);
        t.goalThreshold = number("Movement", "fGoalChangeDistance", t.goalThreshold, 40.0F, 200.0F);
        t.refreshSeconds = number("Movement", "fTravelRefreshSeconds", t.refreshSeconds, 0.4F, 2.0F);
        t.arrivalRadius = number("Movement", "fArrivalRadius", t.arrivalRadius, 40.0F, 100.0F);
        t.stuckSeconds = number("Movement", "fStuckSeconds", t.stuckSeconds, 2.0F, 15.0F);
        t.recoverySeconds = number("Movement", "fRecoverySeconds", t.recoverySeconds, 1.0F, 10.0F);
        t.catchUpSeconds = number("Movement", "fCatchUpSeconds", t.catchUpSeconds, 1.0F, 8.0F);
        t.catchUpBonus = number("Movement", "fCatchUpSpeedBonus", t.catchUpBonus, 0.0F, 250.0F);
        t.maxSpeedScale = number("Movement", "fMaxTravelSpeedScale", t.maxSpeedScale, 1.0F, 1.75F);

        data.distantCatchupStart = number("Movement", "fDistantCatchupStart", data.distantCatchupStart,
                                          300.0F, data.releaseDistance - 100.0F);
        data.distantCatchupEnd = number("Movement", "fDistantCatchupEnd", data.distantCatchupEnd, 150.0F,
                                        data.distantCatchupStart - 100.0F);
        auto& sandbox = data.sandbox;
        sandbox.automatic = ini.GetBoolValue("Sandbox", "bAutomatic", sandbox.automatic);
        sandbox.startRadius = number("Sandbox", "fStartRadius", sandbox.startRadius, 40.0F,
                                     std::min(600.0F, data.releaseDistance - 200.0F));
        sandbox.maxRadius = number("Sandbox", "fMaxRadius", sandbox.maxRadius, sandbox.startRadius,
                                   std::min(1800.0F, data.releaseDistance - 200.0F));
        sandbox.settledActivityRadius =
            number("Sandbox", "fSettledActivityRadius", sandbox.settledActivityRadius, 180.0F,
                   std::min(1800.0F, data.releaseDistance - 200.0F));
        sandbox.activityHeight = number("Sandbox", "fActivityHeight", sandbox.activityHeight, 64.0F, 1600.0F);
        sandbox.fullSandboxAfter =
            number("Sandbox", "fFullSandboxAfter", sandbox.fullSandboxAfter, 0.0F, 180.0F);
        sandbox.idleRecoveryAfter =
            number("Sandbox", "fIdleRecoveryAfter", sandbox.idleRecoveryAfter, 0.0F, 120.0F);
        sandbox.extraPoses = ini.GetBoolValue("Sandbox", "bExtraRestPoses", sandbox.extraPoses);
        sandbox.resumeDistance =
            number("Sandbox", "fResumeDistance", sandbox.resumeDistance, 100.0F, 1000.0F);
        sandbox.groundSitting = ini.GetBoolValue("Sandbox", "bGroundSitting", sandbox.groundSitting);
        sandbox.meals = ini.GetBoolValue("Sandbox", "bMeals", sandbox.meals);
        sandbox.reading = ini.GetBoolValue("Sandbox", "bReading", sandbox.reading);
        sandbox.stretching = ini.GetBoolValue("Sandbox", "bStretching", sandbox.stretching);
        sandbox.social = ini.GetBoolValue("Sandbox", "bSocialIdles", sandbox.social);

        LoadHotkeys(ini, data);

        if (const char* excluded = ini.GetValue("Compatibility", "sExcludedPlugins", nullptr); excluded) {
            data.excludedPlugins.clear();
            std::stringstream stream(excluded);
            std::string plugin;
            while (std::getline(stream, plugin, ',')) {
                plugin = Lower(Trim(std::move(plugin)));
                if (!plugin.empty()) {
                    data.excludedPlugins.insert(std::move(plugin));
                }
            }
        }

        if (!ini.GetValue("Compatibility", "bEnforceCustomFollowers", nullptr) &&
            IsLegacyCustomBlocklist(data.excludedPlugins)) {
            data.excludedPlugins.clear();
            logger::info("[Settings] Retired the bundled custom follower blocklist; custom enforcement is "
                         "available on the Party page");
        }

        logger::info("[Settings] enabled={} auto={} teammate={} mode={} max={} update={} scan={} "
                     "exclusions={} diag={} customEnforcement={}",
                     data.enabled, data.autoDiscover, data.requirePlayerTeammate, static_cast<int>(data.mode),
                     data.maxFollowers, data.updateInterval, data.scanInterval, data.excludedPlugins.size(),
                     data.logDiagnostics, data.enforceCustomFollowers);
    }

    bool Settings::Save(const SettingsData& value)
    {
        CSimpleIniA ini;
        ini.SetUnicode();
        ini.LoadFile(path.string().c_str());
        ini.SetBoolValue("General", "bEnabled", value.enabled);
        ini.SetBoolValue("General", "bAutoDiscover", value.autoDiscover);
        ini.SetLongValue("General", "iEnrollmentVersion", 2);
        ini.SetBoolValue("Compatibility", "bEnforceNFF", value.enforceNFF);
        ini.SetBoolValue("PartyLife", "bWalkingBanter", value.walkingBanter);
        ini.SetBoolValue("PartyLife", "bVictoryCelebrations", value.victoryCelebrations);
        ini.SetBoolValue("Compatibility", "bEnforceCustomFollowers", value.enforceCustomFollowers);
        std::vector<std::string> excluded(value.excludedPlugins.begin(), value.excludedPlugins.end());
        std::sort(excluded.begin(), excluded.end());
        std::string excludedList;
        for (const auto& plugin : excluded) {
            if (!excludedList.empty()) {
                excludedList += ", ";
            }
            excludedList += plugin;
        }
        ini.SetValue("Compatibility", "sExcludedPlugins", excludedList.c_str());
        ini.SetBoolValue("Safety", "bDisableIndoors", value.disableIndoors);
        ini.SetBoolValue("Interface", "bShowOrderHUD", value.showOrderHUD);
        ini.SetBoolValue("Interface", "bHUDPanel", value.hudPanel);
        ini.SetBoolValue("Interface", "bCommandGestures", value.commandGestures);
        ini.SetBoolValue("Interface", "bPointingSignal", value.pointingSignal);
        ini.SetBoolValue("Interface", "bCompanionSignal", value.companionSignal);
        ini.SetBoolValue("Interface", "bGestureAwareness", value.gestureAwareness);
        ini.SetBoolValue("HandHolding", "bEnabled", value.handHolding.enabled);
        ini.SetValue("HandHolding", "sPartner", value.handHolding.partner.c_str());
        ini.SetValue("HandHolding", "sPartnerName", value.handHolding.partnerName.c_str());
        ini.SetDoubleValue("HandHolding", "fLeadInSeconds", value.handHolding.delay);
        ini.SetDoubleValue("HandHolding", "fConnectDistance", value.handHolding.connectDistance);
        ini.SetDoubleValue("HandHolding", "fReleaseDistance", value.handHolding.releaseDistance);
        ini.SetDoubleValue("HandHolding", "fPalmX", value.handHolding.palmX);
        ini.SetDoubleValue("HandHolding", "fPalmY", value.handHolding.palmY);
        ini.SetDoubleValue("HandHolding", "fPalmZ", value.handHolding.palmZ);
        ini.SetDoubleValue("HandHolding", "fGripWristGap", value.handHolding.gripGap);
        ini.SetLongValue("Hotkeys", "iGamepadWheelBinding", value.gamepadBinding);
        ini.SetBoolValue("Debug", "bLogDiagnostics", value.logDiagnostics);
        ini.SetBoolValue("Debug", "bTraceMovement", value.traceMovement);
        ini.SetDoubleValue("Movement", "fSpacingScale", value.travel.spacing);
        ini.SetDoubleValue("Movement", "fNaturalStragglerDistance", value.travel.naturalStragglerDistance);
        ini.SetDoubleValue("Movement", "fCatchUpSeconds", value.travel.catchUpSeconds);
        ini.SetDoubleValue("Movement", "fMaxTravelSpeedScale", value.travel.maxSpeedScale);
        ini.SetDoubleValue("Movement", "fIdleReleaseSeconds", value.travel.idleRelease);
        ini.SetDoubleValue("Movement", "fLookAheadSeconds", value.travel.lookAhead);
        ini.SetDoubleValue("Movement", "fTurnResponseDelay", value.travel.turnDelay);
        ini.SetDoubleValue("Movement", "fCompanionIndividuality", value.travel.individuality);
        ini.SetDoubleValue("Interface", "fHUDScale", value.hudScale);
        ini.SetDoubleValue("Interface", "fHUDVertical", value.hudVertical);
        ini.SetLongValue("General", "iMaxFollowers", static_cast<std::int32_t>(value.maxFollowers));
        ini.SetLongValue("General", "iPreferredSide", static_cast<std::int32_t>(value.preferredSide));
        ini.SetLongValue("General", "iFormationMode", static_cast<std::int32_t>(value.mode));
        SaveHotkeys(ini, value);
        ini.SetDoubleValue("Movement", "fDistantCatchupStart", value.distantCatchupStart);
        ini.SetDoubleValue("Movement", "fDistantCatchupEnd", value.distantCatchupEnd);
        ini.SetBoolValue("Movement", "bTeleportCatchup", value.teleportCatchup);
        ini.SetBoolValue("Movement", "bForwardCollision", value.forwardCollision);
        ini.SetDoubleValue("Movement", "fTeleportDistance", value.teleportDistance);
        ini.SetBoolValue("Sandbox", "bAutomatic", value.sandbox.automatic);
        ini.SetDoubleValue("Sandbox", "fStartRadius", value.sandbox.startRadius);
        ini.SetDoubleValue("Sandbox", "fMaxRadius", value.sandbox.maxRadius);
        ini.SetDoubleValue("Sandbox", "fSettledActivityRadius", value.sandbox.settledActivityRadius);
        ini.SetDoubleValue("Sandbox", "fActivityHeight", value.sandbox.activityHeight);
        ini.SetDoubleValue("Sandbox", "fFullSandboxAfter", value.sandbox.fullSandboxAfter);
        ini.SetDoubleValue("Sandbox", "fIdleRecoveryAfter", value.sandbox.idleRecoveryAfter);
        ini.SetBoolValue("PartyLife", "bConversationAwareness", value.conversationAwareness);
        ini.SetBoolValue("PartyLife", "bPersonalities", value.personalities);
        ini.SetBoolValue("PartyLife", "bLookouts", value.lookouts);
        ini.SetDoubleValue("PartyLife", "fConversationClearance", value.conversationClearance);
        ini.SetDoubleValue("PartyLife", "fLookoutAfter", value.lookoutAfter);
        ini.Delete("Personalities", nullptr);
        for (const auto& [key, choice] : value.personalityOverrides) {
            if (choice >= 0 && choice <= 4) {
                ini.SetLongValue("Personalities", key.c_str(), choice);
            }
        }
        ini.SetBoolValue("Sandbox", "bExtraRestPoses", value.sandbox.extraPoses);
        ini.Delete("Sandbox", "fGrowInterval");
        ini.Delete("Sandbox", "fGrowStep");
        ini.SetDoubleValue("Sandbox", "fResumeDistance", value.sandbox.resumeDistance);
        ini.SetBoolValue("Sandbox", "bGroundSitting", value.sandbox.groundSitting);
        ini.SetBoolValue("Sandbox", "bMeals", value.sandbox.meals);
        ini.SetBoolValue("Sandbox", "bReading", value.sandbox.reading);
        ini.SetBoolValue("Sandbox", "bStretching", value.sandbox.stretching);
        ini.SetBoolValue("Sandbox", "bSocialIdles", value.sandbox.social);
        auto temporary = path;
        temporary += ".tmp";
        if (ini.SaveFile(temporary.string().c_str()) < 0 ||
            !MoveFileExW(temporary.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            logger::error("[Settings] Could not save {}", path.string());
            return false;
        }
        Load();
        return true;
    }

    const SettingsData& Settings::Get() const noexcept
    {
        return data;
    }

    const std::filesystem::path& Settings::GetPath() const noexcept
    {
        return path;
    }

    bool Settings::IsPluginExcluded(std::string_view a_pluginName) const
    {
        return data.excludedPlugins.contains(Lower(std::string(a_pluginName)));
    }
} // namespace Wayfarer

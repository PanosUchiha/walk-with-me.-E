#include "Plugin.h"
#include "WayfarerAPI.h"
#include "command_gesture.h"
#include "formation_controller.h"
#include "hand_holding.h"
#include "input_handler.h"
#include "logging.h"
#include "loot_scout.h"
#include "menu_ui.h"
#include "papyrus_bridge.h"
#include "runtime_compatibility.h"
#include "rest_dialogue.h"
#include "tether_clasp.h"
#include "travel_speed.h"

namespace
{
    class WayfarerInterface final : public WayfarerAPI::IWayfarer
    {
      public:
        std::uint32_t GetVersion() const noexcept override { return WayfarerAPI::INTERFACE_VERSION; }
        bool RegisterFollower(std::uint32_t a_actorFormID, std::int32_t a_preferredSlot) override
        {
            return Wayfarer::FormationController::GetSingleton().RegisterFollower(a_actorFormID,
                                                                                  a_preferredSlot);
        }
        bool UnregisterFollower(std::uint32_t a_actorFormID) override
        {
            return Wayfarer::FormationController::GetSingleton().UnregisterFollower(a_actorFormID);
        }
        bool ExcludeFollower(std::uint32_t a_actorFormID) override
        {
            return Wayfarer::FormationController::GetSingleton().ExcludeFollower(a_actorFormID);
        }
        bool IncludeFollower(std::uint32_t a_actorFormID) override
        {
            return Wayfarer::FormationController::GetSingleton().IncludeFollower(a_actorFormID);
        }
        bool IsManaged(std::uint32_t a_actorFormID) const override
        {
            return Wayfarer::FormationController::GetSingleton().IsManaged(a_actorFormID);
        }
        void SetEnabled(bool a_enabled) override
        {
            Wayfarer::FormationController::GetSingleton().SetEnabled(a_enabled, false);
        }
        bool IsEnabled() const override { return Wayfarer::FormationController::GetSingleton().IsEnabled(); }
        void SetFormationMode(WayfarerAPI::FormationMode a_mode) override
        {
            Wayfarer::FormationController::GetSingleton().SetMode(
                static_cast<Wayfarer::FormationMode>(a_mode), false);
        }
        WayfarerAPI::FormationMode GetFormationMode() const override
        {
            return static_cast<WayfarerAPI::FormationMode>(
                Wayfarer::FormationController::GetSingleton().GetMode());
        }
        std::uint32_t GetManagedCount() const override
        {
            return Wayfarer::FormationController::GetSingleton().GetManagedCount();
        }
    };

    WayfarerInterface g_interface;

    struct PlayerUpdateHook
    {
        static void Thunk(RE::PlayerCharacter* a_player, float a_delta)
        {
            Func(a_player, a_delta);
            Wayfarer::CommandGesture::Update(a_delta);
            Wayfarer::FormationController::GetSingleton().Update(a_delta);
            Wayfarer::FormationController::GetSingleton().UpdateHandHolding(a_delta);
            Wayfarer::FormationController::GetSingleton().UpdateAliasSync(a_delta);
            Wayfarer::Menu::Publish();
            Wayfarer::RestDialogue::Update(a_delta);
            Wayfarer::Loot::Update(a_delta);
        }

        static void Install()
        {
            REL::Relocation<std::uintptr_t> vtable{RE::PlayerCharacter::VTABLE[0]};
            Func = vtable.write_vfunc(0xAD, Thunk);
            logger::info("[Hook] Player update hook installed.");
        }

        static inline REL::Relocation<decltype(Thunk)> Func;
    };

    // NFF's menu lives in the journal, and shows its catch-up timer there: hand it back the
    // player's own value while the journal is open.
    struct JournalWatch final : RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
                                              RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (event && event->opening && event->menuName == RE::JournalMenu::MENU_NAME) {
                Wayfarer::FormationController::GetSingleton().ReleaseFrameworkCatchup("while the journal is open");
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    } journalWatch;

    void OnMessage(SKSE::MessagingInterface::Message* a_message)
    {
        switch (a_message->type) {
        case SKSE::MessagingInterface::kPostLoad:
            Wayfarer::HandHolding::Clasp::Register();
            break;
        case SKSE::MessagingInterface::kInputLoaded:
            Wayfarer::InputHandler::Register();
            break;
        case SKSE::MessagingInterface::kDataLoaded:
            Wayfarer::FormationController::GetSingleton().Initialize();
            Wayfarer::TravelSpeed::Install();
            Wayfarer::Menu::Register();
            Wayfarer::InputHandler::Register();
            PlayerUpdateHook::Install();
            Wayfarer::HandHolding::Install();
            Wayfarer::Loot::Install();
            if (auto* ui = RE::UI::GetSingleton()) {
                ui->AddEventSink<RE::MenuOpenCloseEvent>(&journalWatch);
            }
            break;
        case SKSE::MessagingInterface::kSaveGame:
            // Sent before the game writes the save, so the save keeps the player's NFF settings
            // (and they survive uninstalling Walk With Me).
            Wayfarer::FormationController::GetSingleton().ReleaseFrameworkCatchup("for the save");
            break;
        case SKSE::MessagingInterface::kPreLoadGame:
            Wayfarer::FormationController::GetSingleton().ResetForGameLoad();
            Wayfarer::HandHolding::OnGameLoaded();
            Wayfarer::Menu::Reset();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            Wayfarer::HandHolding::OnGameLoaded();
            Wayfarer::FormationController::GetSingleton().ResetForGameLoad();
            Wayfarer::FormationController::GetSingleton().RestartFrameworkCatchupAfterLoad();
            Wayfarer::Menu::Reset();
            break;
        default:
            break;
        }
    }
} // namespace

extern "C" WAYFARER_API WayfarerAPI::IWayfarer* Wayfarer_GetInterface(std::uint32_t a_version)
{
    return a_version == WayfarerAPI::INTERFACE_VERSION ? &g_interface : nullptr;
}

extern "C" [[maybe_unused]]
__declspec(dllexport) bool __cdecl SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    if (!a_skse || a_skse->IsEditor()) {
        return false;
    }
    const auto runtime = a_skse->RuntimeVersion();
#ifdef WAYFARER_MODERN_COMMONLIB
    constexpr bool modernLibrary = true;
#else
    constexpr bool modernLibrary = false;
#endif
    if (!Wayfarer::RuntimeCompatibility::Supports({runtime[0], runtime[1], runtime[2], runtime[3]},
                                                  modernLibrary)) {
        const auto message = "Walk With Me: this DLL does not support Skyrim " + runtime.string() +
                             (runtime[3] == Wayfarer::RuntimeCompatibility::GOG
                                  ? ". On GOG, update the game to 1.6.1179."
                                  : ". Install the Walk With Me build matching your game runtime.");
        MessageBoxA(nullptr, message.c_str(), "Walk With Me", MB_OK | MB_ICONERROR);
        return false;
    }
    SKSE::Init(a_skse);
    SetupLog(Plugin::NAME);
    logger::info("{} {} loading", Plugin::NAME, Plugin::VERSION.string());
    logger::info("[Runtime] Skyrim {}; modern Address Library reader: {}", runtime.string(), modernLibrary);

    Wayfarer::FormationController::RegisterSerialization();
    const auto* papyrus = SKSE::GetPapyrusInterface();
    if (!papyrus || !papyrus->Register(Wayfarer::Papyrus::Register)) {
        logger::error("Papyrus API registration failed.");
        return false;
    }
    const auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnMessage)) {
        logger::error("SKSE messaging registration failed.");
        return false;
    }
    return true;
}

extern "C" [[maybe_unused]] __declspec(dllexport) constinit auto SKSEPlugin_Version = []() noexcept {
    SKSE::PluginVersionData version;
    version.PluginName("Walk With Me");
    version.PluginVersion(Plugin::VERSION);
    version.AuthorName("Codex and Tobih");
    // "No struct use" was never true here: the plugin reads actor runtime data,
    // AIProcess, character controller flags and animation graph state. Declaring
    // it told the script extender to skip the very check that matters, and left
    // the post-629 layout claim unmade. Declare what is actually compiled in.
#ifdef WAYFARER_MODERN_COMMONLIB
    version.UsesAddressLibrary();
    version.UsesUpdatedStructs();
#else
    version.UsesAddressLibrary(true);
    version.UsesStructsPost629(true);
#endif
    return version;
}();

extern "C" [[maybe_unused]]
__declspec(dllexport) bool __cdecl SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* a_info)
{
    a_info->infoVersion = SKSE::PluginInfo::kVersion;
    a_info->name = SKSEPlugin_Version.pluginName;
    a_info->version = SKSEPlugin_Version.pluginVersion;
    return true;
}

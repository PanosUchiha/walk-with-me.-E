#include "menu_ui.h"
#include "SKSEMenuFramework.h"
#include "command_gesture.h"
#include "action_grid.h"
#include "companion_menu.h"
#include "engine_compatibility.h"
#include "formation_controller.h"
#include "hud_notice.h"
#include "order_visuals.h"
#include "order_wheel.h"
#include "overhead_icons.h"
#include "rest_dialogue.h"
#include <functional>
#include <optional>

namespace Wayfarer::Menu
{
    namespace
    {
        namespace Im = ImGuiMCP;
        namespace Draw = ImGuiMCP::ImDrawListManager;
        namespace MF = SKSEMenuFramework;
        using V = Im::ImVec2;
        constexpr auto teal = IM_COL32(201, 189, 156, 255);
        constexpr auto gold = IM_COL32(197, 180, 136, 255);
        constexpr auto ink = IM_COL32(19, 19, 18, 242);
        constexpr auto panel = IM_COL32(36, 35, 32, 220);
        constexpr auto muted = IM_COL32(149, 146, 137, 255);
        constexpr auto white = IM_COL32(222, 219, 207, 255);
        constexpr std::uint32_t unbound = 0xFFFFFFFF;
        std::mutex viewMutex;
        PartyView cached;
        std::atomic<bool> installed{}, refresh{true};
        std::atomic<int> capturing{-1}, captured{-1}, applyResult{};
        std::atomic<bool> restoreHotkey{}, previousHotkey{};
        std::atomic<std::uint64_t> captureStarted{};
        MF::Model::WindowInterface* commandWindow{};
        MF::Model::WindowInterface* packageWindow{};
        constexpr auto companionPauseName = "WalkWithMeCompanionPause";
        bool companionPauseRequested{};
        void QueueWheelBlur();
        void OpenFormationEditor(const PartyView& view); // The formation editor window.
        // An invisible native menu lets Skyrim own the pause count and its cleanup.
        // It does not change Menu Framework's global freeze preference.
        class CompanionPauseMenu final : public RE::IMenu
        {
          public:
            CompanionPauseMenu() { menuFlags.set(RE::UI_MENU_FLAGS::kPausesGame); }
            static RE::IMenu* Create() { return new CompanionPauseMenu(); }
            RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& message) override
            {
                if (message.type == RE::UI_MESSAGE_TYPE::kHide ||
                    message.type == RE::UI_MESSAGE_TYPE::kForceHide) {
                    if (packageWindow) {
                        packageWindow->IsOpen = false;
                    }
                    QueueWheelBlur();
                }
                return RE::IMenu::ProcessMessage(message);
            }
        };
        void SyncCompanionPause()
        {
            const bool desired = packageWindow && packageWindow->IsOpen;
            if (desired == companionPauseRequested) {
                return;
            }
            if (auto* messages = RE::UIMessageQueue::GetSingleton()) {
                messages->AddMessage(companionPauseName,
                                     desired ? RE::UI_MESSAGE_TYPE::kShow : RE::UI_MESSAGE_TYPE::kHide,
                                     nullptr);
                companionPauseRequested = desired;
            }
        }
        std::atomic<RE::FormID> packageSelection{};
        RE::FormID wheelPackageActor{};
        std::uint64_t packageGeneration{};
        bool packageFromWheel{};
        std::atomic<bool> packageShortcut{}, packageAccept{}, packageCancel{}, packagePush{};
        std::atomic<bool> toggleSaving{}; // One quick toggle saves at a time.
        // The grid page is showing: Esc and B step back to the wheel instead of closing.
        std::atomic<bool> gridBack{}, gridBackEnabled{};
        bool wheelBlurOwned{};
        std::atomic<bool> wheelCloseRequested{};
        void SyncWheelBlur()
        {
            SyncCompanionPause();
            const bool open =
                (commandWindow && commandWindow->IsOpen) || (packageWindow && packageWindow->IsOpen);
            auto* blur = RE::UIBlurManager::GetSingleton();
            if (!blur || open == wheelBlurOwned) {
                return;
            }
            if (open) {
                blur->IncrementBlurCount();
            }
            else if (blur->blurCount) {
                blur->DecrementBlurCount();
            }
            wheelBlurOwned = open;
        }
        void QueueWheelBlur()
        {
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([] { SyncWheelBlur(); });
            }
        }
        void SetWheelOpen(bool open)
        {
            if (commandWindow) {
                wheelCloseRequested = !open;
                if (open) {
                    commandWindow->IsOpen = true;
                }
            }

            QueueWheelBlur();
        }

        std::unique_ptr<MF::Model::InputEvent> inputRegistration;
        std::unique_ptr<MF::Model::HudElement> hudRegistration;
        std::unique_ptr<MF::Model::Event> eventRegistration;
        SettingsData draft;
        bool haveDraft{}, dirty{};
        std::mutex settingsMutex;
        std::optional<std::pair<std::uint64_t, SettingsData>> pendingSettings;
        bool settingsTaskQueued{};
        std::atomic<std::uint64_t> requestedRevision{}, appliedRevision{};
        std::string feedback;
        std::uint64_t nextPublish{}, hudPreviewAt{};
        bool hudPreviewEnabled{true};
        bool hudPreviewPanel{true};
        std::atomic<float> padX{}, padY{};
        std::atomic<std::uint64_t> padStickAt{}, orderNotice{};
        std::atomic<bool> padConfirm{}, padCancel{}, padUsed{};
        std::atomic<int> padDx{}, padDy{}; // D-pad presses not yet read by the menu.
        std::uint32_t padHeld{};
        float hudPreviewScale{.8F}, hudPreviewHeight{.32F};
        constexpr std::array<const char*, 7> profileNames{"Natural", "Lead",    "Companion", "Rear",
                                                          "Relax",   "Vanilla", "Custom"};
        constexpr auto profileModes = OrderModes;
        constexpr std::array<const char*, 7> styleHints{
            "Find your own space as we travel",
            "Take the lead along our route",
            "Travel at my side",
            "Keep to the rear",
            "Stay here and make yourselves at home",
            "Return travel and rest to Skyrim or your follower framework",
            "Travel in the formation you drew in Travel settings"};
        // The order names as shown, with the player's own name for their custom formation.
        std::array<const char*, 7> OrderNames(const PartyView& view)
        {
            auto names = profileNames;
            names[6] = view.settings.customFormation.title.c_str();
            return names;
        }
        std::uint32_t KeyboardModifiers()
        {
            auto* input = RE::BSInputDeviceManager::GetSingleton();
            auto* keyboard = input ? input->GetKeyboard() : nullptr;
            return Hotkeys::ReadModifiers(
                [keyboard](std::uint32_t key) { return keyboard && Engine::IsKeyPressed(*keyboard, key); });
        }

        void CancelCapture()
        {
            capturing.store(-1);
            if (restoreHotkey.exchange(false)) {
                MF::SetHotkeyEnabled(previousHotkey.load());
            }
        }
        void StartCapture(int row)
        {
            if (!restoreHotkey.exchange(true)) {
                previousHotkey.store(MF::IsHotkeyEnabled());
            }
            MF::SetHotkeyEnabled(false);
            captured.store(-1);
            captureStarted.store(GetTickCount64());
            capturing.store(row);
        }

        PartyView Read()
        {
            std::lock_guard lock(viewMutex);
            return cached;
        }
        void Cache(PartyView value)
        {
            OrderVisuals::SetCustomEmblem(value.settings.customFormation.emblem);
            std::lock_guard lock(viewMutex);
            cached = std::move(value);
        }
        void Queue(std::function<void(FormationController&)> work)
        {
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([work = std::move(work)] {
                    auto& c = FormationController::GetSingleton();
                    work(c);
                    Cache(c.Snapshot());
                });
            }
        }
        float E(float value)
        {
            return Im::GetFontSize() * value;
        }
        void Space(float value = 0.16F)
        {
            Im::Dummy({0, E(value)});
        }
        void Note(const char* text)
        {
            Im::PushStyleColor(Im::ImGuiCol_Text, muted);
            Im::TextWrapped("%s", text);
            Im::PopStyleColor();
        }
        struct Inscription
        {
            bool pushed{};
            Inscription()
            {
                auto* before = Im::GetFont();
                const char* name = Im::ImFontManger::GetDebugName(before);
                if (name && std::string_view(name).find("WalkWithMeInscription") != std::string_view::npos) {
                    return;
                }
                MF::PushFont("WalkWithMeInscription");
                pushed = Im::GetFont() != before;
            }
            ~Inscription()
            {
                if (pushed) {
                    Im::PopFont();
                }
            }
        };

        void Help(const char* text)
        {
            if (Im::IsItemHovered(Im::ImGuiHoveredFlags_AllowWhenDisabled)) {
                Im::BeginTooltip();
                Im::PushTextWrapPos(E(30));
                Im::TextUnformatted(text);
                Im::PopTextWrapPos();
                Im::EndTooltip();
            }
        }

        constexpr int pathfinderIcon = 50, crestIcon = 100; // Beyond the Font Awesome headers.
        void Icon(Im::ImDrawList* draw, V c, float r, int type, Im::ImU32 color)
        {
            if (type >= crestIcon) {
                OrderVisuals::Crest(draw, c, r * 2.5F, type - crestIcon, 1, color);
                return;
            }
            if (type == pathfinderIcon) {
                OrderVisuals::Icon(draw, c, r * 2.7F, 9, 3.0F, 1, true, color);
                return;
            }
            static const std::array<std::string, 9> glyphs = [] {
                std::array<std::string, 9> result;
                constexpr unsigned codes[]{0xf14e, 0xf024, 0xf132, 0xf06d, 0xf018,
                                           0xf0c0, 0xf084, 0xf007, 0xf219};
                for (int i = 0; i < 9; ++i) {
                    result[i] = FontAwesome::UnicodeToUtf8(codes[i]);
                }
                return result;
            }();
            FontAwesome::PushSolid();
            const auto& glyph = glyphs[std::clamp(type, 0, 8)];
            const float size = r * 2.0F;
            const auto bounds = Im::CalcTextSize(glyph.c_str());
            const float ratio = size / Im::GetFontSize();
            Draw::AddText(draw, Im::GetFont(), size,
                          {c.x - bounds.x * ratio * .5F, c.y - bounds.y * ratio * .5F}, color, glyph.c_str());
            FontAwesome::Pop();
        }
        // EMBLEM_PICKER_BEGIN: shared with the offline visual proof.
        // Sixteen emblems from Game-icons.net, none used elsewhere in the mod, as a wrapping row of
        // tiles; the chosen one is framed in yellow. Returns true when the choice changed.
        bool EmblemPicker(V start, float width, float cell, int& emblem)
        {
            const float spacing = cell * .12F;
            const int across = std::max(1, static_cast<int>((width + spacing) / (cell + spacing)));
            auto* draw = Im::GetWindowDrawList();
            bool changed = false;
            for (int i = 0; i < Custom::emblemCount; ++i) {
                const V at{start.x + (i % across) * (cell + spacing), start.y + (i / across) * (cell + spacing)};
                Im::SetCursorScreenPos(at);
                Im::PushID(i);
                if (Im::InvisibleButton("emblem", {cell, cell}) && emblem != i) {
                    emblem = i;
                    changed = true;
                }
                const bool over = Im::IsItemHovered(), chosen = emblem == i;
                if (over) {
                    Im::SetTooltip("%s", Custom::emblems[i].label);
                }
                Im::PopID();
                const float r = cell * .16F;
                Draw::AddRectFilled(draw, at, {at.x + cell, at.y + cell},
                                    chosen ? IM_COL32(58, 50, 24, 255)
                                    : over ? IM_COL32(42, 50, 50, 255)
                                           : IM_COL32(27, 35, 38, 235),
                                    r, 0);
                Draw::AddRect(draw, at, {at.x + cell, at.y + cell},
                              chosen ? IM_COL32(240, 199, 74, 255) : IM_COL32(168, 153, 118, over ? 160 : 60), r, 0,
                              chosen ? 2.0F : 1.0F);
                OrderVisuals::Crest(draw, {at.x + cell * .5F, at.y + cell * .5F}, cell * .72F, i, 1,
                                    chosen ? IM_COL32(248, 214, 110, 255) : IM_COL32(226, 218, 201, 255));
            }
            const int rowsUsed = (Custom::emblemCount + across - 1) / across;
            Im::SetCursorScreenPos({start.x, start.y + rowsUsed * (cell + spacing)});
            Im::Dummy({1, 0});
            return changed;
        }
        // EMBLEM_PICKER_END
        void Label(const char* text, int icon = 0)
        {
            Space(0.35F);
            const auto p = Im::GetCursorScreenPos();
            Icon(Im::GetWindowDrawList(), {p.x + E(.4F), p.y + E(.5F)}, E(.35F), icon, gold);
            Im::Dummy({E(1.05F), E(1)});
            Im::SameLine(0, 0);
            Im::PushStyleColor(Im::ImGuiCol_Text, gold);
            Im::TextUnformatted(text);
            Im::PopStyleColor();
            Im::Separator();
            Space(.12F);
        }
        struct Theme
        {
            Theme()
            {
                Im::PushStyleColor(Im::ImGuiCol_Text, white);
                Im::PushStyleColor(Im::ImGuiCol_TextDisabled, muted);
                Im::PushStyleColor(Im::ImGuiCol_WindowBg, ink);
                Im::PushStyleColor(Im::ImGuiCol_ChildBg, ink);
                Im::PushStyleColor(Im::ImGuiCol_FrameBg, panel);
                Im::PushStyleColor(Im::ImGuiCol_Button, panel);
                Im::PushStyleColor(Im::ImGuiCol_ButtonHovered, IM_COL32(67, 63, 53, 230));
                Im::PushStyleColor(Im::ImGuiCol_ButtonActive, IM_COL32(92, 84, 64, 230));
                Im::PushStyleColor(Im::ImGuiCol_CheckMark, teal);
                Im::PushStyleColor(Im::ImGuiCol_SliderGrab, teal);
                Im::PushStyleColor(Im::ImGuiCol_Header, IM_COL32(67, 62, 49, 230));
                Im::PushStyleColor(Im::ImGuiCol_HeaderHovered, IM_COL32(86, 78, 60, 230));
                Im::PushStyleColor(Im::ImGuiCol_PopupBg, ink);
                Im::PushStyleVar(Im::ImGuiStyleVar_FramePadding, V{E(0.2F), E(0.09F)});
                Im::PushStyleVar(Im::ImGuiStyleVar_ItemSpacing, V{E(0.25F), E(0.12F)});
                Im::PushStyleVar(Im::ImGuiStyleVar_FrameRounding, 0.0F);
                Im::PushStyleVar(Im::ImGuiStyleVar_WindowPadding, V{E(0.35F), E(0.25F)});
            }
            ~Theme()
            {
                Im::PopStyleVar(4);
                Im::PopStyleColor(13);
            }
        };
        void SettingsBackground()
        {
            static const auto texture = MF::LoadTexture("Data/Interface/WalkWithMe/menu-background.png");
            if (!texture) {
                return;
            }
            // Window coordinates keep the artwork pinned while the settings content scrolls.
            // Submit before widgets so the image never draws over controls or captures input.
            const auto origin = Im::GetWindowPos();
            const auto window = Im::GetWindowSize();
            const float inset = E(.35F);
            const float availableWidth = window.x - 2 * inset - Im::GetStyle()->ScrollbarSize;
            const float availableHeight = window.y - 2 * inset;
            if (availableWidth <= 0 || availableHeight <= 0) {
                return;
            }
            constexpr float aspect = 742.0F / 465.0F; // Supplied WalkWithME.png, kept unmodified.
            const float width = std::min(availableWidth * .50F, availableHeight * .45F * aspect);
            const V end{origin.x + window.x - inset - Im::GetStyle()->ScrollbarSize,
                        origin.y + window.y - inset};
            Draw::AddImage(Im::GetWindowDrawList(), texture, {end.x - width, end.y - width / aspect}, end,
                           {0, 0}, {1, 1}, IM_COL32(255, 255, 255, 51));
        }
        // SceneDirector's compact density, with Walk With Me's ivory/gold palette.
        // Restore the child window's scale before ending it; no style leaks to another mod.
        struct SettingsPage
        {
            Theme theme;
            explicit SettingsPage(const char* id)
            {
                Im::BeginChild(id, {0, 0});
                Im::SetWindowFontScale(.85F);
                Im::PushStyleVar(Im::ImGuiStyleVar_ItemSpacing, V{6, 3});
                Im::PushStyleVar(Im::ImGuiStyleVar_FramePadding, V{4, 2});
                Im::PushStyleVar(Im::ImGuiStyleVar_CellPadding, V{6, 2});
                SettingsBackground();
            }
            ~SettingsPage()
            {
                Im::PopStyleVar(3);
                Im::SetWindowFontScale(1);
                Im::EndChild();
            }
            SettingsPage(const SettingsPage&) = delete;
            SettingsPage& operator=(const SettingsPage&) = delete;
        };
        bool Group(const char* title, int icon, bool open = true)
        {
            Space();
            const auto p = Im::GetCursorScreenPos();
            Icon(Im::GetWindowDrawList(), {p.x + E(.45F), p.y + E(.6F)}, E(.38F), icon, gold);
            Im::Dummy({E(1.35F), E(1.2F)});
            Im::SameLine(0, 0);
            Im::PushStyleColor(Im::ImGuiCol_Text, gold);
            const bool expanded = Im::CollapsingHeader(title, open ? Im::ImGuiTreeNodeFlags_DefaultOpen : 0);
            Im::PopStyleColor();
            return expanded;
        }
        std::string KeyName(std::uint32_t code)
        {
            if (code == unbound) {
                return "Unbound";
            }
            char name[96]{};

            const LONG scan = static_cast<LONG>((code & 0x7F) << 16) | ((code & 0x80) ? (1 << 24) : 0);
            if (GetKeyNameTextA(scan, name, sizeof(name)) > 0) {
                return name;
            }
            return fmt::format("Key {}", code);
        }
        void Prepare(const PartyView& view)
        {

            if (!haveDraft) {
                draft = view.settings;
                haveDraft = true;
            }
            if (!dirty && appliedRevision.load() == requestedRevision.load()) {
                const int result = applyResult.load();
                if (result != 2 && capturing.load() < 0) {
                    draft = Read().settings;
                }
                if (result == 2) {
                    feedback = "Could not save settings. Check file access, then change the setting again.";
                }
            }
            const int key = captured.exchange(-1);
            const int row = capturing.load();
            if (row >= 0 && key >= 0) {
                if ((key & 255) != 1) {
                    RebindHotkey(draft, row, Hotkeys::CapturedChord(key));
                    dirty = true;
                }
                CancelCapture();
            }
            if (capturing.load() >= 0 && GetTickCount64() - captureStarted.load() > 15000) {
                CancelCapture();
            }
        }

        void Header(const PartyView& view, const char* title)
        {
            Im::PushStyleColor(Im::ImGuiCol_Text, gold);
            Im::Text("WALK WITH ME / %s", title);
            Im::PopStyleColor();
            Im::SameLine(0, E(.65F));
            Im::TextDisabled("%zu companions", std::count_if(view.party.begin(), view.party.end(),
                                                             [](const auto& p) { return p.managed; }));
            Im::PushStyleColor(Im::ImGuiCol_Separator, gold);
            Im::Separator();
            Im::PopStyleColor();
            Space();
        }
        void SaveChanges()
        {
            if (!dirty) {
                return;
            }
            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) {
                feedback = "Settings are unavailable until SKSE is ready.";
                return;
            }
            bool startTask = false;
            {
                std::lock_guard lock(settingsMutex);
                pendingSettings = std::make_pair(++requestedRevision, draft);
                startTask = !settingsTaskQueued;
                settingsTaskQueued = true;
            }
            dirty = false;
            feedback.clear();
            if (!startTask) {
                return;
            }

            tasks->AddTask([] {
                for (;;) {
                    std::optional<std::pair<std::uint64_t, SettingsData>> update;
                    {
                        std::lock_guard lock(settingsMutex);
                        if (!pendingSettings) {
                            settingsTaskQueued = false;
                            return;
                        }
                        update = std::move(pendingSettings);
                        pendingSettings.reset();
                    }
                    auto& c = FormationController::GetSingleton();
                    const bool result = c.ApplyPreferences(update->second);
                    Cache(c.Snapshot());
                    applyResult.store(result ? 1 : 2);
                    appliedRevision.store(update->first);
                }
            });
        }
        void Footer()
        {
            SaveChanges();
            if (!feedback.empty()) {
                Note(feedback.c_str());
            }
        }
        void IssueStyle(FormationMode value)
        {
            Queue([value](auto& c) {
                c.SetMode(value);
                ++orderNotice;
                CommandGesture::Request(value);
            });
            SetWheelOpen(false);
        }

        void OrderCards(const PartyView& view)
        {
            Im::BeginDisabled(!view.inGame || !view.enabled);
            const auto names = OrderNames(view);
            if (Im::BeginTable("orders", 3, Im::ImGuiTableFlags_SizingStretchSame)) {
                for (int i = 0; i < Wheel::count; ++i) {
                    Im::TableNextColumn();
                    const bool selected = view.mode == profileModes[i];
                    if (selected) {
                        Im::PushStyleColor(Im::ImGuiCol_Text, gold);
                        Im::PushStyleColor(Im::ImGuiCol_Button, IM_COL32(67, 62, 49, 230));
                    }
                    const bool unavailable =
                        profileModes[i] == FormationMode::kCustom && !view.settings.customFormation.Defined();
                    Im::BeginDisabled(unavailable);
                    if (Im::Button(names[i], {-1, 0})) {
                        IssueStyle(profileModes[i]);
                    }
                    Im::EndDisabled();
                    Help(unavailable ? "Draw a custom formation in Travel settings first." : styleHints[i]);
                    if (selected) {
                        Im::PopStyleColor(2);
                    }
                }
                Im::EndTable();
            }
            Im::EndDisabled();
        }
        void OpenPackageControl(const PartyView& view, RE::FormID actor = 0)
        {
            if (!packageWindow || !view.inGame) {
                return;
            }
            packageSelection = actor ? actor : view.crosshairActor;
            if (!packageSelection) {
                Engine::Notify("Look at a companion first");
                return;
            }
            if (std::none_of(view.packageTargets.begin(), view.packageTargets.end(),
                             [](const auto& row) { return row.id == packageSelection; })) {
                Engine::Notify("Move closer to your companion");
                return;
            }
            Cache(view);
            packageGeneration = view.generation;
            packageAccept = false;
            packageCancel = false;
            packageShortcut = false;
            packagePush = false;
            packageFromWheel = commandWindow && commandWindow->IsOpen;
            if (commandWindow) {
                commandWindow->IsOpen = false;
            }
            wheelCloseRequested = false;
            packageWindow->IsOpen = true;
            QueueWheelBlur();
        }
        // ORNAMENT_BUTTON_BEGIN: shared with the offline visual proof.
        bool OrnamentButton(const char* id, const char* label, const char* key, V at, V size,
                            float visibility, CompanionMenu::ButtonMotion& motion, bool enabled = true)
        {
            // Keep the hit rectangle stationary while the artwork settles beneath it.
            Im::SetCursorScreenPos(at);
            Im::BeginDisabled(!enabled);
            const bool clicked = Im::InvisibleButton(id, size);
            const bool over = Im::IsItemHovered(), held = Im::IsItemActive();
            Im::EndDisabled();
            motion.Update(std::min(Im::GetIO()->DeltaTime, .05F), over, held, clicked);
            const float h = motion.hover, p = motion.press, a = visibility * (enabled ? 1.0F : .38F);
            at.y += size.y * (.18F * (1 - visibility) - .045F * h + .035F * p);
            const V end{at.x + size.x, at.y + size.y};
            auto* draw = Im::GetWindowDrawList();
            const float r = size.y * .16F;
            Draw::AddRectFilled(draw, {at.x, at.y + 3}, {end.x, end.y + 3}, IM_COL32(0, 0, 0, 65 * a), r, 0);
            Draw::AddRectFilled(draw, at, end,
                                IM_COL32(20 + 13 * h + 12 * motion.flash, 28 + 13 * h + 10 * motion.flash,
                                         29 + 10 * h, 245 * a),
                                r, 0);
            Draw::AddRect(
                draw, at, end,
                IM_COL32(166 + 43 * h, 151 + 38 * h, 114 + 25 * h, (85 + 110 * h + 45 * motion.flash) * a), r,
                0, 1);
            const float reach = (size.x * .18F + size.x * .25F * h) * Wheel::EaseOut(visibility);
            Draw::AddLine(draw, {at.x + size.x * .5F - reach, at.y}, {at.x + size.x * .5F + reach, at.y},
                          IM_COL32(229, 200, 143, (65 + 110 * h) * a), 1);
            const float badge = size.y * .52F;
            const V keyAt{at.x + size.y * .25F, at.y + size.y * .24F};
            Draw::AddRect(draw, keyAt, {keyAt.x + badge, keyAt.y + badge},
                          IM_COL32(177, 156, 111, (75 + 75 * h) * a), size.y * .07F, 0, 1);
            auto text = [&](const char* value, V center, float fontSize, float maxWidth, Im::ImU32 color) {
                const auto bounds = Im::CalcTextSize(value);
                fontSize = std::min(fontSize, maxWidth * Im::GetFontSize() / std::max(1.0F, bounds.x));
                Draw::AddText(
                    draw, Im::GetFont(), fontSize,
                    {center.x - bounds.x * fontSize / Im::GetFontSize() * .5F, center.y - fontSize * .5F},
                    color, value);
            };
            text(key, {keyAt.x + badge * .5F, keyAt.y + badge * .5F}, size.y * .24F, badge * .8F,
                 IM_COL32(197, 180, 136, 255 * a));
            {
                Inscription font;
                text(label, {at.x + size.y * .85F + (size.x - size.y * .95F) * .5F, at.y + size.y * .49F},
                     size.y * .39F, size.x - size.y * 1.1F, IM_COL32(235, 225, 203, 255 * a));
            }
            return enabled && clicked;
        }
        // ORNAMENT_BUTTON_END
        // COMPANION_PANEL_BEGIN: shared with the offline visual proof.
        int CompanionPanel(V origin, float w, const char* name, bool controlled, bool canTake,
                           const char* reason, CompanionMenu::Motion& motion, bool gamepad = false,
                           bool canPush = false, const char* pushReason = "")
        {
            auto* draw = Im::GetWindowDrawList();
            const float fade = motion.visibility, elapsed = motion.elapsed;
            const float dt = std::min(Im::GetIO()->DeltaTime, .05F);
            const V end{origin.x + w, origin.y + w * CompanionMenu::height};
            for (int layer = 3; layer > 0; --layer) {
                Draw::AddRectFilled(draw, {origin.x - layer * 3, origin.y + layer * 3},
                                    {end.x + layer * 3, end.y + layer * 5}, IM_COL32(0, 0, 0, 12 * fade),
                                    w * .025F, 0);
            }
            Draw::AddRectFilled(draw, origin, end, IM_COL32(20, 28, 29, 246 * fade), w * .018F, 0);
            Draw::AddRect(draw, {origin.x + 1, origin.y + 1}, {end.x - 1, end.y - 1},
                          IM_COL32(177, 156, 111, 110 * fade), w * .018F, 0, 1);
            auto text = [&](const char* value, float y, float size, Im::ImU32 color, float maxWidth) {
                const auto bounds = Im::CalcTextSize(value);
                size = std::min(size, maxWidth * Im::GetFontSize() / std::max(1.0F, bounds.x));
                Draw::AddText(draw, Im::GetFont(), size,
                              {origin.x + w * .5F - bounds.x * size / Im::GetFontSize() * .5F, origin.y + y},
                              color, value);
            };
            const float heading = motion.Reveal(.035F), settle = w * .012F * (1 - heading);
            text("C O M P A N I O N", w * .045F + settle, w * .018F, IM_COL32(170, 156, 122, 230 * heading),
                 w * .7F);
            {
                Inscription font;
                text(name, w * .09F + settle, w * .057F, IM_COL32(234, 224, 198, 255 * heading), w * .83F);
            }
            text(controlled ? "Travelling together"
                 : canTake  ? "Independent"
                            : "Unavailable",
                 w * .185F, w * .024F, IM_COL32(160, 176, 169, 255 * fade), w * .8F);
            const float rule = motion.Reveal(.08F);
            Draw::AddLine(draw, {origin.x + w * (.5F - .26F * rule), origin.y + w * .25F},
                          {origin.x + w * (.5F + .26F * rule), origin.y + w * .25F},
                          IM_COL32(177, 156, 111, 100 * fade), 1);
            int choice = 0;
            Im::BeginDisabled(motion.closing);
            for (int index = 0; index < 2; ++index) {
                const auto box = CompanionMenu::Action(index, w);
                const V at{origin.x + box.x, origin.y + box.y};
                const bool enabled = index ? controlled : canTake;
                Im::SetCursorScreenPos(at);
                Im::PushID(index);
                Im::BeginDisabled(!enabled);
                const bool clicked = Im::InvisibleButton("companion-action", {box.w, box.h});
                const bool over = Im::IsItemHovered(), held = Im::IsItemActive();
                Im::EndDisabled();
                Im::PopID();
                auto& button = motion.buttons[index];
                button.Update(dt, over, held, clicked);
                const float reveal = motion.Reveal(.09F + .055F * index);
                const float h = button.hover, a = reveal * (enabled ? 1.0F : .32F);
                const float lift = w * (.023F * (1 - reveal) - .005F * h + .004F * button.press);
                const V top{at.x, at.y + lift}, bottom{at.x + box.w, at.y + box.h + lift};
                Draw::AddRectFilled(draw, {top.x, top.y + 4}, {bottom.x, bottom.y + 4},
                                    IM_COL32(0, 0, 0, 65 * a), w * .012F, 0);
                Draw::AddRectFilled(draw, top, bottom,
                                    IM_COL32(26 + 12 * h + 18 * button.flash, 35 + 12 * h + 15 * button.flash,
                                             38 + 10 * h, 240 * a),
                                    w * .012F, 0);
                Draw::AddRect(draw, top, bottom, IM_COL32(197, 177, 131, (80 + 130 * h) * a), w * .012F, 0,
                              1);
                const V icon{at.x + box.w * .5F, top.y + box.h * .40F};
                const float ring = box.h * (.31F + .015F * h);
                Draw::PathArcTo(draw, icon, ring, -Wheel::pi * .5F,
                                -Wheel::pi * .5F + 2 * Wheel::pi * std::max(.001F, reveal), 48);
                Draw::PathStroke(draw, IM_COL32(177, 156, 111, (30 + 70 * h) * a), 0, 1);
                OrderVisuals::Icon(draw, icon, box.h * (.50F + .035F * h - .02F * button.press),
                                   index ? 0 : 2, std::max(0.0F, elapsed - .09F - .055F * index), a, true);
                const char* title = index ? "Release" : "Travel together";
                const float size = w * .030F;
                const auto bounds = Im::CalcTextSize(title);
                Draw::AddText(
                    draw, Im::GetFont(), size,
                    {at.x + box.w * .5F - bounds.x * size / Im::GetFontSize() * .5F, top.y + box.h * .77F},
                    IM_COL32(231, 220, 196, 255 * a), title);
                if (over) {
                    Im::SetTooltip("%s", index ? "Return to their own travel and rest behavior"
                                               : "Use your party's travel and rest orders");
                }
                if (enabled && clicked) {
                    choice = index + 1;
                }
            }
            if (!canTake && !controlled) {
                Im::SetCursorScreenPos({origin.x + w * .12F, origin.y + w * .17F});
                Im::InvisibleButton("unavailable-reason", {w * .76F, w * .08F});
                if (Im::IsItemHovered()) {
                    Im::SetTooltip("%s", reason);
                }
            }
            const auto cancel = CompanionMenu::Cancel(w);
            const auto recovery = CompanionMenu::Recovery(w);
            const bool push =
                OrnamentButton("companion-unstick", "Unstick", gamepad ? "X" : "U",
                               {origin.x + recovery.x, origin.y + recovery.y}, {recovery.w, recovery.h},
                               motion.Reveal(.18F), motion.buttons[3], canPush);
            if (Im::IsItemHovered(Im::ImGuiHoveredFlags_AllowWhenDisabled)) {
                Im::SetTooltip(
                    "%s",
                    canPush ? "Gentle push to help a frozen companion recover. Applies after the menu closes."
                            : pushReason);
            }
            const bool back = OrnamentButton("companion-cancel", "Cancel", gamepad ? "B" : "Esc",
                                             {origin.x + cancel.x, origin.y + cancel.y}, {cancel.w, cancel.h},
                                             motion.Reveal(.18F), motion.buttons[2]);
            Im::EndDisabled();
            return back ? 3 : push ? 4 : choice;
        }
        // COMPANION_PANEL_END
        void __stdcall RenderPackageControl()
        {
            if (!packageWindow || !packageWindow->IsOpen) {
                return;
            }
            const auto view = Read();
            Theme theme;
            if (!view.inGame || view.generation != packageGeneration) {
                packageWindow->IsOpen = false;
                QueueWheelBlur();
                return;
            }
            const auto selected = std::find_if(view.packageTargets.begin(), view.packageTargets.end(),
                                               [](const auto& row) { return row.id == packageSelection; });
            if (selected == view.packageTargets.end()) {
                packageWindow->IsOpen = false;
                QueueWheelBlur();
                return;
            }
            const auto& row = *selected;
            const auto display = Im::GetIO()->DisplaySize;
            const float w = CompanionMenu::Width(display.x, display.y);
            Im::SetNextWindowPos({display.x * .5F, display.y * .5F}, Im::ImGuiCond_Always, {.5F, .5F});
            Im::SetNextWindowSize({w, w * CompanionMenu::height}, Im::ImGuiCond_Always);
            static CompanionMenu::Motion motion;
            Im::PushStyleVar(Im::ImGuiStyleVar_WindowBorderSize, 0.0F);
            if (Im::Begin("##WalkWithMeCompanion", nullptr,
                          Im::ImGuiWindowFlags_NoDecoration | Im::ImGuiWindowFlags_NoBackground |
                              Im::ImGuiWindowFlags_NoSavedSettings | Im::ImGuiWindowFlags_NoNav)) {
                if (Im::IsWindowAppearing()) {
                    motion = {};
                }
                motion.Update(Im::GetIO()->DeltaTime);
                const float backdrop = packageFromWheel && !motion.closing ? 1.0F : motion.visibility;
                Draw::AddRectFilled(Im::GetBackgroundDrawList(), {0, 0}, display,
                                    IM_COL32(7, 12, 16, 165 * backdrop), 0, 0);
                const int choice = CompanionPanel(Im::GetWindowPos(), w, row.name.c_str(), row.controlled,
                                                  row.canTake, row.state.c_str(), motion, padUsed.load(),
                                                  row.canPush, row.pushReason.c_str());
                const bool accept =
                    packageAccept.exchange(false) || Im::IsKeyPressed(Im::ImGuiKey_Enter, false);
                const bool push =
                    packagePush.exchange(false) || Im::IsKeyPressed(Im::ImGuiKey_U, false) || choice == 4;
                const bool cancel = packageCancel.exchange(false) ||
                                    Im::IsKeyPressed(Im::ImGuiKey_Escape, false) || choice == 3;
                if (cancel) {
                    motion.Close();
                }
                else if (!motion.closing && push && row.canPush) {
                    const auto id = row.id;
                    const auto stamp = packageGeneration;
                    motion.buttons[3].flash = 1;
                    Queue([id, stamp](auto& c) { c.RequestCompanionPush(id, stamp); });
                    motion.Close();
                }
                else if (!motion.closing &&
                         (choice == 1 || choice == 2 || (accept && (row.controlled || row.canTake)))) {
                    const auto id = row.id;
                    const auto stamp = packageGeneration;
                    const bool take = choice ? choice == 1 : !row.controlled;
                    motion.buttons[take ? 0 : 1].flash = 1;
                    Queue([id, take, stamp](auto& c) { c.SetPackageControl(id, take, stamp); });
                    motion.Close();
                }
            }
            Im::End();
            Im::PopStyleVar();
            if (motion.Finished()) {
                packageWindow->IsOpen = false;
                QueueWheelBlur();
            }
        }
        void Roster(const PartyView& view)
        {
            Note("Nearby recruited followers appear here. Add companions here or through dialogue.");
            if (view.party.empty()) {
                Note("No recruited followers detected nearby.");
                return;
            }
            if (Im::BeginTable("companions", 4,
                               Im::ImGuiTableFlags_RowBg | Im::ImGuiTableFlags_SizingStretchProp)) {
                Im::TableSetupColumn("Companion", Im::ImGuiTableColumnFlags_WidthStretch);
                Im::TableSetupColumn("Status", Im::ImGuiTableColumnFlags_WidthStretch);
                Im::TableSetupColumn("Personality", Im::ImGuiTableColumnFlags_WidthFixed, E(8));
                Im::TableSetupColumn("Manage", Im::ImGuiTableColumnFlags_WidthFixed, E(5));
                Im::TableHeadersRow();
                for (const auto& row : view.party) {
                    Im::PushID(static_cast<int>(row.id));
                    Im::TableNextRow();
                    Im::TableNextColumn();
                    Im::TextUnformatted(row.name.c_str());
                    if (Im::IsItemHovered()) {
                        Im::SetTooltip("%s\n%s%s", row.state.c_str(), row.personality.c_str(),
                                       row.nff ? "\nManaged by NFF" : "");
                    }
                    Im::TableNextColumn();
                    Im::TextUnformatted(row.packageControlled          ? "Travel control imported"
                                        : row.manuallyAdded            ? "Added manually"
                                        : row.apiRegistered            ? "Managed by integration"
                                        : row.managed && !row.excluded ? "Added automatically"
                                        : row.excluded                 ? "Removed"
                                                                       : "Not added");
                    Help(row.state.c_str());
                    Im::TableNextColumn();
                    Im::SetNextItemWidth(-1);
                    auto pref = draft.personalityOverrides.find(row.personalityKey);
                    int choice = pref == draft.personalityOverrides.end() ? 0 : pref->second + 1;
                    Im::BeginDisabled(!draft.personalities);
                    if (Im::Combo("##personality", &choice,
                                  "Automatic\0Balanced\0Sociable\0Curious\0Reserved\0Watchful\0")) {
                        if (choice == 0) {
                            draft.personalityOverrides.erase(row.personalityKey);
                        }
                        else {
                            draft.personalityOverrides[row.personalityKey] = choice - 1;
                        }
                        dirty = true;
                    }
                    Help(row.personality.c_str());
                    Im::EndDisabled();
                    Im::TableNextColumn();
                    const bool remove =
                        row.packageControlled || row.manuallyAdded || (!row.excluded && row.managed);
                    // Adding records the choice in any travel style; it takes effect once one directs.
                    Im::BeginDisabled(row.apiRegistered || (!remove && !row.eligible && !row.teammate));
                    if (Im::Button(row.packageControlled ? "Control" : remove ? "Remove" : "Add", {-1, 0})) {
                        const auto id = row.id;
                        const auto stamp = view.generation;
                        const bool manual = row.manuallyAdded;
                        if (row.packageControlled) {
                            OpenPackageControl(view, id);
                        }
                        else {
                            Queue([id, remove, manual, stamp](auto& c) {
                                if (c.Snapshot().generation != stamp) {
                                    return;
                                }
                                if (remove && !manual) {
                                    (void)c.ExcludeFollower(id);
                                    return;
                                }
                                c.SetDialogueManagement(RE::TESForm::LookupByID<RE::Actor>(id), !remove);
                            });
                        }
                    }
                    Help(row.packageControlled ? "Open this companion's travel control popup."
                         : row.apiRegistered   ? "This registration is owned by another integration."
                         : remove ? "Release Walk With Me's control and prevent automatic re-addition. This "
                                    "does not dismiss the follower."
                                  : "Allow Walk With Me to direct this companion's travel and relaxation. "
                                    "Works for any follower, whatever recruited them, and in any travel style. "
                                    "The choice is saved and can be removed here or in dialogue.");
                    Im::EndDisabled();
                    Im::PopID();
                }
                Im::EndTable();
            }
        }
        void Slider(const char* name, float& value, float low, float high, const char* format,
                    const char* hint)
        {
            Im::TableNextRow();
            Im::TableNextColumn();
            Im::AlignTextToFramePadding();
            Im::TextWrapped("%s", name);
            Help(hint);
            Im::TableNextColumn();
            Im::SetNextItemWidth(-1);
            Im::PushID(name);
            dirty |= Im::SliderFloat("##value", &value, low, high, format, Im::ImGuiSliderFlags_AlwaysClamp);
            Help(hint);
            Im::PopID();
        }
        void IntSlider(const char* name, int& value, int low, int high, const char* format, const char* hint)
        {
            Im::TableNextRow();
            Im::TableNextColumn();
            Im::AlignTextToFramePadding();
            Im::TextWrapped("%s", name);
            Help(hint);
            Im::TableNextColumn();
            Im::SetNextItemWidth(-1);
            Im::PushID(name);
            dirty |= Im::SliderInt("##value", &value, low, high, format, Im::ImGuiSliderFlags_AlwaysClamp);
            Help(hint);
            Im::PopID();
        }
        void Toggle(const char* name, bool& value, const char* hint)
        {
            Im::TableNextRow();
            Im::TableNextColumn();
            Im::AlignTextToFramePadding();
            Im::TextWrapped("%s", name);
            Help(hint);
            Im::TableNextColumn();
            Im::PushID(name);
            dirty |= Im::Checkbox("##value", &value);
            Help(hint);
            Im::PopID();
        }
        bool Rows(const char* id)
        {
            if (!Im::BeginTable(id, 2,
                                Im::ImGuiTableFlags_SizingStretchProp | Im::ImGuiTableFlags_NoSavedSettings |
                                    Im::ImGuiTableFlags_PadOuterX)) {
                return false;
            }
            Im::TableSetupColumn("Setting", Im::ImGuiTableColumnFlags_WidthStretch, .40F);
            Im::TableSetupColumn("Value", Im::ImGuiTableColumnFlags_WidthStretch, .60F);
            return true;
        }
        bool Checks(const char* id, int maximum = 2)
        {
            // Checkbox + label share a cell. Keep room for longer labels and collapse on narrow panels.
            const int columns =
                std::clamp(static_cast<int>(Im::GetContentRegionAvail().x / E(13)), 1, maximum);
            return Im::BeginTable(id, columns,
                                  Im::ImGuiTableFlags_SizingStretchSame |
                                      Im::ImGuiTableFlags_NoSavedSettings | Im::ImGuiTableFlags_PadOuterX);
        }
        void Check(const char* name, bool& value, const char* hint)
        {
            Im::TableNextColumn();
            dirty |= Im::Checkbox(name, &value);
            Help(hint);
        }
        void __stdcall RenderParty()
        {
            const auto view = Read();
            Prepare(view);
            SettingsPage page("WalkWithMeParty");
            Header(view, "PARTY");
            if (Rows("partyEnabled")) {
                Toggle("Enable Walk With Me", draft.enabled, "Allow Walk With Me to manage the party.");
                Im::EndTable();
            }
            if (Group("Party orders", 0)) {
                OrderCards(view);
            }
            Label("COMPANIONS", 5);
            Roster(view);
            Im::BeginDisabled(!view.inGame);
            if (Im::Button("Companion control...")) {
                OpenPackageControl(view);
            }
            Help("Open a separate window to explicitly manage a nearby custom companion's travel or release "
                 "a previous import.");
            Im::EndDisabled();
            if (Group("Enrollment", 5)) {
                if (Rows("enrollment")) {
                    IntSlider("Companion limit", draft.maxFollowers, 1, PARTY_CAPACITY, "%d",
                              "Maximum managed companions.");
                    Im::EndTable();
                }
                if (Checks("enrollmentOptions")) {
                    Check("Automatically add followers", draft.autoDiscover,
                          "Add recruited followers as they load nearby. Removed companions remain excluded. "
                          "Manual Add still works.");
                    Im::BeginDisabled(!draft.autoDiscover);
                    Im::BeginDisabled(!view.nffInstalled);
                    Check("Include NFF followers", draft.enforceNFF,
                          "Include active NFF followers in automatic enrollment.");
                    Im::EndDisabled();
                    Check("Include custom followers", draft.enforceCustomFollowers,
                          "Include recruited custom teammates in automatic enrollment. Detection is not a "
                          "compatibility guarantee.");
                    Im::EndDisabled();
                    Im::EndTable();
                }
            }
            Footer();
        }
        void __stdcall RenderTravel()
        {
            const auto view = Read();
            Prepare(view);
            SettingsPage page("WalkWithMeTravel");
            Header(view, "TRAVEL");
            if (Group("Formation", 4) && Rows("travel")) {
                Im::TableNextColumn();
                Im::TextUnformatted("Default travel style");
                Im::TableNextColumn();
                Im::SetNextItemWidth(-1);
                auto styles = profileNames;
                styles[6] = draft.customFormation.title.c_str();
                const int choices = draft.customFormation.Defined() ? Wheel::count : Wheel::count - 1;
                const int current = std::clamp(static_cast<int>(draft.mode), 0, Wheel::count - 1);
                if (Im::BeginCombo("##mode", styles[current])) {
                    for (int i = 0; i < choices; ++i) {
                        if (Im::Selectable(styles[i], i == current)) {
                            draft.mode = profileModes[i];
                            dirty = true;
                        }
                    }
                    Im::EndCombo();
                }
                Slider("Spacing", draft.travel.spacing, .6F, 3.0F, "%.2fx",
                       "Distance between travelling companions. Larger values spread them out; interiors and "
                       "narrow paths still constrain space. Changes take effect immediately.");
                Im::TableNextColumn();
                Im::TextUnformatted("Adjust spacing");
                Im::TableNextColumn();
                if (Im::SmallButton("Tighten")) {
                    draft.travel.spacing = std::max(.6F, draft.travel.spacing - .2F);
                    dirty = true;
                }
                Im::SameLine();
                if (Im::SmallButton("Spread out")) {
                    draft.travel.spacing = std::min(3.0F, draft.travel.spacing + .2F);
                    dirty = true;
                }
                if (draft.mode == FormationMode::kDynamic) {
                    Slider("Straggler distance", draft.travel.naturalStragglerDistance, 0, 800, "%.0f units",
                           "Extra setback for one or two Natural-mode companions. Their mutual spacing stays "
                           "normal. The choice stays stable while travelling; zero disables stragglers.");
                }
                Im::EndTable();
            }
            if (Group("Custom formation", crestIcon + draft.customFormation.emblem) && Rows("customFormation")) {
                auto& custom = draft.customFormation;
                Im::TableNextColumn();
                Im::AlignTextToFramePadding();
                Im::TextUnformatted("Name");
                Help("What the formation is called on the party wheel, the orders HUD and here.");
                Im::TableNextColumn();
                static std::array<char, 64> titleBuffer{};
                static bool editingTitle = false;
                if (!editingTitle) {
                    const auto n = std::min(custom.title.size(), Custom::maxTitle);
                    std::fill(titleBuffer.begin(), titleBuffer.end(), char{});
                    std::copy_n(custom.title.begin(), n, titleBuffer.begin());
                }
                Im::SetNextItemWidth(-1);
                if (Im::InputText("##customTitle", titleBuffer.data(), Custom::maxTitle + 1)) {
                    custom.title = titleBuffer.data();
                    dirty = true;
                }
                editingTitle = Im::IsItemActive();
                Im::TableNextColumn();
                Im::AlignTextToFramePadding();
                Im::TextUnformatted("Emblem");
                Help("The icon shown for the formation on the party wheel and the orders HUD.");
                Im::TableNextColumn();
                if (EmblemPicker(Im::GetCursorScreenPos(), Im::GetContentRegionAvail().x, E(2.1F), custom.emblem)) {
                    dirty = true;
                }
                std::string reserved;
                for (int i = 0; i < custom.count; ++i) {
                    if (!custom.names[i].empty()) {
                        reserved += (reserved.empty() ? "" : ", ") + custom.names[i] + " at " + std::to_string(i + 1);
                    }
                }
                Im::TableNextColumn();
                Im::AlignTextToFramePadding();
                Im::TextUnformatted("Places");
                Help("Draw where each companion walks, and optionally who stands where. It appears on the "
                     "party wheel once saved.");
                Im::TableNextColumn();
                Im::TextUnformatted(custom.Defined()
                                        ? (std::to_string(custom.count) + (custom.count == 1 ? " place" : " places") +
                                           (reserved.empty() ? "" : " - " + reserved))
                                              .c_str()
                                        : "None yet");
                Im::TableNextColumn();
                Im::TableNextColumn();
                Im::BeginDisabled(!view.inGame);
                if (Im::Button(custom.Defined() ? "Edit formation" : "Create formation", {-1, 0})) {
                    OpenFormationEditor(view);
                }
                Help(view.inGame ? "Open the formation editor." : "Load a save first; the editor lists your party.");
                Im::EndDisabled();
                Im::EndTable();
            }
            if (Group("Catch-up & teleport", 4) && Rows("catchup")) {
                Slider("Catch-up response", draft.travel.catchUpSeconds, 1, 6, "%.1f s",
                       "Lower values close gaps more quickly.");
                Slider("Catch-up speed cap", draft.travel.maxSpeedScale, 1, 1.75F, "%.2fx",
                       "Normal catch-up limit. Distant companions use twice this value, then ease back to "
                       "your pace.");
                Slider("Distant boost starts", draft.distantCatchupStart, 300,
                       std::min(2400.0F, draft.releaseDistance - 100), "%.0f units",
                       "Beyond this player distance, catch-up uses twice your selected speed cap.");
                Slider("Normal pace within", draft.distantCatchupEnd, 150, draft.distantCatchupStart - 100,
                       "%.0f units",
                       "The boost ends inside this distance. Separate thresholds prevent repeated speed "
                       "switching.");
                Toggle("Teleport distant companions", draft.teleportCatchup,
                       "Bring companions back after two seconds far away, or three seconds left in another "
                       "cell. Works while you ride, sneak, fight, sit, craft or stand still, and pulls a "
                       "companion out of a chair or bed if need be. Lands them behind you, then on your "
                       "trail, then anywhere reachable nearby. Waiting, fighting, riding and scripted "
                       "companions stay put, and so does everyone in Sovngarde, Skuldafn, Apocrypha, the "
                       "Soul Cairn and the Underforge.");
                Im::BeginDisabled(!draft.teleportCatchup);
                Slider(
                    "Teleport beyond", draft.teleportDistance, 1500, 15000, "%.0f units",
                    "Distance from you before teleport catch-up. Each companion has an 8-second cooldown.");
                Im::EndDisabled();
                Toggle("Leave relaxing companions in place", draft.holdRelaxing,
                       "During a Relax order, companions keep their camp however far you go: nothing "
                       "teleports them, NFF included. Off: they catch up like everyone else and relax "
                       "wherever they land. Temporary rest never holds anyone back.");
                Im::BeginDisabled(!view.nffInstalled);
                Toggle("Take over NFF's catch-up", draft.replaceFrameworkCatchup,
                       "While Walk With Me teleports, it also teleports NFF's own followers and parks NFF's "
                       "Catch Up timer, so the two never race and a Relax camp stays put. NFF's value is put "
                       "back before every save, while the journal is open, and whenever this is off. "
                       "Nothing changes if NFF's Catch Up is off.");
                Im::EndDisabled();
                Im::EndTable();
            }
            if (Group("Movement tuning", 4, false) && Rows("movement")) {
                Slider("Anticipation", draft.travel.lookAhead, .2F, 1.5F, "%.2f s",
                       "How far ahead the party predicts your route.");
                Toggle("Avoid obstacles ahead", draft.forwardCollision,
                       "Take short, bounded detours around rocks while traveling nearby in the wilderness. "
                       "Towns, interiors and distant catch-up use normal pathfinding. If a local detour "
                       "fails, keep the native travel route. Hand-holding uses its own collision checks.");
                Slider("Turn response delay", draft.travel.turnDelay, 0, .6F, "%.2f s",
                       "Wait briefly before following a new direction, ignoring little zigzags and softening "
                       "side steps. Real reversals respond sooner. Zero restores immediate steering. Does "
                       "not affect Vanilla mode.");
                Slider("Companion individuality", draft.travel.individuality, 0, 1, "%.2fx",
                       "Each companion has their own reaction time, turn response and comfortable pace. They "
                       "let a small gap grow, then catch up. Higher values make the party less synchronized; "
                       "zero restores shared movement. Large gaps still recover promptly. Does not affect "
                       "Vanilla or sandboxing.");

                bool mirror = draft.preferredSide < 0;
                Toggle("Swap preferred side", mirror, "Swap left and right.");
                draft.preferredSide = mirror ? -1 : 1;
                Toggle("Outdoors only", draft.disableIndoors, "Release formation control inside buildings.");
                Im::EndTable();
            }
            if (Group("Hand holding (experimental)", 7, false) && Rows("handHolding")) {
                auto& hands = draft.handHolding;
                Im::TableNextColumn();
                Im::TextUnformatted("Hand-holding method");
                Im::TableNextColumn();
                Im::SetNextItemWidth(-1);
                const bool classic = hands.method == HandHolding::Method::Classic;
                if (Im::BeginCombo("##handHoldMethod", classic ? "Classic" : "TETHER")) {
                    if (Im::Selectable("Classic", classic)) {
                        hands.method = HandHolding::Method::Classic;
                        dirty = true;
                    }
                    if (Im::Selectable("TETHER", !classic)) {
                        hands.method = HandHolding::Method::Tether;
                        dirty = true;
                    }
                    Im::EndCombo();
                }
                Help("Classic restores the earlier procedural hand holding and needs no generated clasp "
                     "animations. TETHER uses a physics grip and requires generated OAR clasp clips. "
                     "Changing methods releases the current grip before reconnecting.");
                Toggle("Hold hands (experimental)", hands.enabled,
                       "Work in progress: hands and wrists can still twist, and approach/turn animations may "
                       "look awkward. Disabled by default. Choose one companion to hold hands in "
                       "third-person Companion mode while walking, jogging or running. Sprinting, weapons, "
                       "combat, jumping, swimming, scenes and rest release the grip.");
                Im::TableNextColumn();
                Im::TextUnformatted("Hand-holding companion");
                Im::TableNextColumn();
                Im::SetNextItemWidth(-1);
                std::string preview = hands.partner.empty()       ? "None"
                                      : hands.partnerName.empty() ? "Selected companion"
                                                                  : hands.partnerName;
                const bool nearby = std::any_of(view.party.begin(), view.party.end(), [&](const auto& row) {
                    return !hands.partner.empty() && row.handHoldKey == hands.partner;
                });
                if (!hands.partner.empty() && !nearby) {
                    preview += " (not nearby)";
                }
                if (Im::BeginCombo("##handHoldPartner", preview.c_str())) {
                    if (Im::Selectable("None", hands.partner.empty())) {
                        hands.partner.clear();
                        hands.partnerName.clear();
                        dirty = true;
                    }
                    for (const auto& row : view.party) {
                        Im::PushID(static_cast<int>(row.id));
                        const bool selectable = !row.excluded && !row.handHoldKey.empty();
                        Im::BeginDisabled(!selectable);
                        if (Im::Selectable(row.name.c_str(),
                                           !hands.partner.empty() && row.handHoldKey == hands.partner)) {
                            hands.partner = row.handHoldKey;
                            hands.partnerName = row.name;
                            dirty = true;
                        }
                        Im::EndDisabled();
                        if (!selectable) {
                            Help(row.handHoldKey.empty() ? "This temporary actor has no persistent reference "
                                                           "to remember. Choose a placed companion."
                                                         : "Include this companion on the Party page first.");
                        }
                        Im::PopID();
                    }
                    Im::EndCombo();
                }
                Help("Only this companion is eligible. They approach your side using normal pathfinding. "
                     "Pause to let them arrive, turn and connect before walking together. After a failed "
                     "attempt, step away before trying again. Selection saves automatically.");
                Slider("Lead-in delay", hands.delay, .5F, 10, "%.1f s",
                       "Time in eligible Companion mode before looking for the selected follower. Paused "
                       "menus do not count. Releasing the grip restarts this delay.");
                Slider("Connect within", hands.connectDistance, 60, 110, "%.0f units",
                       "Pause and let the selected companion approach your side. Classic connects when "
                       "the arms can reach; TETHER waits for aligned, close hands before its physics grip.");
                hands.releaseDistance = std::max(hands.releaseDistance, hands.connectDistance + 10);
                Slider("Release beyond", hands.releaseDistance, hands.connectDistance + 10, 160, "%.0f units",
                       "Release when the pair separates beyond this distance. TETHER allows a brief "
                       "grace period. Reconnection waits through the lead-in again.");
                if (classic) {
                    Slider("Hand spacing", hands.spacing, 36, 56, "%.0f units",
                           "Classic: distance between you and your companion while holding hands. Around "
                           "40 puts your shoulders together; wider leaves room between you. A change "
                           "releases the grip, which then re-forms at the new spacing.");
                    Im::TableNextColumn();
                    Im::TextUnformatted("Companion side");
                    Im::TableNextColumn();
                    Im::SetNextItemWidth(-1);
                    static constexpr const char* sides[]{"Left", "Nearest", "Right"};
                    hands.side = std::clamp(hands.side, -1, 1);
                    if (Im::BeginCombo("##handHoldSide", sides[hands.side + 1])) {
                        for (int side = -1; side <= 1; ++side) {
                            if (Im::Selectable(sides[side + 1], hands.side == side)) {
                                hands.side = side;
                                dirty = true;
                            }
                        }
                        Im::EndCombo();
                    }
                    Help("Classic: which of your hands the companion takes. Nearest uses whichever side "
                         "they arrive on. If your camera sits over one shoulder and the companion fades "
                         "out while holding hands, choose the other side. A change releases the grip.");
                }
                Im::EndTable();
            }
            if (Im::SmallButton("Restore travel defaults")) {
                const float restDelay = draft.travel.idleRelease;
                draft.travel = SettingsData{}.travel;
                draft.travel.idleRelease = restDelay;
                draft.distantCatchupStart = 900;
                draft.distantCatchupEnd = 500;
                draft.teleportCatchup = true;
                draft.teleportDistance = 4000;
                draft.holdRelaxing = true;
                draft.replaceFrameworkCatchup = true;
                draft.forwardCollision = true;
                draft.preferredSide = 1;
                draft.disableIndoors = false;
                dirty = true;
            }
            Footer();
        }
        void __stdcall RenderRest()
        {
            const auto view = Read();
            Prepare(view);
            SettingsPage page("WalkWithMeRest");
            Header(view, "REST");
            if (Group("Rest & exploration", 3) && Rows("sandbox")) {
                Toggle("Temporary rest", draft.sandbox.automatic,
                       "Automatically rest after you stand still, then resume following when you leave the "
                       "rest area. Turn off to disable automatic rest; the Relax command remains available. "
                       "Saves immediately.");
                Im::BeginDisabled(!draft.sandbox.automatic);
                Slider("Begin after", draft.travel.idleRelease, 2, 30, "%.0f s",
                       "Time standing still before automatic sandboxing. Travel resumes when you leave the "
                       "resume area.");
                Im::EndDisabled();
                Slider("Nearby rest radius", draft.sandbox.startRadius, 40,
                       std::min(600.0F, draft.releaseDistance - 200), "%.0f units",
                       "Automatic stops only, until free roam opens. Companions head for activities inside "
                       "it straight away; very small areas can leave them standing idle.");
                draft.sandbox.maxRadius = std::max(draft.sandbox.maxRadius, draft.sandbox.startRadius);
                Slider("Free-roam radius", draft.sandbox.maxRadius, draft.sandbox.startRadius,
                       std::min(1800.0F, draft.releaseDistance - 200), "%.0f units",
                       "Activity area used immediately by a Relax order, or after the automatic nearby phase "
                       "ends. Companions set out for seats, idle markers or open ground across it at once.");
                Slider("Indoor & town range", draft.sandbox.settledActivityRadius, 180,
                       std::min(1800.0F, draft.releaseDistance - 200), "%.0f units",
                       "Use nearby seats and activity markers from the start in homes, inns, other safe "
                       "interiors and towns. Capped by Free-roam radius. Wilderness and dungeons keep the "
                       "nearby phase; temporary rest keeps its resume distance.");
                Im::EndTable();
            }
            if (Group("Rest timing & limits", 3, false) && Rows("restTuning")) {
                Slider("Activity height", draft.sandbox.activityHeight, 64, 1600, "%.0f units",
                       "How far above or below the fixed rest anchor to seek activities. Connected stairs or "
                       "ramps are required; low values keep searches near the current floor. Does not change "
                       "Skyrim's global sandbox settings.");
                Slider("Free roam after", draft.sandbox.fullSandboxAfter, 0, 180, "%.0f s",
                       "Automatic stops only. One transition to the larger radius, after which companions "
                       "head out across it. A direct Relax order skips this wait.");
                Slider("Resume following beyond", draft.sandbox.resumeDistance, 100, 1000, "%.0f units",
                       "Temporary rest continues while you stay within this distance of where you stopped, "
                       "including dialogue. Relax ignores player distance.");
                Slider("Idle recovery delay", draft.sandbox.idleRecoveryAfter, 0, 120, "%.0f s",
                       "After sustained inactivity, allow a short walk inside the rest area. Personality "
                       "adjusts this delay; staggered by up to 15 seconds, one companion at a time. Zero "
                       "disables recovery.");
                Im::EndTable();
            }
            if (Group("Activities", 3) && Checks("activities")) {
                Check(
                    "Companion conversations", draft.sandbox.social,
                    "From time to time two resting companions pair up and talk for two to three minutes, "
                    "with silent gestures. If they are apart, one walks over first. With Area activities the "
                    "place shapes it: hushed in caves and ruins, over a drink at an inn.");
                Check(
                    "Area activities", draft.sandbox.areaActivities,
                    "Resting fits the place: warming hands at fires, praying at shrines, the water's edge "
                    "and lookouts outdoors, sweeping at home, nervous glances in caves and ruins. At an inn "
                    "they share a table, call a server for drinks and applaud the bard.");
                Check("Extra rest poses", draft.sandbox.extraPoses,
                      "Optional scripted poses. Off by default: Skyrim chooses its normal furniture, idles "
                      "and activities.");
                if (draft.sandbox.extraPoses) {
                    Check("Ground sitting", draft.sandbox.groundSitting,
                          "Sit on suitable outdoor wilderness ground. Never used inside buildings or "
                          "dungeons.");
                }
                Check("Eating & drinking", draft.sandbox.meals,
                      "Native meals plus occasional refreshments for idle companions in safe interiors and "
                      "towns. Inns favor drinking; other places favor food. Does not require Extra rest "
                      "poses. Disabled in caves and dungeons.");
                if (draft.sandbox.extraPoses) {
                    Check("Reading", draft.sandbox.reading, "Read during quiet rests in safe locations.");
                }
                if (draft.sandbox.extraPoses) {
                    Check("Stretching", draft.sandbox.stretching,
                          "Occasional brief stretches between longer rests.");
                }
                Im::EndTable();
            }
            if (Group("Party life", 5)) {
                if (Checks("partyLife")) {
                    Check("Give conversations space", draft.conversationAwareness,
                          "Other idle companions step aside during player dialogue. The speaking NPC, "
                          "furniture "
                          "users and active scenes are untouched.");
                    Check(
                        "Walk during follower banter", draft.walkingBanter,
                        "Keep simple two-follower conversations moving. Partners try to trade positions with "
                        "a nearby companion and walk beside each other. Scripted movement scenes and player "
                        "dialogue retain control.");
                    Check(
                        "Companion personalities", draft.personalities,
                        "Stable automatic tendencies affect socializing, exploration and lookout preference. "
                        "Override each companion on the Party page.");
                    Check("Occasional lookout", draft.lookouts,
                          "During longer wilderness or dungeon rests, one free companion watches briefly. "
                          "Requires at least three companions so two can still socialize.");
                    Im::EndTable();
                }
                if ((draft.conversationAwareness || draft.lookouts) && Rows("partyLifeTuning")) {
                    if (draft.conversationAwareness) {
                        Slider("Conversation space", draft.conversationClearance, 80, 300, "%.0f units",
                               "Clearance around you, the NPC and the space between you. Only reachable "
                               "positions are used.");
                    }
                    if (draft.lookouts) {
                        Slider("Lookout after", draft.lookoutAfter, 30, 180, "%.0f s",
                               "Minimum rest time before a lookout. Watches last 30-45 seconds, with 90-150 "
                               "seconds between watches.");
                    }
                    Im::EndTable();
                }
            }
            if (Im::SmallButton("Restore rest defaults")) {
                draft.sandbox = SettingsData{}.sandbox;
                draft.travel.idleRelease = SettingsData{}.travel.idleRelease;
                dirty = true;
            }
            Footer();
        }
        // Who takes a role: anyone, or one companion (a persistent reference key).
        void CompanionPicker(const char* id, const PartyView& view, std::string& key, std::string& name,
                             const char* anyone)
        {
            std::string shown = key.empty() ? anyone : name.empty() ? "Selected companion" : name;
            if (!key.empty() && std::none_of(view.party.begin(), view.party.end(),
                                             [&](const auto& row) { return row.handHoldKey == key; })) {
                shown += " (not nearby)";
            }
            Im::SetNextItemWidth(-1);
            if (Im::BeginCombo(id, shown.c_str())) {
                if (Im::Selectable(anyone, key.empty())) {
                    key.clear();
                    name.clear();
                    dirty = true;
                }
                for (const auto& row : view.party) {
                    Im::PushID(static_cast<int>(row.id));
                    const bool selectable = !row.excluded && !row.handHoldKey.empty();
                    Im::BeginDisabled(!selectable);
                    if (Im::Selectable(row.name.c_str(), !key.empty() && row.handHoldKey == key)) {
                        key = row.handHoldKey;
                        name = row.name;
                        dirty = true;
                    }
                    Im::EndDisabled();
                    if (!selectable) {
                        Help(row.handHoldKey.empty() ? "This temporary actor has no persistent reference "
                                                       "to remember. Choose a placed companion."
                                                     : "Include this companion on the Party page first.");
                    }
                    Im::PopID();
                }
                Im::EndCombo();
            }
        }
        void __stdcall RenderRoles()
        {
            const auto view = Read();
            Prepare(view);
            SettingsPage page("WalkWithMeRoles");
            Header(view, "ROLES");
            if (Group("Loot scouting", 8)) {
                auto& loot = draft.loot;
                dirty |= Im::Checkbox("Point out loot", &loot.enabled);
                Help("Your loot finder looks over the chests, bodies and valuable items in view, walks to "
                     "the richest one and kneels to search it. One find per area. Works while travelling "
                     "or standing still, not during rest or Relax.");
                Im::BeginDisabled(!loot.enabled);
                if (Rows("lootFinder")) {
                    Im::TableNextColumn();
                    Im::TextUnformatted("Loot finder");
                    Im::TableNextColumn();
                    CompanionPicker("##lootFinder", view, loot.finder, loot.finderName, "Any companion");
                    Help("Only one companion goes to a find at a time. Choose who does it, or leave Any "
                         "companion "
                         "to send whoever is nearest. Selection saves automatically.");
                    Im::EndTable();
                }
                if (Checks("lootFilters")) {
                    Check(
                        "Containers", loot.containers,
                        "Chests, urns, sacks and other containers. Owned containers (stealing) and your own "
                        "storage are ignored. Food-only containers are skipped.");
                    Check("Bodies", loot.bodies,
                          "Dead people and creatures carrying real loot. Bodies with only pelts, meat or "
                          "ingredients are skipped.");
                    Check("Loose valuables", loot.looseItems,
                          "Unowned items lying in the world, but only when they are worth more than the "
                          "valuable threshold.");
                    Check(
                        "Inside homes and inns", loot.inhabitedInteriors,
                        "Off: no scouting inside houses, inns, temples, shops, castles, guilds and your own "
                        "home. "
                        "Caves, ruins and the outdoors are unaffected.");
                    Im::EndTable();
                }
                Im::EndDisabled();
            }
            if (Group("Search tuning", 8, false) && Rows("lootTuning")) {
                auto& loot = draft.loot;
                Im::BeginDisabled(!loot.enabled);
                Slider("Notice within", loot.searchRadius, 300, 2500, "%.0f units",
                       "How far from a companion a find can be. They must also have a clear line of sight "
                       "to it.");
                Slider("Leave when you are beyond", loot.leaveDistance, 600,
                       std::max(600.0F, draft.releaseDistance - 100), "%.0f units",
                       "If you move this far from the find, the companion leaves it and rejoins you. Finds "
                       "must start within 80% of this distance from you.");
                IntSlider("Valuable above", loot.valuableGold, 100, 10000, "%d gold",
                          "If any single item in the find is worth more than this, the icon turns gold. "
                          "Piles of septims count as a whole.");
                Slider("Search for", loot.searchSeconds, 5, 30, "%.0f s",
                       "How long the finder kneels at the find before standing up to wait beside it. "
                       "Opening the find or moving away ends it sooner.");
                Slider("Search again after moving", loot.areaDistance, 800, 6000, "%.0f units",
                       "After the finder reaches one find, they stop looking until you are this far from it, "
                       "or enter another interior or worldspace.");
                Im::EndDisabled();
                Im::EndTable();
            }
            if (Im::SmallButton("Restore loot tuning")) {
                const Loot::Tuning defaults;
                draft.loot.searchRadius = defaults.searchRadius;
                draft.loot.leaveDistance = defaults.leaveDistance;
                draft.loot.valuableGold = defaults.valuableGold;
                draft.loot.searchSeconds = defaults.searchSeconds;
                draft.loot.areaDistance = defaults.areaDistance;
                dirty = true;
            }
            if (Group("Pathfinder", pathfinderIcon)) {
                auto& path = draft.pathfinder;
                dirty |= Im::Checkbox("Point out new places", &path.enabled);
                Help("In Natural, Lead, Companion and Custom, a companion who spots a place you have never found "
                     "runs out toward it, turns to wave at you and points it out, then leads the way. Outdoors "
                     "only, never while fighting, sneaking or riding.");
                Im::BeginDisabled(!path.enabled);
                if (Rows("pathfinderWho")) {
                    Im::TableNextColumn();
                    Im::TextUnformatted("Pathfinder");
                    Im::TableNextColumn();
                    CompanionPicker("##pathfinder", view, path.companion, path.companionName,
                                    "Any companion, curious first");
                    Help("Who points places out. Any companion lets whoever can see it go, a curious one first.");
                    Im::EndTable();
                }
                if (Checks("pathfinderOptions")) {
                    Check("Wave first", path.wave, "Turn to you and wave before pointing, to catch your eye.");
                    Check("Name what they saw", path.notice,
                          "Show a notice naming what they saw and which way it lies.");
                    Check("Lead the way", path.lead,
                          "After pointing it out, keep going toward it. Whenever they get far enough ahead they "
                          "stop and wave you on, and walk on once you come close. Go another way, or find it, "
                          "and they come back.");
                    Im::EndTable();
                }
                Im::EndDisabled();
            }
            if (Group("Pathfinder tuning", pathfinderIcon, false) && Rows("pathfinderTuning")) {
                auto& path = draft.pathfinder;
                Im::BeginDisabled(!path.enabled);
                Slider("Notice places within", path.range, 3000, 12000, "%.0f units",
                       "How far away an undiscovered place can be spotted. Places closer than 1500 units are "
                       "already on your compass.");
                Slider("Stop ahead of you at", path.run, 400, 3000, "%.0f units",
                       "How far ahead of you, straight toward the place, the pathfinder gets before stopping "
                       "to wave you on. They stop 450 units short of the place itself.");
                Im::BeginDisabled(!path.lead);
                Slider("Walk on when you're within", path.resume, 200, 1200, "%.0f units",
                       "How close you come before they lead on. Never more than half the stopping distance.");
                Im::EndDisabled();
                Slider("Between sightings", path.cooldown, 20, 300, "%.0f s",
                       "The shortest time between two places being pointed out.");
                Im::EndDisabled();
                Im::EndTable();
            }
            if (Im::SmallButton("Restore pathfinder tuning")) {
                const Pathfinder::Tuning defaults;
                draft.pathfinder.range = defaults.range;
                draft.pathfinder.run = defaults.run;
                draft.pathfinder.resume = defaults.resume;
                draft.pathfinder.cooldown = defaults.cooldown;
                dirty = true;
            }
            Footer();
        }
        void __stdcall RenderInterface()
        {
            const auto view = Read();
            Prepare(view);
            SettingsPage page("WalkWithMeInterface");
            Header(view, "INTERFACE");
            if (Group("Follower icons", 7) && Checks("followerIcons", 3)) {
                Check(
                    "Show chat icons", draft.restDialogueIcons,
                    "Show an ivory speech bubble above resting companions with unheard conversation options. "
                    "Filters follower commands, trading, services and random chatter.");
                Im::BeginDisabled(!draft.restDialogueIcons);
                Check("Animate chat icons", draft.animateChatIcons,
                      "Pop up and gently float chat icons above companions. Off keeps a still, slightly "
                      "transparent icon.");
                Im::EndDisabled();
                Check("Show loot icons", draft.loot.icons,
                      "An animated chest marks the loot finder: ivory for ordinary loot, gold for valuable "
                      "items. "
                      "Requires loot scouting on the Loot page.");
                Check("Show pathfinder icons", draft.pathfinder.icon,
                      "The Pathfinder emblem floats above a companion from the moment they spot a new place "
                      "until they have pointed it out, or until they stop. Requires Pathfinder on the Roles "
                      "page.");
                Im::EndTable();
            }
            if (Group("Order emblem", 0)) {
                if (Checks("hudOptions")) {
                    Check("Show emblem", draft.showOrderHUD,
                          "Shows the current order on the right side of the screen.");
                    Im::BeginDisabled(!draft.showOrderHUD);
                    Check("Soft HUD backing", draft.hudPanel,
                          "Subtle dark backing behind the order. Disable for bare icon and text.");
                    Im::EndDisabled();
                    Im::EndTable();
                }
                if (Rows("hudSize")) {
                    Im::BeginDisabled(!draft.showOrderHUD);
                    Slider("Size", draft.hudScale, .55F, 1.3F, "%.2fx", "Scale the small order emblem.");
                    Slider("Height", draft.hudVertical, .1F, .85F, "%.2f", "Distance down the right edge.");
                    Im::EndDisabled();
                    Im::EndTable();
                }
                hudPreviewAt = GetTickCount64();
                hudPreviewEnabled = draft.showOrderHUD;
                hudPreviewScale = draft.hudScale;
                hudPreviewHeight = draft.hudVertical;
                hudPreviewPanel = draft.hudPanel;
                Note("Changes take effect and save automatically.");
            }
            if (Group("Troubleshooting", 6, false)) {
                if (Checks("debug")) {
                    Check("Eligibility log", draft.logDiagnostics, "Record why a companion is released.");
                    Check("Movement log", draft.traceMovement, "Record movement samples once per second.");
                    Im::EndTable();
                }
            }
            Footer();
        }
        void __stdcall RenderControls()
        {
            const auto view = Read();
            Prepare(view);
            SettingsPage page("WalkWithMeControls");
            Header(view, "CONTROLS");
            Label("KEY BINDINGS", 6);
            Note("Select a binding, then hold Shift, Ctrl or Alt and press a key. Modifiers can be combined; "
                 "either side works. Esc cancels; Delete clears.");
            Note("Use the exact combination: G, Ctrl + G and Shift + G can each have their own action.");
            Space();
            if (Im::BeginTable("bindings", 3, Im::ImGuiTableFlags_SizingStretchProp)) {
                Im::TableSetupColumn("Action", Im::ImGuiTableColumnFlags_WidthStretch, 1.4F);
                Im::TableSetupColumn("Key", Im::ImGuiTableColumnFlags_WidthStretch, 1.0F);
                Im::TableSetupColumn("", Im::ImGuiTableColumnFlags_WidthFixed, E(3.5F));
                for (int i = 0; i < static_cast<int>(hotkeyBindings.size()); ++i) {
                    Im::PushID(i);
                    Im::TableNextRow();
                    Im::TableNextColumn();
                    Im::TextUnformatted(hotkeyBindings[i].label);
                    Im::TableNextColumn();
                    const bool waiting = capturing.load() == i;
                    const auto chord = hotkeyBindings[i].Get(draft);
                    const auto name = waiting ? "Press a key or chord..."
                                              : Hotkeys::ModifierPrefix(chord.modifiers) + KeyName(chord.key);
                    if (Im::Button(name.c_str(), {-1, 0})) {
                        if (waiting) {
                            CancelCapture();
                        }
                        else {
                            StartCapture(i);
                        }
                    }
                    Im::TableNextColumn();
                    if (Im::Button("Clear", {-1, 0})) {
                        hotkeyBindings[i].Set(draft, {});
                        dirty = true;
                        CancelCapture();
                    }
                    Im::PopID();
                }
                Im::EndTable();
            }
            if (Group("Wheel & signals", 0)) {
                if (Rows("signals")) {
                    Im::TableNextRow();
                    Im::TableNextColumn();
                    Im::AlignTextToFramePadding();
                    Im::TextUnformatted("Gamepad wheel");
                    Im::TableNextColumn();
                    Im::SetNextItemWidth(-1);
                    dirty |= Im::Combo("##gamepadWheel", &draft.gamepadBinding,
                                       "Unbound\0LB + Right-stick click\0LB + Y\0LB + D-pad up\0");
                    Help("Hold LB and press the chosen button. Right stick aims; A confirms; B cancels.");
                    Im::EndTable();
                }
                if (Checks("gestureOptions")) {
                    Check("Command hand signal", draft.commandGestures,
                          "Third-person Lead, Companion and Rear gestures need OAR and generated GP Offset "
                          "Movement "
                          "Animation behaviors. First-person signals use installed First Person Interactions "
                          "clips.");
                    Im::BeginDisabled(!draft.commandGestures);
                    Check("Glance toward followers", draft.gestureAwareness,
                          "Briefly turn toward the party during a signal. Third person only.");
                    Check("Point for Lead", draft.pointingSignal,
                          "Use the bundled left-hand point in third person. First person keeps the installed "
                          "FPI forward reach.");
                    Check(
                        "Invitation for Companion", draft.companionSignal,
                        "Use the bundled two-arm invitation in third person. First person uses the installed "
                        "FPI wave.");
                    Im::EndDisabled();
                    Im::EndTable();
                }
            }
            if (Im::Button("Open party orders") && commandWindow) {
                SetWheelOpen(true);
            }
            Help("Commands pause in game menus. Avoid keys used by other mods or Skyrim quicksave / "
                 "quickload.");
            Footer();
        }

        // PARTY_HEADER_BEGIN: shared with the offline visual proof.
        constexpr auto yellow = IM_COL32(240, 199, 74, 255);
        Im::ImU32 Faded(Im::ImU32 color, float alpha)
        {
            const auto a = static_cast<Im::ImU32>(((color >> 24) & 255) * std::clamp(alpha, 0.0F, 1.0F));
            return (color & 0x00FFFFFFu) | (a << 24);
        }
        Im::ImU32 Blend(Im::ImU32 from, Im::ImU32 to, float t)
        {
            t = std::clamp(t, 0.0F, 1.0F);
            Im::ImU32 result = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                const float a = static_cast<float>((from >> shift) & 255), b = static_cast<float>((to >> shift) & 255);
                result |= static_cast<Im::ImU32>(a + (b - a) * t + .5F) << shift;
            }
            return result;
        }
        void CenteredText(Im::ImDrawList* draw, const char* value, V at, float size, Im::ImU32 color, float width)
        {
            const auto bounds = Im::CalcTextSize(value);
            size = std::min(size, width * Im::GetFontSize() / std::max(bounds.x, 1.0F));
            Draw::AddText(draw, Im::GetFont(), size, {at.x - bounds.x * size / Im::GetFontSize() * .5F, at.y}, color,
                          value);
        }
        // The title stays put while the pages move; the subtitle says which page is showing.
        void PartyHeader(Im::ImDrawList* draw, V origin, float w, float fade, float page)
        {
            const float x = origin.x + w * .5F;
            CenteredText(draw, "W A L K   W I T H   M E", {x, origin.y + w * .005F}, w * .025F, Faded(gold, fade),
                         w * .8F);
            CenteredText(draw, "P A R T Y   O R D E R S", {x, origin.y + w * .042F}, w * .017F,
                         Faded(muted, fade * (1 - page)), w * .8F);
            CenteredText(draw, "P A R T Y   A C T I O N S", {x, origin.y + w * .042F}, w * .017F,
                         Faded(yellow, fade * page), w * .8F);
            const float y = origin.y + w * .018F;
            Draw::AddLine(draw, {x - w * .29F, y}, {x - w * .20F, y}, IM_COL32(180, 163, 127, 65 * fade), 1);
            Draw::AddLine(draw, {x + w * .20F, y}, {x + w * .29F, y}, IM_COL32(180, 163, 127, 65 * fade), 1);
        }
        // PARTY_HEADER_END
        // ACTION_GRID_BEGIN: shared with the offline visual proof.
        struct GridTile
        {
            const char* label{};
            std::string caption;
            bool enabled{true}, toggle{}, on{};
        };
        // A translucent bar pointing at the other page; its chevrons drift the way it leads.
        bool PageBar(const char* id, V at, V size, bool down, const char* label, float visibility, float elapsed,
                     float& hover)
        {
            Im::SetCursorScreenPos(at);
            const bool clicked = Im::InvisibleButton(id, size);
            const bool over = Im::IsItemHovered(), held = Im::IsItemActive();
            hover = Wheel::Follow(hover, over ? 1.0F : 0.0F, std::min(Im::GetIO()->DeltaTime, .05F), 12);
            auto* draw = Im::GetWindowDrawList();
            const float a = std::clamp(visibility, 0.0F, 1.0F);
            at.y += size.y * (held ? .04F : 0);
            const V end{at.x + size.x, at.y + size.y}, mid{at.x + size.x * .5F, at.y + size.y * .5F};
            const float r = size.y * .5F;
            Draw::AddRectFilled(draw, at, end, IM_COL32(240, 199, 74, (9 + 17 * hover) * a), r, 0);
            Draw::AddRect(draw, at, end, IM_COL32(240, 199, 74, (30 + 75 * hover) * a), r, 0, 1);
            // Two fine rules run out from the label, like the ones beside the title.
            const float labelSize = size.y * .27F;
            const float labelWidth = Im::CalcTextSize(label).x * labelSize / Im::GetFontSize();
            CenteredText(draw, label, {mid.x, mid.y - labelSize * .55F}, labelSize,
                         Faded(Blend(IM_COL32(214, 190, 120, 255), yellow, hover), a), size.x * .5F);
            const float dir = down ? 1.0F : -1.0F;
            const float drift = std::sin(elapsed * 3.1F) * size.y * .055F * dir;
            const float hw = size.y * .14F, hh = size.y * .075F;
            for (int side = -1; side <= 1; side += 2) {
                const float cx = mid.x + side * (labelWidth * .5F + size.y * .62F);
                const float cy = mid.y + drift;
                const auto color = IM_COL32(240, 199, 74, (150 + 105 * hover) * a);
                Draw::AddLine(draw, {cx - hw, cy - hh * dir}, {cx, cy + hh * dir}, color, 2);
                Draw::AddLine(draw, {cx, cy + hh * dir}, {cx + hw, cy - hh * dir}, color, 2);
                const float rule0 = cx + side * size.y * .40F, rule1 = mid.x + side * size.x * .40F;
                Draw::AddLine(draw, {rule0, mid.y}, {rule1, mid.y}, IM_COL32(240, 199, 74, (22 + 40 * hover) * a), 1);
            }
            return clicked;
        }
        // The party actions page: six tiles that rise in one after another, a yellow selection frame
        // that glides between them, and a line under the grid explaining the focused one. Returns the
        // tile clicked this frame, or -1; `hovered` is the tile under the mouse.
        int ActionGridPage(V origin, float w, const std::array<GridTile, ActionGrid::count>& tiles, int focus,
                           const ActionGrid::Motion& motion, float visibility, bool interactive, int& hovered)
        {
            static constexpr const char* details[ActionGrid::count]{
                "Bring every travelling companion to clear ground beside you, even from another cell.",
                "When you stop, the party settles in nearby. Off: they wait on their feet.",
                "Take or return control of the companion you were looking at.",
                "Switch party guidance on or off. Off hands everyone back to their own AI.",
                "Walk hand in hand with your chosen partner in Companion mode.",
                "Your finder points out chests, bodies and valuables worth a look.",
                "Your pathfinder points out places you have never found, in Natural, Lead and Companion."};
            auto* draw = Im::GetWindowDrawList();
            const float lift = w * .05F * (1 - motion.page);
            const float a0 = std::clamp(visibility, 0.0F, 1.0F);
            // One label size for every tile: the largest at which every label fits, a two-word name
            // breaking onto two lines rather than shrinking.
            std::array<std::array<std::string, 2>, ActionGrid::count> lines;
            float labelSize = ActionGrid::TileBox(0, w).h * .10F;
            {
                Inscription font;
                const float room = ActionGrid::TileBox(0, w).w * .86F;
                auto width = [](const std::string& text) { return Im::CalcTextSize(text.c_str()).x; };
                for (int i = 0; i < ActionGrid::count; ++i) {
                    const std::string label = tiles[i].label ? tiles[i].label : "";
                    lines[i] = {label, {}};
                    const auto space = label.find(' ');
                    if (space != std::string::npos && width(label) * labelSize / Im::GetFontSize() > room) {
                        lines[i] = {label.substr(0, space), label.substr(space + 1)};
                    }
                    for (const auto& line : lines[i]) {
                        labelSize = std::min(labelSize, room * Im::GetFontSize() / std::max(width(line), 1.0F));
                    }
                }
            }
            int clicked = -1;
            hovered = -1;
            for (int i = 0; i < ActionGrid::count; ++i) {
                const auto& tile = tiles[i];
                const auto box = ActionGrid::TileBox(i, w);
                if (interactive) {
                    // The hit rectangle stays still while the artwork settles.
                    Im::SetCursorScreenPos({origin.x + box.x, origin.y + box.y});
                    Im::PushID(i);
                    const bool click = Im::InvisibleButton("tile", {box.w, box.h});
                    if (Im::IsItemHovered(Im::ImGuiHoveredFlags_AllowWhenDisabled)) {
                        hovered = i;
                    }
                    Im::PopID();
                    if (click && tile.enabled) {
                        clicked = i;
                    }
                }
                const float reveal = motion.Reveal(i);
                const float h = motion.highlight[i], f = motion.flash[i], on = motion.toggle[i];
                const float a = a0 * reveal * (tile.enabled ? 1.0F : .42F);
                if (a <= .003F) {
                    continue;
                }
                const V at{origin.x + box.x, origin.y + box.y + lift + box.h * (.12F * (1 - reveal) - .03F * h)};
                const V end{at.x + box.w, at.y + box.h};
                const float r = box.h * .075F;
                Draw::AddRectFilled(draw, {at.x, at.y + box.h * .03F}, {end.x, end.y + box.h * .03F},
                                    IM_COL32(0, 0, 0, (50 + 30 * h) * a), r, 0);
                const auto fill = Blend(IM_COL32(24, 31, 35, 238), IM_COL32(55, 49, 26, 246), h * .9F + f * .3F);
                Draw::AddRectFilled(draw, at, end, Faded(fill, a), r, 0);
                // A warm pool of light behind the icon on the focused tile.
                const V icon{at.x + box.w * .5F, at.y + box.h * .37F};
                for (int ring = 3; ring > 0; --ring) {
                    Draw::AddCircleFilled(draw, icon, box.h * (.12F + .07F * ring),
                                          IM_COL32(240, 199, 74, (7 + 10 * f) * h * a), 40);
                }
                Draw::AddRect(draw, at, end, Faded(Blend(IM_COL32(168, 153, 118, 50), IM_COL32(240, 199, 74, 150), h), a),
                              r, 0, 1);
                const float reach = box.w * (.10F + .30F * h);
                Draw::AddLine(draw, {icon.x - reach, at.y}, {icon.x + reach, at.y},
                              IM_COL32(246, 214, 120, (40 + 150 * h) * a), 1);
                const float t = i == focus ? motion.focusElapsed : 3.0F;
                const float iconSize = box.h * (.40F + .03F * h);
                const auto tint = Faded(Blend(IM_COL32(226, 218, 201, 255), IM_COL32(248, 214, 110, 255), h), a);
                switch (i) {
                case ActionGrid::Teleport:
                    OrderVisuals::Icon(draw, icon, iconSize, 8, t, a, true, tint | 0xFF000000u);
                    break;
                case ActionGrid::TemporaryRest:
                    OrderVisuals::Icon(draw, icon, iconSize, 4, t, a * (.6F + .4F * on), true, tint | 0xFF000000u);
                    break;
                case ActionGrid::LootScout:
                    OrderVisuals::Loot(draw, {icon.x, icon.y - box.h * .02F * std::sin(std::min(t, .5F) * 6.2832F)},
                                       iconSize * .74F, h > .5F, a * (.55F + .35F * on));
                    break;
                case ActionGrid::CompanionControl:
                    OrderVisuals::DrawGlyph(draw, icon, iconSize * .92F, OrderVisuals::Glyph::Target, t, tint);
                    break;
                case ActionGrid::HoldHands:
                    OrderVisuals::DrawGlyph(draw, icon, iconSize, OrderVisuals::Glyph::Link, t,
                                            Faded(tint, .6F + .4F * on));
                    break;
                case ActionGrid::Pathfinder:
                    OrderVisuals::Icon(draw, icon, iconSize, 9, t, a * (.6F + .4F * on), true, tint | 0xFF000000u);
                    break;
                default:
                    OrderVisuals::DrawGlyph(draw, icon, iconSize * .92F, OrderVisuals::Glyph::Power, t,
                                            Faded(tint, .6F + .4F * on));
                    break;
                }
                {
                    Inscription font;
                    const bool two = !lines[i][1].empty();
                    const auto labelColor =
                        Faded(Blend(IM_COL32(222, 215, 197, 255), IM_COL32(246, 234, 204, 255), h), a);
                    CenteredText(draw, lines[i][0].c_str(), {icon.x, at.y + box.h * (two ? .585F : .63F)}, labelSize,
                                 labelColor, box.w * .9F);
                    if (two) {
                        CenteredText(draw, lines[i][1].c_str(), {icon.x, at.y + box.h * .585F + labelSize * 1.05F},
                                     labelSize, labelColor, box.w * .9F);
                    }
                }
                CenteredText(draw, tile.caption.c_str(), {icon.x, at.y + box.h * .835F}, box.h * .066F,
                             Faded(Blend(IM_COL32(150, 160, 157, 245), IM_COL32(214, 196, 142, 255), h * .6F), a),
                             box.w * .88F);
                if (tile.toggle) {
                    // A small switch in the corner: yellow and to the right when on.
                    const float tw = box.w * .17F, th = box.h * .085F;
                    const V s{end.x - box.w * .065F - tw, at.y + box.h * .075F};
                    Draw::AddRectFilled(draw, s, {s.x + tw, s.y + th},
                                        Faded(Blend(IM_COL32(54, 62, 63, 255), IM_COL32(224, 182, 62, 255), on), a),
                                        th * .5F, 0);
                    Draw::AddCircleFilled(draw, {s.x + th * .5F + (tw - th) * on, s.y + th * .5F}, th * .36F,
                                          Faded(Blend(IM_COL32(182, 186, 178, 255), IM_COL32(40, 33, 14, 255), on), a),
                                          20);
                }
            }
            // The selection frame: yellow corner brackets gliding between tiles.
            const auto first = ActionGrid::TileBox(0, w);
            const float frameReveal = motion.Reveal(std::clamp(focus, 0, ActionGrid::count - 1));
            const float fa = a0 * frameReveal * (tiles[std::clamp(focus, 0, ActionGrid::count - 1)].enabled ? 1.0F : .55F);
            if (fa > .003F) {
                const float pad = w * .009F, len = first.h * .13F;
                const V p0{origin.x + w * motion.frameX - pad,
                           origin.y + w * motion.frameY + lift - first.h * .03F - pad};
                const V p1{p0.x + first.w + 2 * pad, p0.y + first.h + 2 * pad};
                const auto color = IM_COL32(246, 206, 86, 235 * fa);
                const float thick = std::max(2.0F, w * .0035F);
                for (int corner = 0; corner < 4; ++corner) {
                    const float cx = corner & 1 ? p1.x : p0.x, cy = corner & 2 ? p1.y : p0.y;
                    const float sx = corner & 1 ? -1.0F : 1.0F, sy = corner & 2 ? -1.0F : 1.0F;
                    Draw::AddLine(draw, {cx, cy}, {cx + sx * len, cy}, color, thick);
                    Draw::AddLine(draw, {cx, cy}, {cx, cy + sy * len}, color, thick);
                }
            }
            if (focus >= 0 && focus < ActionGrid::count) {
                CenteredText(draw, details[focus], {origin.x + w * .5F, origin.y + w * 1.0F + lift}, w * .02F,
                             IM_COL32(186, 184, 172, 255 * a0), w * .88F);
            }
            return clicked;
        }
        // ACTION_GRID_END
        void ToggleGuidance(FormationController& c)
        {
            auto value = c.Snapshot().settings;
            value.enabled = !c.IsEnabled();
            if (c.ApplyPreferences(value)) {
                Engine::Notify(value.enabled ? "Walk With Me enabled" : "Walk With Me disabled");
            }
            else {
                Engine::Notify("Walk With Me could not save the enable setting");
            }
        }
        void ToggleHandHolding(FormationController& c)
        {
            auto value = c.Snapshot().settings;
            auto& hands = value.handHolding;
            if (!hands.enabled && hands.partner.empty()) {
                Engine::Notify("Choose a hand-holding companion in Travel settings first");
                return;
            }
            hands.enabled = !hands.enabled;
            if (!c.ApplyPreferences(value)) {
                Engine::Notify("Walk With Me could not save the hand-holding setting");
            }
            else if (!hands.enabled) {
                Engine::Notify("Hand holding off");
            }
            else {
                Engine::Notify(c.Snapshot().mode == FormationMode::kCompanion
                                   ? "Hand holding on"
                                   : "Hand holding on; it starts in Companion mode");
            }
        }
        void ToggleLootScouting(FormationController& c)
        {
            auto value = c.Snapshot().settings;
            value.loot.enabled = !value.loot.enabled;
            if (!c.ApplyPreferences(value)) {
                Engine::Notify("Walk With Me could not save the loot scouting setting");
            }
            else {
                Engine::Notify(value.loot.enabled ? "Loot scouting on" : "Loot scouting off");
            }
        }
        void TogglePathfinder(FormationController& c)
        {
            auto value = c.Snapshot().settings;
            value.pathfinder.enabled = !value.pathfinder.enabled;
            if (!c.ApplyPreferences(value)) {
                Engine::Notify("Walk With Me could not save the pathfinder setting");
            }
            else {
                Engine::Notify(value.pathfinder.enabled ? "Pathfinder on" : "Pathfinder off");
            }
        }
        void __stdcall RenderCommands()
        {
            if (!commandWindow || !commandWindow->IsOpen) {
                return;
            }
            const auto view = Read();
            Theme theme;
            static float elapsed = 0, centerElapsed = 0;
            static Wheel::Motion motion;
            static int focus = 0, lastFocus = -1, pressed = -1;
            static Wheel::Stick stick;
            static bool gridPage = false;
            static int tileFocus = 0;
            static ActionGrid::Motion grid;
            static ActionGrid::StickSteps gridStick;
            static bool wheelStickLocked = false; // Until the stick recentres after leaving the grid.
            static std::array<float, 2> barHover{};
            const auto display = Im::GetIO()->DisplaySize;
            const float w = Wheel::Width(display.x, display.y);
            const float dt = std::min(Im::GetIO()->DeltaTime, .05F);
            const auto& hands = view.settings.handHolding;
            const auto& loot = view.settings.loot;
            const std::array<bool, ActionGrid::count> on{false,         view.settings.sandbox.automatic,
                                                         false,         view.enabled,
                                                         hands.enabled, loot.enabled,
                                                         view.settings.pathfinder.enabled};
            Im::SetNextWindowPos({display.x * .5F, display.y * .5F}, Im::ImGuiCond_Always, {.5F, .5F});
            Im::SetNextWindowSize({w, w * Wheel::windowHeight}, Im::ImGuiCond_Always);
            bool open = true;
            Im::PushStyleVar(Im::ImGuiStyleVar_WindowBorderSize, 0.0F);
            Im::PushStyleColor(Im::ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
            if (Im::Begin("WalkWithMeOrders", &open,
                          Im::ImGuiWindowFlags_NoTitleBar | Im::ImGuiWindowFlags_NoResize |
                              Im::ImGuiWindowFlags_NoMove | Im::ImGuiWindowFlags_NoScrollbar |
                              Im::ImGuiWindowFlags_NoScrollWithMouse | Im::ImGuiWindowFlags_NoSavedSettings |
                              Im::ImGuiWindowFlags_NoNav)) {
                if (Im::IsWindowAppearing()) {
                    elapsed = 0;
                    focus = std::clamp(static_cast<int>(view.mode), 0, Wheel::count - 1);
                    lastFocus = -1;
                    pressed = -1;
                    stick = {};
                    padConfirm = false;
                    padCancel = false;
                    padDx = 0;
                    padDy = 0;
                    gridBack = false;
                    motion.Open(focus);
                    wheelPackageActor = view.crosshairActor;
                    packageShortcut = false;
                    gridPage = false;
                    tileFocus = 0;
                    grid.Reset(tileFocus, false, on);
                    gridStick = {};
                    wheelStickLocked = false;
                    barHover = {};
                }
                if (wheelCloseRequested.load()) {
                    motion.Close();
                }
                elapsed += dt;
                centerElapsed += dt;
                const auto origin = Im::GetWindowPos();
                auto* draw = Im::GetWindowDrawList();
                const V wheelCenter{origin.x + w * .5F, origin.y + w * .48F};
                const float hitRadius = w * .40F;
                auto* backdrop = Im::GetBackgroundDrawList();
                const auto mouse = Im::GetIO()->MousePos;
                const bool mouseMoved =
                    std::abs(Im::GetIO()->MouseDelta.x) + std::abs(Im::GetIO()->MouseDelta.y) > .5F;
                Im::BeginDisabled(motion.closing);

                // Page changes: D-pad or arrows, Tab, the mouse wheel, or the bars themselves.
                const int dx = padDx.exchange(0), dy = padDy.exchange(0);
                const bool tab = Im::IsKeyPressed(Im::ImGuiKey_Tab, false);
                const float scroll = Im::GetIO()->MouseWheel;
                const bool back = padCancel.exchange(false) || Im::IsKeyPressed(Im::ImGuiKey_Escape, false) ||
                                  gridBack.exchange(false);
                bool confirm = padConfirm.exchange(false) || Im::IsKeyPressed(Im::ImGuiKey_Enter, false);
                bool toGrid = false, toWheel = false;
                int choice = -1, tileChoice = -1;
                if (!gridPage) {
                    int over = Wheel::Mouse(mouse.x - wheelCenter.x, mouse.y - wheelCenter.y, hitRadius);
                    Im::SetCursorScreenPos({origin.x, origin.y + w * .04F});
                    const bool clicked = Im::InvisibleButton("wheel-hit", {w, w * .87F});
                    const bool activated = Im::IsItemActivated();
                    if (!Im::IsItemHovered()) {
                        over = -1;
                    }
                    if (activated) {
                        pressed = over;
                    }
                    choice = clicked && pressed >= 0 && pressed == over ? over : -1;
                    if (!Im::IsItemActive()) {
                        pressed = -1;
                    }
                    int step = dx;
                    if (Im::IsKeyPressed(Im::ImGuiKey_RightArrow)) {
                        ++step;
                    }
                    if (Im::IsKeyPressed(Im::ImGuiKey_LeftArrow)) {
                        --step;
                    }
                    if (step) {
                        focus = Wheel::clockwise[(Wheel::slots[focus] + (step > 0 ? 1 : Wheel::count - 1)) %
                                                 Wheel::count];
                    }
                    const bool stickLive = padStickAt.load() && GetTickCount64() - padStickAt.load() < 350;
                    if (stickLive && wheelStickLocked && std::hypot(padX.load(), padY.load()) < .22F) {
                        wheelStickLocked = false;
                    }
                    if (stickLive && !wheelStickLocked) {
                        focus = stick.Update(padX.load(), padY.load(), focus);
                    }
                    else {
                        stick.engaged = false;
                    }
                    if (over >= 0 && (mouseMoved || activated)) {
                        focus = over;
                    }
                    if (confirm) {
                        choice = focus;
                    }
                    for (int i = 0; i < Wheel::count; ++i) {
                        if (Im::IsKeyPressed(static_cast<Im::ImGuiKey>(Im::ImGuiKey_1 + i), false)) {
                            choice = i;
                        }
                    }
                    toGrid = dy > 0 || Im::IsKeyPressed(Im::ImGuiKey_DownArrow) || tab || scroll < 0;
                    if (back) {
                        open = false;
                    }
                }
                else {
                    int mx = dx, my = dy;
                    mx += Im::IsKeyPressed(Im::ImGuiKey_RightArrow) - Im::IsKeyPressed(Im::ImGuiKey_LeftArrow);
                    my += Im::IsKeyPressed(Im::ImGuiKey_DownArrow) - Im::IsKeyPressed(Im::ImGuiKey_UpArrow);
                    // The right stick steps through the tiles like the D-pad.
                    const bool stickLive = padStickAt.load() && GetTickCount64() - padStickAt.load() < 350;
                    const auto push = gridStick.Update(stickLive ? padX.load() : 0, stickLive ? padY.load() : 0, dt);
                    mx += push[0];
                    my += push[1];
                    if (mx || my) {
                        const int next = ActionGrid::Move(tileFocus, mx, my);
                        if (next < 0) {
                            toWheel = true;
                        }
                        else {
                            tileFocus = next;
                        }
                    }
                    if (confirm) {
                        tileChoice = tileFocus;
                    }
                    for (int i = 0; i < ActionGrid::count; ++i) {
                        if (Im::IsKeyPressed(static_cast<Im::ImGuiKey>(Im::ImGuiKey_1 + i), false)) {
                            tileChoice = i;
                        }
                    }
                    toWheel = toWheel || back || tab || scroll > 0;
                }
                if (focus != lastFocus) {
                    lastFocus = focus;
                    centerElapsed = 0;
                }

                // Tiles, as they stand this frame.
                std::array<GridTile, ActionGrid::count> tiles;
                const std::string recallCaption = view.recallPending ? "Teleport queued"
                                                  : !view.recallReason.empty()
                                                      ? view.recallReason
                                                      : fmt::format("{} companion{} ready", view.recallCount,
                                                                    view.recallCount == 1 ? "" : "s");
                const bool saving = toggleSaving.load();
                tiles[ActionGrid::Teleport] = {"Teleport party", recallCaption,
                                               view.inGame && view.recallReason.empty() && !view.recallPending};
                tiles[ActionGrid::TemporaryRest] = {"Temporary rest",
                                                    saving           ? "Saving..."
                                                    : on[ActionGrid::TemporaryRest] ? "Settle when you stop"
                                                                                     : "Keep to your feet",
                                                    view.inGame && !saving, true, on[ActionGrid::TemporaryRest]};
                std::string target;
                for (const auto& row : view.packageTargets) {
                    if (row.id == wheelPackageActor) {
                        target = row.name;
                    }
                }
                tiles[ActionGrid::CompanionControl] = {"Companion control",
                                                       target.empty() ? "Look at a companion" : target,
                                                       view.inGame && !target.empty()};
                tiles[ActionGrid::Guidance] = {"Walk With Me", on[ActionGrid::Guidance] ? "Guiding" : "Follower AI",
                                               view.inGame && !saving, true, on[ActionGrid::Guidance]};
                const std::string partner = hands.partnerName.empty() ? "your partner" : hands.partnerName;
                tiles[ActionGrid::HoldHands] = {
                    "Hold hands",
                    hands.partner.empty()                    ? "Choose a partner"
                    : view.mode == FormationMode::kCompanion ? "With " + partner
                                                             : "In Companion",
                    view.inGame && !saving && (!hands.partner.empty() || hands.enabled), true, on[ActionGrid::HoldHands]};
                tiles[ActionGrid::LootScout] = {"Loot scouting",
                                                loot.finderName.empty() ? "Any companion" : loot.finderName,
                                                view.inGame && !saving, true, on[ActionGrid::LootScout]};
                const auto& path = view.settings.pathfinder;
                const bool pathStyle = view.mode == FormationMode::kDynamic || view.mode == FormationMode::kLead ||
                                       view.mode == FormationMode::kCustom ||
                                       view.mode == FormationMode::kCompanion;
                tiles[ActionGrid::Pathfinder] = {"Pathfinder",
                                                 !pathStyle                  ? "Not in this style"
                                                 : path.companionName.empty() ? "Curious first"
                                                                              : path.companionName,
                                                 view.inGame && !saving, true, on[ActionGrid::Pathfinder]};

                if (toGrid && !motion.closing) {
                    gridPage = true;
                    gridStick.Lock();
                }
                else if (toWheel && !motion.closing) {
                    gridPage = false;
                    wheelStickLocked = true;
                    stick.engaged = false;
                }
                gridBackEnabled = gridPage;
                motion.Update(dt, focus);
                grid.Update(dt, gridPage, tileFocus, on);
                const float windowFade = motion.visibility;
                const float page = grid.page;
                Draw::AddRectFilled(backdrop, {0, 0}, display, IM_COL32(7, 12, 16, 165 * windowFade), 0, 0);
                PartyHeader(draw, origin, w, windowFade, page);

                const bool customReady = view.settings.customFormation.Defined();
                const auto orderNames = OrderNames(view);
                if (grid.WheelVisibility() > .001F) {
                    // The wheel lifts away as the grid rises in.
                    const float fade = motion.visibility * grid.WheelVisibility();
                    const float radius = hitRadius * motion.Scale();
                    const V center{wheelCenter.x, wheelCenter.y - w * .05F * page};
                    // WHEEL_DRAW_BEGIN: shared with the offline visual proof.
                    auto text = [&](const char* value, V at, float size, Im::ImU32 color, float width) {
                        CenteredText(draw, value, at, size, Faded(color, fade), width);
                    };
                    for (int layer = 4; layer > 0; --layer) {
                        Draw::AddCircleFilled(draw, center, radius + layer * w * .008F,
                                              IM_COL32(0, 0, 0, 8 * fade), 128);
                    }
                    Draw::AddCircle(draw, center, radius * 1.023F, IM_COL32(170, 153, 115, 45 * fade), 128, 1);
                    for (int mode = 0; mode < Wheel::count; ++mode) {
                        const float a = Wheel::Angle(mode);
                        const float light = motion.highlight[mode];
                        const float dim = mode == 6 && !customReady ? .42F : 1.0F;
                        const float reveal = Wheel::EaseOut((elapsed - Wheel::slots[mode] * .018F) / .25F);
                        const auto fill = IM_COL32(25 + 37 * light, 32 + 29 * light, 36 + 14 * light,
                                                   (230 + 15 * light) * fade * reveal);
                        auto point = [&](float angle, float r) {
                            return V{center.x + std::cos(angle) * r, center.y + std::sin(angle) * r};
                        };
                        const auto sliceCenter = point(a, w * .006F * light);
                        OrderVisuals::Sector(draw, sliceCenter, radius * (.975F + .025F * reveal), mode, fill);
                        Draw::PathArcTo(draw, sliceCenter, radius * .98F, a - Wheel::pi / 6 + .065F,
                                        a + Wheel::pi / 6 - .065F, 24);
                        Draw::PathStroke(draw, IM_COL32(218, 191, 133, (25 + 145 * light) * fade * reveal), 0,
                                         1 + light);
                        V at = point(a, radius * .755F);
                        at.y -= w * .018F;
                        at.y -= w * .004F * light;
                        OrderVisuals::Icon(draw, at, w * (.088F + .008F * light), mode, elapsed,
                                           fade * reveal * dim * (view.enabled ? .68F + .32F * light : .4F));
                        text(orderNames[mode], {at.x, at.y + w * .062F}, w * .025F,
                             IM_COL32(155 + 67 * light, 156 + 63 * light, 149 + 58 * light, 255 * reveal * dim),
                             w * .20F);
                        if (mode == static_cast<int>(view.mode)) {
                            Draw::AddCircleFilled(draw, point(a, radius * .555F), w * .003F,
                                                  IM_COL32(228, 203, 152, 220 * fade), 16);
                        }
                    }
                    Draw::AddCircle(draw, center, radius * .445F + 1, IM_COL32(20, 28, 29, 45 * fade), 96, 3);
                    Draw::AddCircleFilled(draw, center, radius * .445F, IM_COL32(20, 28, 29, 242 * fade), 96);
                    Draw::AddCircle(draw, center, radius * .445F, IM_COL32(166, 151, 114, 70 * fade), 64, 1);
                    Draw::PathArcTo(draw, center, radius * .462F, motion.angle - .30F, motion.angle + .30F, 28);
                    Draw::PathStroke(draw, IM_COL32(229, 200, 143, 220 * fade), 0, 2);
                    constexpr const char* captions[]{"Find your own pace",      "Take the road ahead",
                                                     "Stay by my side",         "Watch our backs",
                                                     "Make yourselves at home", "Return to follower AI",
                                                     "Walk as I drew it"};
                    for (int i = 0; i < Wheel::count; ++i) {
                        const float weight = motion.centerWeight[i];
                        if (weight < .005F) {
                            continue;
                        }
                        const float lift = w * .007F * (1 - weight);
                        const float dim = i == 6 && !customReady ? .42F : 1.0F;
                        OrderVisuals::Icon(draw, {center.x, center.y - w * .060F + lift}, w * .105F, i,
                                           i == focus ? centerElapsed : 3.0F, fade * weight * dim);
                        {
                            Inscription font;
                            text(orderNames[i], {center.x, center.y + w * .018F + lift}, w * .037F,
                                 IM_COL32(235, 225, 203, 255 * weight), w * .32F);
                        }
                        text(i == 6 && !customReady ? "Draw one in Travel settings" : captions[i],
                             {center.x, center.y + w * .070F + lift}, w * .020F,
                             IM_COL32(164, 168, 165, 255 * weight), w * .33F);
                    }
                    const std::string status =
                        !view.inGame ? "Available in game"
                        : !view.enabled
                            ? "Walk With Me is disabled"
                            : fmt::format(
                                  "ACTIVE ORDER  /  {}",
                                  orderNames[std::clamp(static_cast<int>(view.mode), 0, Wheel::count - 1)]);
                    text(status.c_str(), {center.x, origin.y + w * .955F - w * .05F * page}, w * .019F, gold, w * .8F);
                    // WHEEL_DRAW_END
                }
                if (!gridPage) {
                    const auto bar = ActionGrid::MoreBar(w);
                    if (PageBar("more", {origin.x + bar.x, origin.y + bar.y - w * .05F * page}, {bar.w, bar.h}, true,
                                "P A R T Y   A C T I O N S", windowFade * grid.WheelVisibility(), elapsed, barHover[0])) {
                        gridPage = true;
                    }
                }
                if (page > .005F || gridPage) {
                    int hovered = -1;
                    const int clicked = ActionGridPage(origin, w, tiles, tileFocus, grid, windowFade * grid.GridVisibility(), gridPage, hovered);
                    if (gridPage && hovered >= 0 && mouseMoved) {
                        tileFocus = hovered;
                    }
                    if (clicked >= 0) {
                        tileChoice = clicked;
                    }
                    if (gridPage) {
                        const auto bar = ActionGrid::BackBar(w);
                        if (PageBar("back", {origin.x + bar.x, origin.y + bar.y + w * .05F * (1 - page)},
                                    {bar.w, bar.h}, false, "P A R T Y   O R D E R S", windowFade * grid.GridVisibility(), elapsed,
                                    barHover[1])) {
                            gridPage = false;
                        }
                    }
                }

                if (!motion.closing && choice >= 0 && view.inGame && view.enabled) {
                    focus = choice;
                    if (OrderMode(choice) == FormationMode::kCustom && !customReady) {
                        Engine::Notify("Draw a custom formation in Travel settings first");
                    }
                    else {
                        IssueStyle(OrderMode(choice));
                    }
                }
                if (!motion.closing && tileChoice >= 0 && tiles[tileChoice].enabled) {
                    tileFocus = tileChoice;
                    grid.Press(tileChoice);
                    const auto stamp = view.generation;
                    switch (tileChoice) {
                    case ActionGrid::Teleport:
                        Queue([stamp](auto& c) { c.RequestPartyRecall(stamp); });
                        SetWheelOpen(false);
                        break;
                    case ActionGrid::CompanionControl:
                        OpenPackageControl(view, wheelPackageActor);
                        break;
                    default:
                        if (!toggleSaving.exchange(true)) {
                            const int which = tileChoice;
                            Queue([stamp, which](auto& c) {
                                if (c.Snapshot().generation == stamp) {
                                    if (which == ActionGrid::TemporaryRest) {
                                        c.ToggleTemporaryRelaxation(stamp);
                                    }
                                    else if (which == ActionGrid::HoldHands) {
                                        ToggleHandHolding(c);
                                    }
                                    else if (which == ActionGrid::LootScout) {
                                        ToggleLootScouting(c);
                                    }
                                    else if (which == ActionGrid::Pathfinder) {
                                        TogglePathfinder(c);
                                    }
                                    else {
                                        ToggleGuidance(c);
                                    }
                                }
                                toggleSaving = false;
                            });
                        }
                        break;
                    }
                }
                const bool control = packageShortcut.exchange(false) || Im::IsKeyPressed(Im::ImGuiKey_C, false);
                if (control && !motion.closing) {
                    OpenPackageControl(view, wheelPackageActor);
                }
                const bool pad = padUsed.load();
                const char* wheelHints = pad ? "Stick: aim    A: order    D-pad down: party actions    B: back"
                                             : "Click / 1-7: order    Tab: party actions    Esc: back";
                const char* gridHints = pad ? "Stick / D-pad: choose    A: select    Up: party orders    B: back"
                                            : "Click / arrows: choose    Enter: select    Tab: party orders    Esc: back";
                CenteredText(draw, wheelHints, {wheelCenter.x, origin.y + w * 1.235F}, w * .017F,
                             Faded(muted, windowFade * grid.WheelVisibility()), w * .95F);
                CenteredText(draw, gridHints, {wheelCenter.x, origin.y + w * 1.235F}, w * .017F,
                             Faded(muted, windowFade * grid.GridVisibility()), w * .95F);
                Im::EndDisabled();
            }
            Im::End();
            Im::PopStyleColor();
            Im::PopStyleVar();
            if (!open) {
                SetWheelOpen(false);
            }
            if (motion.Finished()) {
                commandWindow->IsOpen = false;
                wheelCloseRequested = false;
                gridBackEnabled = false;
                QueueWheelBlur();
            }
        }
        // FORMATION_EDITOR_BEGIN: shared with the offline visual proof.
        struct EditorCompanion
        {
            std::string key, name;
        };
        struct FormationEditorState
        {
            Custom::Formation draft, saved;
            int selected{-1}, dragging{-1};
            bool snap{true};
            float elapsed{}, appear{};
            std::array<float, PARTY_CAPACITY> pop{};
            void Open(const Custom::Formation& current)
            {
                draft = saved = current;
                selected = current.count ? 0 : -1;
                dragging = -1;
                elapsed = appear = 0;
                pop.fill(1);
            }
        };
        // A small flat button in the party menu's style; `primary` fills it yellow.
        bool EditorButton(const char* id, const char* label, V at, V size, bool enabled, bool primary, float fade)
        {
            Im::SetCursorScreenPos(at);
            Im::BeginDisabled(!enabled);
            const bool clicked = Im::InvisibleButton(id, size);
            const bool over = Im::IsItemHovered(), held = Im::IsItemActive();
            Im::EndDisabled();
            auto* draw = Im::GetWindowDrawList();
            const float a = fade * (enabled ? 1.0F : .4F), h = over && enabled ? 1.0F : 0.0F;
            const V top{at.x, at.y + (held ? 1.0F : 0.0F)}, end{top.x + size.x, top.y + size.y};
            const float r = size.y * .2F;
            const auto fill = primary ? Blend(IM_COL32(186, 146, 44, 255), IM_COL32(232, 190, 70, 255), h)
                                      : Blend(IM_COL32(27, 35, 38, 250), IM_COL32(42, 50, 50, 252), h);
            Draw::AddRectFilled(draw, top, end, Faded(fill, a), r, 0);
            Draw::AddRect(draw, top, end,
                          Faded(primary ? IM_COL32(250, 222, 132, 210) : IM_COL32(168, 153, 118, 70 + 110 * h), a), r, 0, 1);
            const float fontSize = std::min(size.y * .46F, Im::GetFontSize());
            CenteredText(draw, label, {top.x + size.x * .5F, top.y + (size.y - fontSize) * .5F}, fontSize,
                         Faded(primary ? IM_COL32(34, 27, 8, 255) : IM_COL32(226, 218, 201, 255), a), size.x * .88F);
            return clicked && enabled;
        }
        // The formation editor: a map seen from above, with you in the middle and the way ahead at
        // the top, and a panel beside it. Returns 1 to save, 2 to save and travel in it, 3 to close.
        int FormationEditor(V origin, V size, FormationEditorState& s, const std::vector<EditorCompanion>& party,
                            float fade)
        {
            auto* draw = Im::GetWindowDrawList();
            auto& f = s.draft;
            const float dt = std::min(Im::GetIO()->DeltaTime, .05F);
            s.elapsed += dt;
            for (int i = 0; i < PARTY_CAPACITY; ++i) {
                s.pop[i] = i < f.count ? Wheel::Follow(s.pop[i], 1, dt, 13) : 0;
            }
            int action = 0;
            const float u = size.y;
            const V end{origin.x + size.x, origin.y + size.y};
            // The two stock widgets (the companion list and the snap box) wear the menu's colours.
            struct WidgetStyle
            {
                WidgetStyle()
                {
                    Im::PushStyleColor(Im::ImGuiCol_FrameBg, IM_COL32(27, 35, 38, 250));
                    Im::PushStyleColor(Im::ImGuiCol_FrameBgHovered, IM_COL32(42, 50, 50, 252));
                    Im::PushStyleColor(Im::ImGuiCol_FrameBgActive, IM_COL32(58, 50, 24, 255));
                    Im::PushStyleColor(Im::ImGuiCol_Button, IM_COL32(40, 47, 48, 255));
                    Im::PushStyleColor(Im::ImGuiCol_ButtonHovered, IM_COL32(58, 50, 24, 255));
                    Im::PushStyleColor(Im::ImGuiCol_PopupBg, IM_COL32(18, 25, 27, 250));
                    Im::PushStyleColor(Im::ImGuiCol_Header, IM_COL32(58, 50, 24, 255));
                    Im::PushStyleColor(Im::ImGuiCol_HeaderHovered, IM_COL32(74, 63, 28, 255));
                    Im::PushStyleColor(Im::ImGuiCol_CheckMark, IM_COL32(240, 199, 74, 255));
                    Im::PushStyleColor(Im::ImGuiCol_Text, IM_COL32(226, 218, 201, 255));
                    Im::PushStyleColor(Im::ImGuiCol_Border, IM_COL32(168, 153, 118, 110));
                    Im::PushStyleVar(Im::ImGuiStyleVar_FrameRounding, 4.0F);
                    Im::PushStyleVar(Im::ImGuiStyleVar_FrameBorderSize, 1.0F);
                }
                ~WidgetStyle()
                {
                    Im::PopStyleVar(2);
                    Im::PopStyleColor(11);
                }
            } widgetStyle;
            Draw::AddRectFilled(draw, {origin.x, origin.y + u * .008F}, {end.x, end.y + u * .008F},
                                IM_COL32(0, 0, 0, 70 * fade), u * .02F, 0);
            Draw::AddRectFilled(draw, origin, end, IM_COL32(18, 25, 27, 246 * fade), u * .02F, 0);
            Draw::AddRect(draw, origin, end, IM_COL32(166, 151, 114, 90 * fade), u * .02F, 0, 1);
            CenteredText(draw, "W A L K   W I T H   M E", {origin.x + size.x * .5F, origin.y + u * .03F}, u * .024F,
                         Faded(gold, fade), size.x * .6F);
            {
                // The formation's own name, spaced like the other headings.
                std::string spaced;
                for (const char ch : f.title) {
                    if (!spaced.empty()) {
                        spaced += ' ';
                    }
                    spaced += static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
                }
                CenteredText(draw, spaced.c_str(), {origin.x + size.x * .5F, origin.y + u * .066F}, u * .019F,
                             Faded(yellow, fade), size.x * .6F);
            }

            // The map.
            const float mapSize = u * .80F;
            const V mapOrigin{origin.x + u * .05F, origin.y + u * .135F};
            const V centre{mapOrigin.x + mapSize * .5F, mapOrigin.y + mapSize * .5F};
            const float radius = mapSize * .5F - u * .04F, scale = radius / Custom::reach;
            auto toScreen = [&](FormationSlot p) {
                return V{centre.x + p.lateral * scale, centre.y - p.longitudinal * scale};
            };
            auto toWorld = [&](V p) { return FormationSlot{(p.x - centre.x) / scale, (centre.y - p.y) / scale}; };
            Draw::AddCircleFilled(draw, centre, radius + u * .012F, IM_COL32(23, 31, 34, 255 * fade), 96);
            Draw::AddCircle(draw, centre, radius + u * .012F, IM_COL32(166, 151, 114, 60 * fade), 96, 1);
            for (int ring = 1; ring <= 4; ++ring) {
                const float r = radius * ring / 4.0F;
                Draw::AddCircle(draw, centre, r, IM_COL32(180, 170, 140, (ring == 4 ? 34 : 22) * fade), 96, 1);
                const auto label = std::to_string(ring * 2) + " m";
                Draw::AddText(draw, Im::GetFont(), u * .017F, {centre.x + r * .72F + 3, centre.y + r * .69F},
                              IM_COL32(150, 150, 140, 150 * fade), label.c_str());
            }
            Draw::AddLine(draw, {centre.x - radius, centre.y}, {centre.x + radius, centre.y},
                          IM_COL32(180, 170, 140, 16 * fade), 1);
            // The line of travel, its dashes drifting forward.
            const float dash = u * .014F, gap = u * .014F, drift = std::fmod(s.elapsed * u * .03F, dash + gap);
            for (float y = centre.y + radius - drift; y > centre.y - radius; y -= dash + gap) {
                const float y1 = std::max(centre.y - radius, y - dash);
                Draw::AddLine(draw, {centre.x, y}, {centre.x, y1}, IM_COL32(240, 199, 74, 46 * fade), 1);
            }
            CenteredText(draw, "A H E A D", {centre.x, mapOrigin.y - u * .004F}, u * .017F, Faded(yellow, fade * .85F),
                         u * .3F);
            CenteredText(draw, "B E H I N D", {centre.x, mapOrigin.y + mapSize - u * .022F}, u * .017F,
                         Faded(muted, fade), u * .3F);
            Draw::AddText(draw, Im::GetFont(), u * .017F, {mapOrigin.x - u * .01F, centre.y - u * .03F},
                          Faded(muted, fade), "LEFT");
            Draw::AddText(draw, Im::GetFont(), u * .017F, {mapOrigin.x + mapSize - u * .035F, centre.y - u * .03F},
                          Faded(muted, fade), "RIGHT");
            // Where nobody may stand: your own footprint.
            Draw::AddCircleFilled(draw, centre, Custom::clearance * scale, IM_COL32(0, 0, 0, 55 * fade), 48);
            Draw::AddCircleFilled(draw, centre, u * .016F, IM_COL32(226, 218, 201, 255 * fade), 28);
            Draw::AddLine(draw, {centre.x - u * .014F, centre.y - u * .026F}, {centre.x, centre.y - u * .04F},
                          IM_COL32(240, 199, 74, 230 * fade), 2);
            Draw::AddLine(draw, {centre.x, centre.y - u * .04F}, {centre.x + u * .014F, centre.y - u * .026F},
                          IM_COL32(240, 199, 74, 230 * fade), 2);
            CenteredText(draw, "You", {centre.x, centre.y + u * .022F}, u * .018F, Faded(muted, fade), u * .1F);

            // Mouse on the map: press to pick up a place (or put down a new one), drag, right-click
            // to remove.
            const auto mouse = Im::GetIO()->MousePos;
            const float token = u * .024F;
            auto under = [&](V p) {
                int best = -1;
                float bestDistance = token * 1.25F;
                for (int i = 0; i < f.count; ++i) {
                    const auto q = toScreen(f.slots[i]);
                    const float d = std::hypot(q.x - p.x, q.y - p.y);
                    if (d < bestDistance) {
                        best = i;
                        bestDistance = d;
                    }
                }
                return best;
            };
            Im::SetCursorScreenPos(mapOrigin);
            Im::InvisibleButton("map", {mapSize, mapSize});
            const bool hovered = Im::IsItemHovered();
            const bool inside = std::hypot(mouse.x - centre.x, mouse.y - centre.y) <= radius + token;
            if (Im::IsItemActivated() && inside) {
                const int hit = under(mouse);
                if (hit >= 0) {
                    s.selected = s.dragging = hit;
                }
                else if (Custom::Add(f, toWorld(mouse), s.snap)) {
                    s.selected = s.dragging = f.count - 1;
                    s.pop[s.selected] = .35F;
                }
            }
            if (Im::IsItemActive() && s.dragging >= 0 && s.dragging < f.count) {
                const auto moved = Custom::Constrain(toWorld(mouse), s.snap);
                if (!Custom::Crowded(f, moved, s.dragging)) {
                    f.slots[s.dragging] = moved;
                }
            }
            else {
                s.dragging = -1;
            }
            if (hovered && Im::IsMouseClicked(1)) {
                if (const int hit = under(mouse); hit >= 0) {
                    Custom::Remove(f, hit);
                    s.selected = std::min(s.selected, f.count - 1);
                }
            }
            if (s.selected >= 0 && s.selected < f.count && !Im::IsAnyItemActive()) {
                if (Im::IsKeyPressed(Im::ImGuiKey_Delete, false)) {
                    Custom::Remove(f, s.selected);
                    s.selected = std::min(s.selected, f.count - 1);
                }
                else {
                    const float nx = static_cast<float>(Im::IsKeyPressed(Im::ImGuiKey_RightArrow) -
                                                        Im::IsKeyPressed(Im::ImGuiKey_LeftArrow));
                    const float ny = static_cast<float>(Im::IsKeyPressed(Im::ImGuiKey_UpArrow) -
                                                        Im::IsKeyPressed(Im::ImGuiKey_DownArrow));
                    if (nx != 0 || ny != 0) {
                        const auto nudged = Custom::Constrain(
                            {f.slots[s.selected].lateral + nx * Custom::step,
                             f.slots[s.selected].longitudinal + ny * Custom::step}, s.snap);
                        if (!Custom::Crowded(f, nudged, s.selected)) {
                            f.slots[s.selected] = nudged;
                        }
                    }
                }
            }
            // Where companions beyond the drawn places fall in (once there is a formation to follow).
            for (int role = f.count; f.count > 0 && role < static_cast<int>(party.size()) && role < PARTY_CAPACITY; ++role) {
                const auto p = toScreen(Custom::RolePosition(f, role));
                for (int k = 0; k < 12; k += 2) {
                    const float a0 = k * Wheel::pi / 6, a1 = (k + 1) * Wheel::pi / 6;
                    Draw::PathArcTo(draw, p, token * .8F, a0, a1, 4);
                    Draw::PathStroke(draw, IM_COL32(180, 176, 160, 90 * fade), 0, 1.5F);
                }
            }
            // A ghost where a click would put a new place.
            if (hovered && inside && s.dragging < 0 && under(mouse) < 0 && f.count < PARTY_CAPACITY) {
                const auto ghost = Custom::Constrain(toWorld(mouse), s.snap);
                if (!Custom::Crowded(f, ghost)) {
                    const auto p = toScreen(ghost);
                    Draw::AddCircle(draw, p, token, IM_COL32(240, 199, 74, 110 * fade), 28, 1.5F);
                    Draw::AddLine(draw, {p.x - token * .35F, p.y}, {p.x + token * .35F, p.y},
                                  IM_COL32(240, 199, 74, 150 * fade), 1.5F);
                    Draw::AddLine(draw, {p.x, p.y - token * .35F}, {p.x, p.y + token * .35F},
                                  IM_COL32(240, 199, 74, 150 * fade), 1.5F);
                }
            }
            for (int i = 0; i < f.count; ++i) {
                const auto p = toScreen(f.slots[i]);
                const bool chosen = i == s.selected;
                const float q = s.pop[i], grow = 1 + .5F * (1 - q) * std::sin(q * Wheel::pi);
                const float r = token * (.6F + .4F * q) * grow;
                if (chosen) {
                    for (int ring = 3; ring > 0; --ring) {
                        Draw::AddCircleFilled(draw, p, r + ring * u * .006F, IM_COL32(240, 199, 74, 16 * fade), 32);
                    }
                }
                Draw::AddLine(draw, centre, p, IM_COL32(240, 199, 74, (chosen ? 55 : 18) * fade), 1);
                Draw::AddCircleFilled(draw, p, r, chosen ? IM_COL32(58, 50, 24, 255 * fade) : IM_COL32(26, 34, 37, 255 * fade),
                                      32);
                Draw::AddCircle(draw, p, r,
                                chosen ? IM_COL32(246, 206, 86, 255 * fade) : IM_COL32(214, 206, 186, 200 * fade), 32,
                                chosen ? 2.5F : 1.5F);
                const auto number = std::to_string(i + 1);
                {
                    Inscription font;
                    CenteredText(draw, number.c_str(), {p.x, p.y - r * .62F}, r * 1.2F,
                                 chosen ? IM_COL32(250, 224, 140, 255 * fade) : IM_COL32(230, 222, 204, 255 * fade), r * 2);
                }
                if (!f.names[i].empty()) {
                    CenteredText(draw, f.names[i].c_str(), {p.x, p.y + r + u * .004F}, u * .017F,
                                 Faded(chosen ? yellow : IM_COL32(200, 196, 182, 255), fade), u * .16F);
                }
            }

            // The panel.
            const float x0 = mapOrigin.x + mapSize + u * .05F, x1 = end.x - u * .05F, pw = x1 - x0;
            float y = origin.y + u * .15F;
            auto line = [&](const char* value, float fontSize, Im::ImU32 color) {
                Draw::AddText(draw, Im::GetFont(), fontSize, {x0, y}, Faded(color, fade), value);
                y += fontSize * 1.3F;
            };
            {
                Inscription font;
                Draw::AddText(draw, Im::GetFont(), u * .034F, {x0, y}, Faded(IM_COL32(235, 225, 203, 255), fade), "Places");
            }
            const auto tally = std::to_string(f.count) + " / " + std::to_string(PARTY_CAPACITY);
            const float tallyWidth = Im::CalcTextSize(tally.c_str()).x * u * .024F / Im::GetFontSize();
            Draw::AddText(draw, Im::GetFont(), u * .024F, {x1 - tallyWidth, y + u * .008F}, Faded(gold, fade),
                          tally.c_str());
            y += u * .05F;
            line("Click the map to add a place, drag to move it.", u * .019F, muted);
            line("Right-click or Delete removes; arrow keys nudge.", u * .019F, muted);
            y += u * .012F;
            Draw::AddLine(draw, {x0, y}, {x1, y}, IM_COL32(166, 151, 114, 50 * fade), 1);
            y += u * .025F;
            if (s.selected >= 0 && s.selected < f.count) {
                const auto slot = f.slots[s.selected];
                {
                    Inscription font;
                    const auto title = "Place " + std::to_string(s.selected + 1);
                    Draw::AddText(draw, Im::GetFont(), u * .028F, {x0, y}, Faded(yellow, fade), title.c_str());
                }
                y += u * .042F;
                char where[96];
                std::snprintf(where, sizeof(where), "%.1f m %s,  %.1f m %s",
                              std::abs(slot.lateral) / Custom::unitsPerMetre, slot.lateral < 0 ? "left" : "right",
                              std::abs(slot.longitudinal) / Custom::unitsPerMetre,
                              slot.longitudinal < 0 ? "behind" : "ahead");
                line(where, u * .02F, IM_COL32(200, 196, 182, 255));
                y += u * .01F;
                line("Who stands here", u * .019F, muted);
                const auto& reservedName = f.names[s.selected];
                const std::string preview = reservedName.empty() ? "Anyone" : reservedName;
                Im::SetCursorScreenPos({x0, y});
                Im::SetNextItemWidth(pw);
                if (Im::BeginCombo("##who", preview.c_str())) {
                    if (Im::Selectable("Anyone", reservedName.empty())) {
                        Custom::Reserve(f, s.selected, {}, {});
                    }
                    for (const auto& companion : party) {
                        if (Im::Selectable(companion.name.c_str(), f.companions[s.selected] == companion.key)) {
                            Custom::Reserve(f, s.selected, companion.key, companion.name);
                        }
                    }
                    Im::EndCombo();
                }
                y += u * .06F;
                const float bw = (pw - u * .02F) * .5F, bh = u * .05F;
                if (EditorButton("mirror", "Mirror", {x0, y}, {bw, bh},
                                 f.count < PARTY_CAPACITY && std::abs(slot.lateral) >= Custom::spacing * .5F, false, fade)) {
                    if (Custom::MirrorOne(f, s.selected, s.snap)) {
                        s.selected = f.count - 1;
                        s.pop[s.selected] = .35F;
                    }
                }
                if (EditorButton("remove", "Remove", {x0 + bw + u * .02F, y}, {bw, bh}, true, false, fade)) {
                    Custom::Remove(f, s.selected);
                    s.selected = std::min(s.selected, f.count - 1);
                }
                y += bh + u * .03F;
            }
            else {
                line(f.count ? "Select a place to choose who stands there." : "No places yet.", u * .02F,
                     IM_COL32(200, 196, 182, 255));
                line(f.count ? "" : "Click the map, or start from a style below.", u * .019F, muted);
                y += u * .086F;
            }
            Draw::AddLine(draw, {x0, y}, {x1, y}, IM_COL32(166, 151, 114, 50 * fade), 1);
            y += u * .025F;
            line("Start from", u * .019F, muted);
            {
                constexpr const char* names[]{"Natural", "Lead", "Companion", "Rear"};
                constexpr FormationMode styles[]{FormationMode::kDynamic, FormationMode::kLead,
                                                 FormationMode::kCompanion, FormationMode::kRear};
                const float bw = (pw - u * .03F) / 4, bh = u * .046F;
                for (int i = 0; i < 4; ++i) {
                    Im::PushID(i);
                    if (EditorButton("style", names[i], {x0 + i * (bw + u * .01F), y}, {bw, bh}, true, false, fade)) {
                        Custom::Formation fresh;
                        const int places = std::clamp(std::max(static_cast<int>(party.size()), 3), 1, PARTY_CAPACITY);
                        for (int k = 0; k < places; ++k) {
                            Custom::Add(fresh, FormationPosition(styles[i], k), false);
                        }
                        f = fresh;
                        s.selected = f.count ? 0 : -1;
                        for (int k = 0; k < f.count; ++k) {
                            s.pop[k] = .2F + .05F * k;
                        }
                    }
                    Im::PopID();
                }
                y += bh + u * .016F;
                const float half = (pw - u * .02F) * .5F;
                if (EditorButton("flip", "Flip left and right", {x0, y}, {half, bh}, f.count > 0, false, fade)) {
                    Custom::Flip(f);
                }
                if (EditorButton("clear", "Clear", {x0 + half + u * .02F, y}, {half, bh}, f.count > 0, false, fade)) {
                    f = {};
                    s.selected = -1;
                }
                y += bh + u * .02F;
            }
            Im::SetCursorScreenPos({x0, y});
            Im::Checkbox("Snap to a 35 cm grid", &s.snap);
            y += u * .06F;
            const int partySize = static_cast<int>(party.size());
            std::string summary = f.count == 0     ? "Draw at least one place to use this formation."
                                  : partySize == 0 ? "Your companions will take these places in order."
                                  : partySize > f.count
                                      ? std::to_string(partySize - f.count) + (partySize - f.count == 1 ? " companion falls" : " companions fall") +
                                            " in behind (dotted)."
                                      : "Everyone in your party has a place.";
            line(summary.c_str(), u * .019F, IM_COL32(200, 196, 182, 255));

            // Footer.
            const bool changed = !(f == s.saved);
            const float bh = u * .058F, by = end.y - u * .045F - bh;
            if (changed) {
                Draw::AddText(draw, Im::GetFont(), u * .019F, {x0, by - u * .04F}, Faded(yellow, fade * .9F),
                              "Unsaved changes");
            }
            const float bw = (pw - u * .04F) / 3;
            if (EditorButton("close", "Close", {x0, by}, {bw, bh}, true, false, fade)) {
                action = 3;
            }
            if (EditorButton("save", "Save", {x0 + bw + u * .02F, by}, {bw, bh}, changed, false, fade)) {
                action = 1;
            }
            if (EditorButton("use", "Save and use", {x0 + 2 * (bw + u * .02F), by}, {bw, bh}, f.count > 0, true, fade)) {
                action = 2;
            }
            return action;
        }
        // FORMATION_EDITOR_END
        FormationEditorState formationEditor;
        MF::Model::WindowInterface* formationWindow{};
        void OpenFormationEditor(const PartyView& view)
        {
            if (!formationWindow) {
                return;
            }
            formationEditor.Open(view.settings.customFormation);
            formationWindow->IsOpen = true;
        }
        void __stdcall RenderFormationEditor()
        {
            if (!formationWindow || !formationWindow->IsOpen) {
                return;
            }
            const auto view = Read();
            Theme theme;
            const auto display = Im::GetIO()->DisplaySize;
            const float height = std::min(display.y * .86F, display.x * .92F / 1.52F), width = height * 1.52F;
            Im::SetNextWindowPos({display.x * .5F, display.y * .5F}, Im::ImGuiCond_Always, {.5F, .5F});
            Im::SetNextWindowSize({width, height}, Im::ImGuiCond_Always);
            Im::PushStyleVar(Im::ImGuiStyleVar_WindowBorderSize, 0.0F);
            Im::PushStyleColor(Im::ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
            if (Im::Begin("WalkWithMeFormationEditor", nullptr,
                          Im::ImGuiWindowFlags_NoTitleBar | Im::ImGuiWindowFlags_NoResize | Im::ImGuiWindowFlags_NoMove |
                              Im::ImGuiWindowFlags_NoScrollbar | Im::ImGuiWindowFlags_NoScrollWithMouse |
                              Im::ImGuiWindowFlags_NoSavedSettings | Im::ImGuiWindowFlags_NoNav)) {
                auto& s = formationEditor;
                s.appear = std::min(1.0F, s.appear + std::min(Im::GetIO()->DeltaTime, .05F) / .25F);
                const float fade = Wheel::EaseOut(s.appear);
                Draw::AddRectFilled(Im::GetBackgroundDrawList(), {0, 0}, display, IM_COL32(7, 12, 16, 150 * fade), 0, 0);
                std::vector<EditorCompanion> party;
                for (const auto& row : view.party) {
                    if (!row.handHoldKey.empty() && (row.managed || row.manuallyAdded || row.packageControlled ||
                                                     row.apiRegistered || row.eligible || row.teammate)) {
                        party.push_back({row.handHoldKey, row.name});
                    }
                }
                const auto origin = Im::GetWindowPos();
                const int action = FormationEditor({origin.x, origin.y + height * .02F * (1 - fade)}, {width, height},
                                                   s, party, fade);
                if (action == 1 || action == 2) {
                    const auto formation = Custom::Sanitize(s.draft);
                    const bool use = action == 2;
                    s.saved = s.draft = formation;
                    Queue([formation, use](auto& c) {
                        auto value = c.Snapshot().settings;
                        value.customFormation = Custom::WithPlaces(value.customFormation, formation);
                        if (!c.ApplyPreferences(value)) {
                            Engine::Notify("Walk With Me could not save the custom formation");
                            return;
                        }
                        if (use && formation.Defined()) {
                            c.SetMode(FormationMode::kCustom);
                            ++orderNotice;
                            Engine::Notify(fmt::format("Travelling in {}", value.customFormation.title).c_str());
                        }
                        else {
                            Engine::Notify(formation.Defined() ? "Custom formation saved" : "Custom formation cleared");
                        }
                    });
                }
                if (action == 2 || action == 3 || Im::IsKeyPressed(Im::ImGuiKey_Escape, false)) {
                    formationWindow->IsOpen = false;
                }
            }
            Im::End();
            Im::PopStyleColor();
            Im::PopStyleVar();
        }
        void __stdcall RenderHUD()
        {
            const auto view = Read();
            static Overhead::Animation chatMotion, lootMotion, pathMotion;
            static std::uint64_t markerGeneration{};
            if (markerGeneration != view.generation) {
                chatMotion.Clear();
                lootMotion.Clear();
                pathMotion.Clear();
                markerGeneration = view.generation;
            }
            chatMotion.BeginFrame(Im::GetIO()->DeltaTime);
            lootMotion.BeginFrame(Im::GetIO()->DeltaTime);
            pathMotion.BeginFrame(Im::GetIO()->DeltaTime);
            // Publish only game-thread projections; never retain a hidden or missing marker.
            if (view.inGame && view.enabled && !MF::IsAnyBlockingWindowOpened()) {
                const auto display = Im::GetIO()->DisplaySize;
                const float scale = std::clamp(display.y / 1080.0F, .65F, 2.5F);
                auto* draw = Im::GetForegroundDrawList();
                if (view.settings.restDialogueIcons) {
                    for (const auto& marker : RestDialogue::ReadMarkers()) {
                        const auto pose = chatMotion.Show(marker.actor, view.settings.animateChatIcons);
                        const float size = 36 * scale;
                        const V p{marker.x * display.x, marker.y * display.y + pose.rise * size};
                        OrderVisuals::Chat(draw, p, size * pose.scale, pose.alpha);
                    }
                }
                const bool lootIcons = view.settings.loot.enabled && view.settings.loot.icons;
                const bool pathIcons = view.settings.pathfinder.enabled && view.settings.pathfinder.icon;
                if (lootIcons || pathIcons) {
                    for (const auto& marker : Loot::ReadMarkers()) {
                        if (marker.pathfinder ? !pathIcons : !lootIcons) {
                            continue;
                        }
                        const auto pose = (marker.pathfinder ? pathMotion : lootMotion).Show(marker.actor);
                        const float size = 34 * scale;
                        const V p{marker.x * display.x, marker.y * display.y + pose.rise * size};
                        if (marker.pathfinder) {
                            OrderVisuals::PathfinderEmblem(draw, p, size * pose.scale, pose.alpha * marker.alpha);
                        }
                        else {
                            OrderVisuals::Loot(draw, p, size * pose.scale, marker.valuable,
                                               pose.alpha * marker.alpha);
                        }
                    }
                }
            }
            chatMotion.EndFrame();
            lootMotion.EndFrame();
            pathMotion.EndFrame();
            if (capturing.load() >= 0 && GetTickCount64() - captureStarted.load() > 15000) {
                CancelCapture();
            }
            const bool preview = hudPreviewAt && GetTickCount64() - hudPreviewAt < 200;
            const bool show = preview ? hudPreviewEnabled : view.settings.showOrderHUD;
            static HudNotice notice;
            static bool shown = false, wasPreview = false;
            static std::uint64_t seenOrder{};
            static HudNoticePresentation presentation;
            if (!view.inGame || !view.enabled || !show) {
                shown = false;
                return;
            }
            if (!preview && (!view.gameplay || MF::IsAnyBlockingWindowOpened())) {
                return;
            }
            const int mode =
                view.sandboxActive ? 4 : std::clamp(static_cast<int>(view.mode), 0, Wheel::count - 1);
            const std::uint64_t key = (view.generation << 16) | (static_cast<std::uint64_t>(view.mode) << 8) |
                                      static_cast<std::uint64_t>(view.sandboxActive);
            const auto order = orderNotice.load();
            if (!preview) {
                presentation.Update(view.generation, static_cast<int>(view.mode), view.sandboxActive,
                                    order != seenOrder, !shown || wasPreview);
                notice.Update(key, Im::GetIO()->DeltaTime, !shown || wasPreview || order != seenOrder);
            }
            seenOrder = order;
            shown = true;
            wasPreview = preview;
            const bool compact = !preview && presentation.compact;
            const float alpha = preview ? 1 : notice.Alpha();
            if (alpha <= 0) {
                return;
            }
            const auto display = Im::GetIO()->DisplaySize;
            const float dpi = std::clamp(display.y / 1080.0F, .65F, 2.5F);
            const float scale = (preview ? hudPreviewScale : view.settings.hudScale) * dpi;
            const float height = preview ? hudPreviewHeight : view.settings.hudVertical;
            const float right = display.x - 24 * dpi + (preview || compact ? 0 : notice.Slide() * dpi),
                        y = display.y * height;
            auto* draw = Im::GetForegroundDrawList();
            if (compact) {

                OrderVisuals::Icon(draw, {right - 42 * scale, y}, 24.5F * scale, mode, 5.0F, alpha, true);
                return;
            }
            if (preview ? hudPreviewPanel : view.settings.hudPanel) {
                Draw::AddRectFilledMultiColor(draw, {right - 270 * scale, y - 44 * scale},
                                              {right, y + 44 * scale}, IM_COL32(20, 27, 29, 0),
                                              IM_COL32(20, 27, 29, 238 * alpha),
                                              IM_COL32(20, 27, 29, 238 * alpha), IM_COL32(20, 27, 29, 0));
                Draw::AddLine(draw, {right, y - 44 * scale}, {right, y + 44 * scale},
                              IM_COL32(198, 173, 125, 255 * alpha), 2 * scale);
            }
            OrderVisuals::Icon(draw, {right - 42 * scale, y}, 56 * scale, mode,
                               preview ? 5.0F : notice.elapsed, alpha);
            constexpr const char* titles[]{"Natural", "Lead", "Companion", "Rear", "At Ease", "Vanilla", "Custom"};
            constexpr const char* details[]{"Travelling together", "Taking the lead", "By your side",
                                            "Watching your back",  "Resting here",    "Follower AI restored",
                                            "In your formation"};
            auto text = [&](const char* value, float yy, float size, Im::ImU32 color) {
                auto bounds = Im::CalcTextSize(value);
                float x = right - 86 * scale - bounds.x * size / Im::GetFontSize();
                Draw::AddText(draw, Im::GetFont(), size, {x + 1, yy + 2}, IM_COL32(0, 0, 0, 210 * alpha),
                              value);
                Draw::AddText(draw, Im::GetFont(), size, {x, yy}, color, value);
            };
            {
                Inscription font;
                text(mode == 6 ? view.settings.customFormation.title.c_str() : titles[mode], y - 18 * scale, 20 * scale,
                     IM_COL32(226, 218, 201, 255 * alpha));
            }
            text(details[mode], y + 9 * scale, 12 * scale, IM_COL32(166, 172, 169, 255 * alpha));
        }
        bool __stdcall OnInput(RE::InputEvent* event)
        {
            return HandleInput(event);
        }
        void __stdcall OnMenuEvent(MF::Model::EventType event)
        {
            QueueWheelBlur();
            if (event == MF::Model::EventType::kCloseMenu) {
                CancelCapture();
                captured.store(-1);
            }
            if (event == MF::Model::EventType::kOpenMenu) {
                refresh.store(true);
            }
        }
    } // namespace

    bool HandleInput(RE::InputEvent* event)
    {
        if (event && event->GetDevice() == RE::INPUT_DEVICE::kGamepad) {
            const bool wheelOpen = commandWindow && commandWindow->IsOpen;
            const bool popupOpen = packageWindow && packageWindow->IsOpen;
            if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
                auto* stick = static_cast<RE::ThumbstickEvent*>(event);
                if (stick->IsRight() && wheelOpen) {
                    padX = stick->xValue;
                    padY = -stick->yValue;
                    padStickAt = GetTickCount64();
                    padUsed = true;
                    return true;
                }
                return wheelOpen || popupOpen;
            }
            if (event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
                return wheelOpen || popupOpen;
            }
            auto* button = event->AsButtonEvent();
            if (!button) {
                return wheelOpen;
            }
            const auto key = button->GetIDCode();
            const bool fresh = button->IsDown() && (padHeld & key) == 0;
            if (button->IsPressed()) {
                padHeld |= key;
            }
            else {
                padHeld &= ~key;
            }
            if (popupOpen) {
                if (fresh) {
                    if (key == 0x1000) {
                        packageAccept = true;
                    }
                    else if (key == 0x2000) {
                        packageCancel = true;
                    }
                    else if (key == 0x4000) {
                        packagePush = true;
                    }
                }
                return true;
            }
            if (wheelOpen) {
                padUsed = true;
                if (fresh) {
                    if (key == 0x1000) {
                        padConfirm = true;
                    }
                    else if (key == 0x2000) {
                        padCancel = true;
                    }
                    else if (key == 0x4000) {
                        packageShortcut = true;
                    }
                    else if (key == 1 || key == 2) {
                        padDy += key == 2 ? 1 : -1;
                    }
                    else if (key == 4 || key == 8) {
                        padDx += key == 8 ? 1 : -1;
                    }
                }
                return true;
            }
            const auto view = Read();
            const int binding = view.settings.gamepadBinding;
            constexpr std::array<std::uint32_t, 4> triggers{0, 0x80, 0x8000, 1};
            auto* ui = RE::UI::GetSingleton();
            if (fresh && binding > 0 && key == triggers[std::clamp(binding, 0, 3)] && (padHeld & 0x100) &&
                commandWindow && view.inGame && view.gameplay && ui && !ui->GameIsPaused() &&
                !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) && !ui->IsMenuOpen(RE::Console::MENU_NAME) &&
                !MF::IsAnyBlockingWindowOpened()) {
                padConfirm = false;
                padCancel = false;
                padDx = 0;
                padDy = 0;
                padX = 0;
                padY = 0;
                padStickAt = 0;
                padUsed = true;
                SetWheelOpen(true);
                return true;
            }
            return false;
        }
        if (!event || event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton ||
            event->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
            return false;
        }
        auto* button = event->AsButtonEvent();
        if (!button) {
            return false;
        }
        const auto key = static_cast<std::uint32_t>(button->GetIDCode());
        const auto modifiers = KeyboardModifiers();
        if (capturing.load() >= 0) {
            if (GetTickCount64() - captureStarted.load() > 15000) {
                CancelCapture();
                return false;
            }
            const int chord = Hotkeys::Capture(key, modifiers, button->IsDown(), button->IsUp());
            if (chord >= 0) {
                int empty = -1;
                captured.compare_exchange_strong(empty, chord);
            }
            return true;
        }
        if (!button->IsDown()) {
            return false;
        }
        padUsed = false;
        if (packageWindow && packageWindow->IsOpen) {
            if (key == 1) {
                packageCancel = true;
                return true;
            }
            return false; // Enter and Escape are handled by the focused popup.
        }
        if (commandWindow && commandWindow->IsOpen) {
            if (key == 1 && gridBackEnabled.load()) {
                gridBack = true; // Esc on the grid steps back to the wheel.
                return true;
            }
            if (key == 1 || hotkeyBindings[0].Get(Read().settings).Matches(key, modifiers)) {
                SetWheelOpen(false);
                return true;
            }
            return false;
        }
        if (installed && MF::IsAnyBlockingWindowOpened()) {
            return false;
        }
        auto* ui = RE::UI::GetSingleton();

        if (!ui || ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME) ||
            ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) || ui->IsMenuOpen(RE::TweenMenu::MENU_NAME) ||
            ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME) || ui->IsMenuOpen(RE::MagicMenu::MENU_NAME) ||
            ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME) || ui->IsMenuOpen(RE::BarterMenu::MENU_NAME) ||
            ui->IsMenuOpen(RE::MapMenu::MENU_NAME) || ui->IsMenuOpen(RE::JournalMenu::MENU_NAME) ||
            ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME) || ui->IsMenuOpen(RE::StatsMenu::MENU_NAME) ||
            ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME) || ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME) ||
            ui->IsMenuOpen(RE::BookMenu::MENU_NAME) || ui->IsMenuOpen(RE::SleepWaitMenu::MENU_NAME) ||
            ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME)) {
            return false;
        }
        const auto view = Read();
        if (!view.inGame) {
            return false;
        }
        int action = -1;
        for (int i = 0; i < static_cast<int>(hotkeyBindings.size()); ++i) {
            if (hotkeyBindings[i].Get(view.settings).Matches(key, modifiers)) {
                action = i;
                break;
            }
        }
        if (action < 0) {
            return false;
        }
        if (action == 0) {
            if (commandWindow) {
                SetWheelOpen(true);
            }
            else {
                Engine::Notify("Walk With Me party menu requires SKSE Menu Framework 3");
            }
        }
        else if (action == static_cast<int>(companionHotkeyRow)) {
            if (packageWindow) {
                OpenPackageControl(FormationController::GetSingleton().Snapshot());
            }
            else {
                Engine::Notify("Companion control requires SKSE Menu Framework 3");
            }
        }
        else {
            Queue([action](auto& c) {
                switch (action) {
                case 1:
                    ToggleGuidance(c);
                    break;
                case 2:
                    c.CycleMode();
                    break;
                case 3:
                    c.GiveOrder(PartyOrder::kScout);
                    break;
                case 4:
                    c.GiveOrder(PartyOrder::kRear);
                    break;
                case 5:
                    c.GiveOrder(PartyOrder::kRoam);
                    break;
                case 6:
                    c.ReloadSettings();
                    break;
                case static_cast<int>(handHoldHotkeyRow):
                    ToggleHandHolding(c);
                    break;
                }
                if (action >= 2 && action <= 5) {
                    ++orderNotice;
                    CommandGesture::Request(c.Snapshot().mode);
                }
            });
        }
        return true;
    }

    RE::FormID CompanionTarget()
    {
        return packageSelection.load();
    }

    void Publish()
    {
        SyncWheelBlur();
        const auto now = GetTickCount64();
        if (!refresh.exchange(false) && now < nextPublish) {
            return;
        }
        nextPublish = now + 250;
        Cache(FormationController::GetSingleton().Snapshot());
    }
    void Reset()
    {
        RestDialogue::Reset();
        CommandGesture::Reset();
        padHeld = 0;
        padX = 0;
        padY = 0;
        padStickAt = 0;
        padConfirm = false;
        padCancel = false;
        padDx = 0;
        padDy = 0;
        gridBack = false;
        gridBackEnabled = false;
        if (commandWindow) {
            commandWindow->IsOpen = false;
        }
        wheelCloseRequested = false;
        if (packageWindow) {
            packageWindow->IsOpen = false;
        }
        packageSelection = 0;
        packageAccept = false;
        packageCancel = false;
        packageShortcut = false;
        packagePush = false;
        SyncWheelBlur();
        CancelCapture();
        captured.store(-1);
        refresh.store(true);
        std::lock_guard lock(viewMutex);
        cached.inGame = false;
        cached.gameplay = false;
        cached.party.clear();
    }
    bool IsInstalled()
    {
        return installed.load();
    }
    bool HasBlockingWindow()
    {
        return installed.load() && MF::IsAnyBlockingWindowOpened();
    }
    void Register()
    {
        Cache(FormationController::GetSingleton().Snapshot());
        if (!GetModuleHandleW(L"SKSEMenuFramework.dll") || MF::GetMenuFrameworkVersion() < 3.0F) {
            logger::warn(
                "[Menu] SKSE Menu Framework 3 not found; INI and keyboard controls remain available");
            return;
        }
        MF::SetSection("Walk With Me");
        MF::AddSectionItem("Party", RenderParty);
        MF::AddSectionItem("Travel", RenderTravel);
        MF::AddSectionItem("Rest", RenderRest);
        MF::AddSectionItem("Roles", RenderRoles);
        MF::AddSectionItem("Interface", RenderInterface);
        MF::AddSectionItem("Controls", RenderControls);
        commandWindow = MF::AddWindow(RenderCommands, true);
        packageWindow = MF::AddWindow(RenderPackageControl, true);
        formationWindow = MF::AddWindow(RenderFormationEditor, true);
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->Register(companionPauseName, CompanionPauseMenu::Create);
        }
        inputRegistration.reset(MF::AddInputEvent(OnInput));
        hudRegistration.reset(MF::AddHudElement(RenderHUD));
        eventRegistration.reset(MF::AddEvent(OnMenuEvent, 0));
        installed.store(true);
        logger::info("[Menu] Party, Travel, Rest, Loot, Interface, Controls, command window and HUD "
                     "registered (framework {})",
                     MF::GetMenuFrameworkVersion());
    }
} // namespace Wayfarer::Menu

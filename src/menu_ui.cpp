#include "menu_ui.h"
#include "SKSEMenuFramework.h"
#include "command_gesture.h"
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
        std::atomic<bool> temporaryRestSaving{};
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
        std::atomic<int> padStep{};
        std::uint32_t padHeld{};
        float hudPreviewScale{.8F}, hudPreviewHeight{.32F};
        constexpr std::array<const char*, 6> profileNames{"Natural", "Lead",  "Companion",
                                                          "Rear",    "Relax", "Vanilla"};
        constexpr auto profileModes = OrderModes;
        constexpr std::array<const char*, 6> styleHints{
            "Find your own space as we travel",
            "Take the lead along our route",
            "Travel at my side",
            "Keep to the rear",
            "Stay here and make yourselves at home",
            "Return travel and rest to Skyrim or your follower framework"};
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

        void Icon(Im::ImDrawList* draw, V c, float r, int type, Im::ImU32 color)
        {
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
            if (Im::BeginTable("orders", 3, Im::ImGuiTableFlags_SizingStretchSame)) {
                for (int i = 0; i < Wheel::count; ++i) {
                    Im::TableNextColumn();
                    const bool selected = view.mode == profileModes[i];
                    if (selected) {
                        Im::PushStyleColor(Im::ImGuiCol_Text, gold);
                        Im::PushStyleColor(Im::ImGuiCol_Button, IM_COL32(67, 62, 49, 230));
                    }
                    if (Im::Button(profileNames[i], {-1, 0})) {
                        IssueStyle(profileModes[i]);
                    }
                    Help(styleHints[i]);
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
                    Im::BeginDisabled(row.apiRegistered ||
                                      (!remove && (!row.eligible || !view.enabled ||
                                                   view.mode == FormationMode::kVanilla)));
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
                int mode = static_cast<int>(draft.mode);
                if (Im::Combo("##mode", &mode, "Natural\0Lead\0Companion\0Rear\0Relax\0Vanilla\0")) {
                    draft.mode = profileModes[mode];
                    dirty = true;
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
                       "cell. Works while you ride, sneak, fight or stand still. Lands them behind you, then "
                       "on your trail, then anywhere reachable nearby. Waiting, fighting, scripted, "
                       "seated and relaxing companions stay put, and so does everyone in Sovngarde, "
                       "Skuldafn, Apocrypha, the Soul Cairn and the Underforge.");
                Im::BeginDisabled(!draft.teleportCatchup);
                Slider(
                    "Teleport beyond", draft.teleportDistance, 1500, 15000, "%.0f units",
                    "Distance from you before teleport catch-up. Each companion has an 8-second cooldown.");
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
        void __stdcall RenderLoot()
        {
            const auto view = Read();
            Prepare(view);
            SettingsPage page("WalkWithMeLoot");
            Header(view, "LOOT");
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
                    Im::SetNextItemWidth(-1);
                    std::string finder = loot.finder.empty()       ? "Any companion"
                                         : loot.finderName.empty() ? "Selected companion"
                                                                   : loot.finderName;
                    if (!loot.finder.empty() &&
                        std::none_of(view.party.begin(), view.party.end(),
                                     [&](const auto& row) { return row.handHoldKey == loot.finder; })) {
                        finder += " (not nearby)";
                    }
                    if (Im::BeginCombo("##lootFinder", finder.c_str())) {
                        if (Im::Selectable("Any companion", loot.finder.empty())) {
                            loot.finder.clear();
                            loot.finderName.clear();
                            dirty = true;
                        }
                        for (const auto& row : view.party) {
                            Im::PushID(static_cast<int>(row.id));
                            const bool selectable = !row.excluded && !row.handHoldKey.empty();
                            Im::BeginDisabled(!selectable);
                            if (Im::Selectable(row.name.c_str(),
                                               !loot.finder.empty() && row.handHoldKey == loot.finder)) {
                                loot.finder = row.handHoldKey;
                                loot.finderName = row.name;
                                dirty = true;
                            }
                            Im::EndDisabled();
                            if (!selectable) {
                                Help(row.handHoldKey.empty()
                                         ? "This temporary actor has no persistent reference "
                                           "to remember. Choose a placed companion."
                                         : "Include this companion on the Party page first.");
                            }
                            Im::PopID();
                        }
                        Im::EndCombo();
                    }
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

        // ORDER_UTILITY_CARD_BEGIN: shared with the offline visual proof.
        bool OrderUtilityCard(const char* id, V at, V size, int kind, bool enabled, bool on,
                              const char* caption, float elapsed, float visibility, float& hover,
                              float& toggle)
        {
            Im::SetCursorScreenPos(at);
            Im::BeginDisabled(!enabled);
            const bool clicked = Im::InvisibleButton(id, size);
            const bool over = Im::IsItemHovered();
            const bool held = Im::IsItemActive();
            Im::EndDisabled();
            const float dt = std::min(Im::GetIO()->DeltaTime, .05F);
            hover = Wheel::Follow(hover, over ? 1.0F : 0.0F, dt, 12);
            toggle = Wheel::Follow(toggle, on ? 1.0F : 0.0F, dt, 16);
            const float alpha = visibility * (enabled ? 1.0F : .48F);
            at.y += size.y * (.16F * (1 - visibility) - .045F * hover + .025F * held);
            auto* draw = Im::GetWindowDrawList();
            const V end{at.x + size.x, at.y + size.y};
            const float rounding = size.y * .12F;
            Draw::AddRectFilled(draw, {at.x, at.y + 4}, {end.x, end.y + 4}, IM_COL32(0, 0, 0, 55 * alpha),
                                rounding, 0);
            Draw::AddRectFilled(draw, at, end,
                                IM_COL32(26 + 9 * hover, 34 + 10 * hover, 37 + 9 * hover, 245 * alpha),
                                rounding, 0);
            Draw::AddRect(draw, at, end, IM_COL32(168, 153, 118, (45 + 100 * hover) * alpha), rounding, 0, 1);
            Draw::AddLine(draw, {at.x + rounding, at.y}, {end.x - rounding, at.y},
                          IM_COL32(222, 199, 146, (20 + 55 * hover) * alpha), 1);
            const V icon{at.x + size.y * .46F, at.y + size.y * .50F};
            OrderVisuals::Icon(draw, icon, size.y * .62F, kind ? 4 : 6,
                               kind ? std::fmod(elapsed, 3.2F) : elapsed,
                               alpha * (kind ? (.65F + .35F * toggle) : 1.0F), true);
            const float left = at.x + size.y * .9F;
            const float maxWidth = size.x - size.y * .98F - (kind ? size.x * .18F : 0);
            auto label = [&](const char* value, float y, float fontSize, Im::ImU32 color) {
                const auto measured = Im::CalcTextSize(value);
                fontSize = std::min(fontSize, maxWidth * Im::GetFontSize() / std::max(measured.x, 1.0F));
                Draw::AddText(draw, Im::GetFont(), fontSize, {left, y}, color, value);
            };
            label(kind ? "Temporary relaxation" : "Teleport party", at.y + size.y * .25F, size.y * .22F,
                  IM_COL32(225, 219, 201, 255 * alpha));
            label(caption, at.y + size.y * .57F, size.y * .16F, IM_COL32(154, 166, 163, 240 * alpha));
            if (kind) {
                const float trackW = size.x * .135F, trackH = size.y * .235F;
                const V start{end.x - size.x * .17F, icon.y - trackH * .5F};
                Draw::AddRectFilled(
                    draw, start, {start.x + trackW, start.y + trackH},
                    IM_COL32(53 + 41 * toggle, 62 + 49 * toggle, 63 + 32 * toggle, 255 * alpha), trackH * .5F,
                    0);
                const float knob = trackH * .36F;
                Draw::AddCircleFilled(
                    draw, {start.x + trackH * .5F + (trackW - trackH) * toggle, icon.y}, knob,
                    IM_COL32(181 + 53 * toggle, 185 + 40 * toggle, 177 + 23 * toggle, 255 * alpha), 20);
            }
            return enabled && clicked;
        }
        // ORDER_UTILITY_CARD_END
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
            static std::array<float, 2> utilityHover{}, utilityToggle{};
            static std::array<CompanionMenu::ButtonMotion, 2> dockMotion{};
            const auto display = Im::GetIO()->DisplaySize;
            const float w = Wheel::Width(display.x, display.y);
            const float dt = std::min(Im::GetIO()->DeltaTime, .05F);
            Im::SetNextWindowPos({display.x * .5F, display.y * .5F}, Im::ImGuiCond_Always, {.5F, .5F});
            Im::SetNextWindowSize({w, w * Wheel::windowHeight}, Im::ImGuiCond_Always);
            bool open = true;
            Im::PushStyleVar(Im::ImGuiStyleVar_WindowBorderSize, 0.0F);
            Im::PushStyleColor(Im::ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
            if (Im::Begin("WalkWithMeOrders", &open,
                          Im::ImGuiWindowFlags_NoTitleBar | Im::ImGuiWindowFlags_NoResize |
                              Im::ImGuiWindowFlags_NoMove | Im::ImGuiWindowFlags_NoScrollbar |
                              Im::ImGuiWindowFlags_NoSavedSettings | Im::ImGuiWindowFlags_NoNav)) {
                if (Im::IsWindowAppearing()) {
                    elapsed = 0;
                    focus = std::clamp(static_cast<int>(view.mode), 0, Wheel::count - 1);
                    lastFocus = -1;
                    pressed = -1;
                    stick = {};
                    padConfirm = false;
                    padCancel = false;
                    padStep = 0;
                    motion.Open(focus);
                    wheelPackageActor = view.crosshairActor;
                    packageShortcut = false;
                    dockMotion = {};
                    utilityHover = {};
                    utilityToggle = {0, view.settings.sandbox.automatic ? 1.0F : 0.0F};
                }
                if (wheelCloseRequested.load()) {
                    motion.Close();
                }
                elapsed += dt;
                centerElapsed += dt;
                const auto origin = Im::GetWindowPos();
                auto* draw = Im::GetWindowDrawList();
                const V center{origin.x + w * .5F, origin.y + w * .48F};
                const float hitRadius = w * .40F;
                auto* backdrop = Im::GetBackgroundDrawList();
                const auto mouse = Im::GetIO()->MousePos;
                int over = Wheel::Mouse(mouse.x - center.x, mouse.y - center.y, hitRadius);
                Im::BeginDisabled(motion.closing);

                Im::SetCursorScreenPos({origin.x, origin.y + w * .04F});
                const bool clicked = Im::InvisibleButton("wheel-hit", {w, w * .87F});
                const bool activated = Im::IsItemActivated();
                if (!Im::IsItemHovered()) {
                    over = -1;
                }
                if (activated) {
                    pressed = over;
                }
                int choice = clicked && pressed >= 0 && pressed == over ? over : -1;
                if (!Im::IsItemActive()) {
                    pressed = -1;
                }
                const bool mouseMoved =
                    std::abs(Im::GetIO()->MouseDelta.x) + std::abs(Im::GetIO()->MouseDelta.y) > .5F;
                int step = padStep.exchange(0);
                if (Im::IsKeyPressed(Im::ImGuiKey_RightArrow) || Im::IsKeyPressed(Im::ImGuiKey_DownArrow)) {
                    ++step;
                }
                if (Im::IsKeyPressed(Im::ImGuiKey_LeftArrow) || Im::IsKeyPressed(Im::ImGuiKey_UpArrow)) {
                    --step;
                }
                if (step) {
                    focus = Wheel::clockwise[(Wheel::slots[focus] + (step > 0 ? 1 : Wheel::count - 1)) %
                                             Wheel::count];
                }
                if (padStickAt.load() && GetTickCount64() - padStickAt.load() < 350) {
                    focus = stick.Update(padX.load(), padY.load(), focus);
                }
                else {
                    stick.engaged = false;
                }
                if (over >= 0 && (mouseMoved || activated)) {
                    focus = over;
                }
                if (focus != lastFocus) {
                    lastFocus = focus;
                    centerElapsed = 0;
                }
                motion.Update(dt, focus);
                const float fade = motion.visibility;
                const float radius = hitRadius * motion.Scale();
                Draw::AddRectFilled(backdrop, {0, 0}, display, IM_COL32(7, 12, 16, 165 * fade), 0, 0);
                // WHEEL_DRAW_BEGIN: shared with the offline visual proof.
                auto text = [&](const char* value, V at, float size, Im::ImU32 color, float width) {
                    auto bounds = Im::CalcTextSize(value);
                    size = std::min(size, width * Im::GetFontSize() / std::max(bounds.x, 1.0F));
                    color =
                        (color & 0x00FFFFFFu) | (static_cast<Im::ImU32>(((color >> 24) & 255) * fade) << 24);
                    Draw::AddText(draw, Im::GetFont(), size,
                                  {at.x - bounds.x * size / Im::GetFontSize() * .5F, at.y}, color, value);
                };
                text("W A L K   W I T H   M E", {center.x, origin.y + w * .005F}, w * .025F, gold, w * .8F);
                text("P A R T Y   O R D E R S", {center.x, origin.y + w * .042F}, w * .017F, muted, w * .8F);
                Draw::AddLine(draw, {center.x - w * .29F, origin.y + w * .018F},
                              {center.x - w * .20F, origin.y + w * .018F}, IM_COL32(180, 163, 127, 65 * fade),
                              1);
                Draw::AddLine(draw, {center.x + w * .20F, origin.y + w * .018F},
                              {center.x + w * .29F, origin.y + w * .018F}, IM_COL32(180, 163, 127, 65 * fade),
                              1);
                for (int layer = 4; layer > 0; --layer) {
                    Draw::AddCircleFilled(draw, center, radius + layer * w * .008F,
                                          IM_COL32(0, 0, 0, 8 * fade), 128);
                }
                Draw::AddCircle(draw, center, radius * 1.023F, IM_COL32(170, 153, 115, 45 * fade), 128, 1);
                for (int mode = 0; mode < Wheel::count; ++mode) {
                    const float a = Wheel::Angle(mode);
                    const float light = motion.highlight[mode];
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
                                       fade * reveal * (view.enabled ? .68F + .32F * light : .4F));
                    text(profileNames[mode], {at.x, at.y + w * .062F}, w * .025F,
                         IM_COL32(155 + 67 * light, 156 + 63 * light, 149 + 58 * light, 255 * reveal),
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
                                                 "Make yourselves at home", "Return to follower AI"};
                for (int i = 0; i < Wheel::count; ++i) {
                    const float weight = motion.centerWeight[i];
                    if (weight < .005F) {
                        continue;
                    }
                    const float lift = w * .007F * (1 - weight);
                    OrderVisuals::Icon(draw, {center.x, center.y - w * .060F + lift}, w * .105F, i,
                                       i == focus ? centerElapsed : 3.0F, fade * weight);
                    {
                        Inscription font;
                        text(profileNames[i], {center.x, center.y + w * .018F + lift}, w * .037F,
                             IM_COL32(235, 225, 203, 255 * weight), w * .32F);
                    }
                    text(captions[i], {center.x, center.y + w * .070F + lift}, w * .020F,
                         IM_COL32(164, 168, 165, 255 * weight), w * .33F);
                }
                // WHEEL_DRAW_END
                if (padConfirm.exchange(false) || Im::IsKeyPressed(Im::ImGuiKey_Enter, false)) {
                    choice = focus;
                }
                for (int i = 0; i < Wheel::count; ++i) {
                    if (Im::IsKeyPressed(static_cast<Im::ImGuiKey>(Im::ImGuiKey_1 + i), false)) {
                        choice = i;
                    }
                }
                const bool cancel = padCancel.exchange(false) || Im::IsKeyPressed(Im::ImGuiKey_Escape, false);
                if (cancel) {
                    open = false;
                }
                else if (!motion.closing && choice >= 0 && view.inGame && view.enabled) {
                    focus = choice;
                    IssueStyle(OrderMode(choice));
                }
                const char* hints = padUsed.load()
                                        ? "Stick: aim   A: order   X: companion control   B: back"
                                        : "Click / 1-6: order    C: companion control    Esc: back";
                const std::string status =
                    !view.inGame ? "Available in game"
                    : !view.enabled
                        ? "Walk With Me is disabled"
                        : fmt::format(
                              "ACTIVE ORDER  /  {}",
                              profileNames[std::clamp(static_cast<int>(view.mode), 0, Wheel::count - 1)]);
                const auto teleport = Wheel::UtilityBox(0, w), relax = Wheel::UtilityBox(1, w);
                const std::string recallCaption = view.recallPending ? "Teleport queued"
                                                  : !view.recallReason.empty()
                                                      ? view.recallReason
                                                      : fmt::format("{} companion{} ready", view.recallCount,
                                                                    view.recallCount == 1 ? "" : "s");
                if (OrderUtilityCard("party-teleport", {origin.x + teleport.x, origin.y + teleport.y},
                                     {teleport.w, teleport.h}, 0,
                                     view.inGame && view.recallReason.empty() && !view.recallPending, false,
                                     recallCaption.c_str(), elapsed, fade, utilityHover[0],
                                     utilityToggle[0])) {
                    const auto stamp = view.generation;
                    Queue([stamp](auto& c) { c.RequestPartyRecall(stamp); });
                    SetWheelOpen(false);
                }
                Help("Mouse: summon eligible travelling companions to clear ground nearby, including from "
                     "other cells. Waiting or scripted companions stay put.");
                const bool restOn = view.settings.sandbox.automatic;
                if (OrderUtilityCard("temporary-relaxation", {origin.x + relax.x, origin.y + relax.y},
                                     {relax.w, relax.h}, 1, view.inGame && !temporaryRestSaving.load(),
                                     restOn,
                                     temporaryRestSaving ? "Saving..."
                                     : restOn            ? "ON - rest when you stop"
                                                         : "OFF - keep travelling",
                                     elapsed, fade, utilityHover[1], utilityToggle[1])) {
                    if (!temporaryRestSaving.exchange(true)) {
                        const auto stamp = view.generation;
                        Queue([stamp](auto& c) {
                            c.ToggleTemporaryRelaxation(stamp);
                            temporaryRestSaving = false;
                        });
                    }
                }
                Help("Mouse: toggle automatic relaxation during travel. Saves immediately; the manual Relax "
                     "order stays available.");
                text(status.c_str(), {center.x, origin.y + w * 1.09F}, w * .019F, gold, w * .8F);
                text(hints, {center.x, origin.y + w * 1.235F}, w * .017F, muted, w * .95F);
                const float dockFade = fade * Wheel::EaseOut((elapsed - .16F) / .26F);
                const bool control =
                    OrnamentButton("companion-control", "Companion control", padUsed.load() ? "X" : "C",
                                   {origin.x + w * .07F, origin.y + w * 1.14F}, {w * .54F, w * .072F},
                                   dockFade, dockMotion[0]) ||
                    packageShortcut.exchange(false) || Im::IsKeyPressed(Im::ImGuiKey_C, false);
                if (control && !motion.closing) {
                    OpenPackageControl(view, wheelPackageActor);
                }
                Help("Open custom companion control (C / gamepad X).");
                if (OrnamentButton("wheel-return", "Return", padUsed.load() ? "B" : "Esc",
                                   {origin.x + w * .63F, origin.y + w * 1.14F}, {w * .30F, w * .072F},
                                   fade * Wheel::EaseOut((elapsed - .21F) / .26F), dockMotion[1])) {
                    open = false;
                }
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
                QueueWheelBlur();
            }
        }
        void __stdcall RenderHUD()
        {
            const auto view = Read();
            static Overhead::Animation chatMotion, lootMotion;
            static std::uint64_t markerGeneration{};
            if (markerGeneration != view.generation) {
                chatMotion.Clear();
                lootMotion.Clear();
                markerGeneration = view.generation;
            }
            chatMotion.BeginFrame(Im::GetIO()->DeltaTime);
            lootMotion.BeginFrame(Im::GetIO()->DeltaTime);
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
                if (view.settings.loot.enabled && view.settings.loot.icons) {
                    for (const auto& marker : Loot::ReadMarkers()) {
                        const auto pose = lootMotion.Show(marker.actor);
                        const float size = 34 * scale;
                        const V p{marker.x * display.x, marker.y * display.y + pose.rise * size};
                        OrderVisuals::Loot(draw, p, size * pose.scale, marker.valuable,
                                           pose.alpha * marker.alpha);
                    }
                }
            }
            chatMotion.EndFrame();
            lootMotion.EndFrame();
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
            constexpr const char* titles[]{"Natural", "Lead", "Companion", "Rear", "At Ease", "Vanilla"};
            constexpr const char* details[]{"Travelling together", "Taking the lead", "By your side",
                                            "Watching your back",  "Resting here",    "Follower AI restored"};
            auto text = [&](const char* value, float yy, float size, Im::ImU32 color) {
                auto bounds = Im::CalcTextSize(value);
                float x = right - 86 * scale - bounds.x * size / Im::GetFontSize();
                Draw::AddText(draw, Im::GetFont(), size, {x + 1, yy + 2}, IM_COL32(0, 0, 0, 210 * alpha),
                              value);
                Draw::AddText(draw, Im::GetFont(), size, {x, yy}, color, value);
            };
            {
                Inscription font;
                text(titles[mode], y - 18 * scale, 20 * scale, IM_COL32(226, 218, 201, 255 * alpha));
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
                    else if (key == 1) {
                        --padStep;
                    }
                    else if (key == 2) {
                        ++padStep;
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
                padStep = 0;
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
        else if (action == 7) {
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
                case 1: {
                    auto value = c.Snapshot().settings;
                    value.enabled = !c.IsEnabled();
                    if (c.ApplyPreferences(value)) {
                        Engine::Notify(value.enabled ? "Walk With Me enabled" : "Walk With Me disabled");
                    }
                    else {
                        Engine::Notify("Walk With Me could not save the enable setting");
                    }
                    break;
                }
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
        padStep = 0;
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
        MF::AddSectionItem("Loot", RenderLoot);
        MF::AddSectionItem("Interface", RenderInterface);
        MF::AddSectionItem("Controls", RenderControls);
        commandWindow = MF::AddWindow(RenderCommands, true);
        packageWindow = MF::AddWindow(RenderPackageControl, true);
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

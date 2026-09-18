#include "imgui.h"
#include "order_wheel.h"
#include <iostream>
using namespace Wayfarer;
struct WheelUI
{
    int pressed = -1;
    bool legacy{};
    int Frame(float x, float y, bool down)
    {
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(x, y);
        io.AddMouseButtonEvent(0, down);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({500, 500 * Wheel::windowHeight});
        ImGui::Begin("wheel", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoNav);
        int over = Wheel::Mouse(x - 250, y - 240, 210), choice = -1;
        if (legacy) {
            if (!ImGui::IsWindowHovered()) {
                over = -1;
            }
            ImGui::SetCursorScreenPos({0, 20});
            ImGui::InvisibleButton("wheel-hit", {500, 435});
            if (ImGui::IsMouseClicked(0)) {
                pressed = over;
            }
            if (ImGui::IsMouseReleased(0)) {
                if (pressed >= 0 && pressed == over) {
                    choice = over;
                }
                pressed = -1;
            }
        }
        else {
            ImGui::SetCursorScreenPos({0, 20});
            const bool clicked = ImGui::InvisibleButton("wheel-hit", {500, 435});
            const bool activated = ImGui::IsItemActivated();
            if (!ImGui::IsItemHovered()) {
                over = -1;
            }
            if (activated) {
                pressed = over;
            }
            choice = clicked && pressed >= 0 && pressed == over ? over : -1;
            if (!ImGui::IsItemActive()) {
                pressed = -1;
            }
        }
        for (int index = 0; index < 2; ++index) {
            const auto box = Wheel::UtilityBox(index, 500);
            ImGui::SetCursorScreenPos({box.x, box.y});
            ImGui::PushID(index);
            if (ImGui::InvisibleButton("utility", {box.w, box.h})) {
                choice = 10 + index;
            }
            ImGui::PopID();
        }
        ImGui::End();
        ImGui::Render();
        return choice;
    }
};
int main()
{
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.DisplaySize = {1000, 1000};
    io.DeltaTime = 1.0f / 60;
    io.IniFilename = nullptr;
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    int failures = 0;
    auto check = [&](bool v, const char* msg) {
        if (!v) {
            std::cerr << msg << '\n';
            ++failures;
        }
    };
    WheelUI wheel;
    for (int mode = 0; mode < 6; ++mode) {
        const float x = 250 + std::cos(Wheel::Angle(mode)) * 160,
                    y = 240 + std::sin(Wheel::Angle(mode)) * 160;
        wheel.Frame(x, y, false);
        wheel.Frame(x, y, false);
        wheel.Frame(x, y, true);
        wheel.Frame(x, y, true);
        check(wheel.Frame(x, y, false) == mode, "click failed");
        check(wheel.Frame(x, y, false) == -1, "duplicate activation");
    }
    wheel.Frame(250, 80, false);
    wheel.Frame(250, 80, true);
    wheel.Frame(250, 80, true);
    check(wheel.Frame(250, 400, false) == -1, "drag across sectors should cancel");
    wheel.Frame(250, 240, false);
    wheel.Frame(250, 240, true);
    check(wheel.Frame(250, 240, false) == -1, "dead zone should not select");
    wheel.Frame(900, 900, false);
    wheel.Frame(900, 900, true);
    wheel.Frame(250, 80, true);
    check(wheel.Frame(250, 80, false) == -1, "outside press should not select");
    for (int index = 0; index < 2; ++index) {
        const auto box = Wheel::UtilityBox(index, 500);
        const float x = box.x + box.w / 2, y = box.y + box.h / 2;
        wheel.Frame(x, y, false);
        wheel.Frame(x, y, false);
        wheel.Frame(x, y, true);
        wheel.Frame(x, y, true);
        check(wheel.Frame(x, y, false) == 10 + index,
              "utility click must trigger its own action, not a radial order");
        check(wheel.Frame(x, y, false) == -1, "utility action must not repeat on release");
        wheel.Frame(250, 80, false);
        wheel.Frame(250, 80, true);
        check(wheel.Frame(x, y, false) == -1, "dragging from wheel to utility must cancel both actions");
        wheel.Frame(x, y, false);
        wheel.Frame(x, y, true);
        check(wheel.Frame(250, 80, false) == -1, "dragging from utility to wheel must cancel both actions");
    }
    wheel.legacy = true;
    wheel.Frame(250, 80, false);
    wheel.Frame(250, 80, true);
    wheel.Frame(250, 80, true);
    check(wheel.Frame(250, 80, false) == -1, "legacy failure was not reproduced");
    ImGui::DestroyContext();
    std::cout
        << "Real ImGui " << IMGUI_VERSION << ": " << failures
        << " failures; six mouse orders, drag/dead-zone/outside cancellation, and legacy bug reproduction.\n";
    return failures ? 1 : 0;
}

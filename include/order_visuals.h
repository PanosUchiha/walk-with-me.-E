#pragma once
#include "SKSEMenuFramework.h"
namespace Wayfarer::OrderVisuals
{
    void Icon(ImGuiMCP::ImDrawList* draw, ImGuiMCP::ImVec2 center, float size, int mode, float elapsed,
              float alpha = 1, bool bare = false);
    void Sector(ImGuiMCP::ImDrawList* draw, ImGuiMCP::ImVec2 center, float radius, int mode,
                ImGuiMCP::ImU32 color);
} // namespace Wayfarer::OrderVisuals

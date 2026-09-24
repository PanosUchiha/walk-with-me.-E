#pragma once
#include "SKSEMenuFramework.h"
namespace Wayfarer::OrderVisuals
{
    void Chat(ImGuiMCP::ImDrawList* draw, ImGuiMCP::ImVec2 center, float size, float alpha = 1);
    void Loot(ImGuiMCP::ImDrawList* draw, ImGuiMCP::ImVec2 center, float size, bool valuable,
              float alpha = 1);
    void Icon(ImGuiMCP::ImDrawList* draw, ImGuiMCP::ImVec2 center, float size, int mode, float elapsed,
              float alpha = 1, bool bare = false);
    void Sector(ImGuiMCP::ImDrawList* draw, ImGuiMCP::ImVec2 center, float radius, int mode,
                ImGuiMCP::ImU32 color);
} // namespace Wayfarer::OrderVisuals

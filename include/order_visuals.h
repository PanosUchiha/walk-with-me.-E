#pragma once
#include "SKSEMenuFramework.h"
namespace Wayfarer::OrderVisuals
{
    void Chat(ImGuiMCP::ImDrawList* draw, ImGuiMCP::ImVec2 center, float size, float alpha = 1);
    void Loot(ImGuiMCP::ImDrawList* draw, ImGuiMCP::ImVec2 center, float size, bool valuable,
              float alpha = 1);
    // The Pathfinder emblem (the scout on its menu tile) as an overhead marker.
    void PathfinderEmblem(ImGuiMCP::ImDrawList* draw, ImGuiMCP::ImVec2 center, float size, float alpha = 1);
    // `mode` is an order (0-6, FormationMode order; 6 shows the custom formation's emblem), 8 for
    // Teleport party or 9 for Pathfinder. `tint` recolours the
    // artwork; the party actions grid warms a focused icon toward yellow.
    void Icon(ImGuiMCP::ImDrawList* draw, ImGuiMCP::ImVec2 center, float size, int mode, float elapsed,
              float alpha = 1, bool bare = false, ImGuiMCP::ImU32 tint = IM_COL32(226, 218, 201, 255));
    // Line-art marks drawn from primitives, for actions that have no artwork of their own. Each
    // plays a short animation from `elapsed` = 0 (when its tile gains focus).
    enum class Glyph
    {
        Target, // Companion control: a bust inside a sighting ring.
        Link,   // Hold hands: two rings that slide together and interlock.
        Power,  // Walk With Me on or off.
        Formation // Formation editor: companion places around the player.
    };
    void DrawGlyph(ImGuiMCP::ImDrawList* draw, ImGuiMCP::ImVec2 center, float size, Glyph glyph, float elapsed,
                   ImGuiMCP::ImU32 color);
    // The emblem the player chose for their custom formation (a Custom::emblems index), shown for
    // order 6; any thread may set it.
    void SetCustomEmblem(int index);
    // One of the custom formation emblems, centred, `size` across.
    void Crest(ImGuiMCP::ImDrawList* draw, ImGuiMCP::ImVec2 center, float size, int index, float alpha = 1,
               ImGuiMCP::ImU32 tint = IM_COL32(226, 218, 201, 255));
    void Sector(ImGuiMCP::ImDrawList* draw, ImGuiMCP::ImVec2 center, float radius, int mode,
                ImGuiMCP::ImU32 color);
} // namespace Wayfarer::OrderVisuals

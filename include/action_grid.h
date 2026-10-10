#pragma once
#include "order_wheel.h"
#include <algorithm>
#include <array>
#include <cmath>

// The party menu's second page: the actions that used to sit under the wheel (teleport,
// temporary relaxation, companion control) and quick toggles. The top row holds the party
// actions, the row beneath the companion roles (hand holding, loot scouting, pathfinder) and
// the battle plan, which belongs to Fight With Me and stays dimmed without it. The wheel page
// ends in a translucent bar pointing down to it; the grid starts with one pointing back up.
// Pure layout, navigation and motion, shared by the game and the offline preview.
namespace Wayfarer::ActionGrid
{
    constexpr std::array<int, 2> rowSizes{4, 4};
    constexpr int rows = static_cast<int>(rowSizes.size()), count = 8, columns = 4;
    enum Tile : int
    {
        Teleport,
        TemporaryRest,
        CompanionControl,
        Guidance,
        HoldHands,
        LootScout,
        Pathfinder,
        BattlePlan
    };

    struct Box
    {
        float x, y, w, h;
    };
    constexpr int RowOf(int index) { return std::clamp(index, 0, count - 1) < rowSizes[0] ? 0 : 1; }
    constexpr int RowStart(int row) { return row == 0 ? 0 : rowSizes[0]; }
    // In the party window's own coordinates: width w, height w * Wheel::windowHeight. Every tile
    // is the same size; a shorter row is centred.
    constexpr Box TileBox(int index, float w)
    {
        index = std::clamp(index, 0, count - 1);
        const int row = RowOf(index), col = index - RowStart(row);
        const float gap = w * .026F, left = w * .05F, top = w * .215F;
        const float tw = (w * .90F - (columns - 1) * gap) / columns, th = w * .33F;
        const float indent = (columns - rowSizes[row]) * (tw + gap) * .5F;
        return {left + indent + col * (tw + gap), top + row * (th + gap), tw, th};
    }
    // Under the wheel, pointing down to the grid; above the grid, pointing back up.
    constexpr Box MoreBar(float w) { return {w * .06F, w * 1.03F, w * .88F, w * .085F}; }
    constexpr Box BackBar(float w) { return {w * .06F, w * .09F, w * .88F, w * .085F}; }

    // D-pad, arrow keys or stick on the grid. Left and right move along a row and stop at its
    // ends; up and down go to the nearest tile in the next row (the left one on a tie). Up from
    // the top row returns to the wheel (-1).
    inline int Move(int focus, int dx, int dy)
    {
        focus = std::clamp(focus, 0, count - 1);
        const int row = RowOf(focus), col = focus - RowStart(row);
        if (dy < 0 && row == 0) {
            return -1;
        }
        if (dy != 0) {
            const int target = std::clamp(row + (dy > 0 ? 1 : -1), 0, rows - 1);
            if (target == row) {
                return focus;
            }
            const auto here = TileBox(focus, 1);
            int best = RowStart(target);
            float bestDistance = 1e9F;
            for (int i = RowStart(target); i < RowStart(target) + rowSizes[target]; ++i) {
                const auto box = TileBox(i, 1);
                const float d = std::abs((box.x + box.w * .5F) - (here.x + here.w * .5F));
                if (d < bestDistance - 1e-4F) {
                    best = i;
                    bestDistance = d;
                }
            }
            return best;
        }
        return RowStart(row) + std::clamp(col + (dx > 0) - (dx < 0), 0, rowSizes[row] - 1);
    }

    // The right stick steps through the grid like the D-pad: a firm push moves one tile, and
    // holding it repeats after a pause. After a page change the stick is ignored until it has
    // come back near the centre, so the push that aimed the wheel never lands on a tile.
    struct StickSteps
    {
        int held{};   // 0 centred, -1 locked until centred, else the direction held (1-4).
        float wait{};
        void Lock() { held = -1; }
        std::array<int, 2> Update(float x, float y, float dt)
        {
            const float magnitude = std::hypot(x, y);
            if (!std::isfinite(magnitude) || magnitude < .35F) {
                held = 0;
                return {0, 0};
            }
            if (held < 0 || (held == 0 && magnitude < .6F)) {
                return {0, 0};
            }
            // y grows downward, as on screen.
            const bool across = std::abs(x) >= std::abs(y);
            const int dx = across ? (x > 0 ? 1 : -1) : 0, dy = across ? 0 : (y > 0 ? 1 : -1);
            const int code = across ? (dx > 0 ? 1 : 2) : (dy > 0 ? 3 : 4);
            if (code != held) {
                held = code;
                wait = .42F;
                return {dx, dy};
            }
            wait -= std::clamp(dt, 0.0F, .1F);
            if (wait <= 0) {
                wait = .16F;
                return {dx, dy};
            }
            return {0, 0};
        }
    };

    struct Motion
    {
        float page{};         // 0 shows the wheel, 1 the grid; eased between them.
        float gridElapsed{};  // Time on the grid; drives the staggered tile reveal.
        float focusElapsed{}; // Time on the focused tile; drives its icon animation.
        float frameX{}, frameY{}; // The selection frame glides between tiles (in window widths).
        int lastFocus{-1};
        bool gridTarget{};
        std::array<float, count> highlight{}, flash{}, toggle{};

        void Reset(int focus, bool grid, const std::array<bool, count>& on)
        {
            *this = {};
            page = grid ? 1.0F : 0.0F;
            gridTarget = grid;
            gridElapsed = grid ? 1.0F : 0.0F;
            focus = std::clamp(focus, 0, count - 1);
            frameX = TileBox(focus, 1).x;
            frameY = TileBox(focus, 1).y;
            lastFocus = focus;
            highlight[focus] = 1;
            for (int i = 0; i < count; ++i) {
                toggle[i] = on[i] ? 1.0F : 0.0F;
            }
        }
        void Press(int tile)
        {
            if (tile >= 0 && tile < count) {
                flash[tile] = 1;
            }
        }
        void Update(float dt, bool grid, int focus, const std::array<bool, count>& on)
        {
            if (!std::isfinite(dt) || dt <= 0) {
                return;
            }
            dt = std::min(dt, .1F);
            focus = std::clamp(focus, 0, count - 1);
            page = Wheel::Follow(page, grid ? 1.0F : 0.0F, dt, 11);
            if (std::abs(page - (grid ? 1.0F : 0.0F)) < .001F) {
                page = grid ? 1.0F : 0.0F;
            }
            // Each arrival replays the reveal; leaving keeps the tiles up so they fade with the page.
            if (grid && !gridTarget) {
                gridElapsed = 0;
            }
            gridTarget = grid;
            gridElapsed += dt;
            focusElapsed = focus == lastFocus ? focusElapsed + dt : 0;
            lastFocus = focus;
            frameX = Wheel::Follow(frameX, TileBox(focus, 1).x, dt, 20);
            frameY = Wheel::Follow(frameY, TileBox(focus, 1).y, dt, 20);
            for (int i = 0; i < count; ++i) {
                highlight[i] = Wheel::Follow(highlight[i], i == focus ? 1.0F : 0.0F, dt, 16);
                flash[i] = Wheel::Follow(flash[i], 0, dt, 7);
                toggle[i] = Wheel::Follow(toggle[i], on[i] ? 1.0F : 0.0F, dt, 16);
            }
        }
        // Tiles rise in one after another, left to right and top to bottom.
        float Reveal(int index) const { return Wheel::EaseOut((gridElapsed - .06F - index * .03F) / .26F); }
        bool OnGrid() const { return page > .5F; }
        // A staggered crossfade: the wheel clears before the grid is fully in, so the two never
        // sit on top of each other at half strength.
        float WheelVisibility() const { return 1 - Wheel::Smooth(page / .65F); }
        float GridVisibility() const { return Wheel::Smooth((page - .2F) / .8F); }
    };
} // namespace Wayfarer::ActionGrid

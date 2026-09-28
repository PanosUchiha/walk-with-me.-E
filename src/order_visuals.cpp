#include "order_visuals.h"
#include "custom_formation.h"
#include "order_wheel.h"
#include <atomic>
namespace Wayfarer::OrderVisuals
{
    namespace
    {
        namespace Im = ImGuiMCP;
        namespace D = Im::ImDrawListManager;
        using V = Im::ImVec2;
        enum Asset
        {
            Foot,
            Arrow,
            WingA,
            WingB,
            Shield,
            Logs,
            Flame,
            Ember,
            Teleport,
            ChatBubble,
            LootChest,
            Scout
        };
        constexpr Im::ImU32 ivory = IM_COL32(226, 218, 201, 255);
        Im::ImU32 iconTint = ivory; // Set by Icon() for its layers; 0 below means "use it".
        std::atomic<int> customEmblem{Custom::defaultEmblem};
        void Layer(Im::ImDrawList* draw, V center, float size, Asset asset, float alpha, float dx = 0,
                   float dy = 0, float rotation = 0, float sx = 1, float sy = 1, V uv0 = {0, 0},
                   V uv1 = {1, 1}, Im::ImU32 tint = 0)
        {
            if (!tint) {
                tint = iconTint;
            }
            static const auto textures = [] {
                std::array<Im::ImTextureID, 12> value{};
                constexpr const char* names[]{"natural", "arrow", "wing-a", "wing-b", "shield", "logs",
                                              "flame",   "ember", "teleport", "chat", "loot",  "pathfinder"};
                for (int i = 0; i < 12; ++i) {
                    value[i] = SKSEMenuFramework::LoadTexture(
                        std::string("Data/Interface/WalkWithMe/") + names[i] + ".svg", {256, 256});
                }
                return value;
            }();
            if (!textures[asset] || alpha <= 0) {
                return;
            }
            const float c = std::cos(rotation), s = std::sin(rotation);
            auto point = [&](V uv) {
                float x = (uv.x - .5F) * size * sx, y = (uv.y - .5F) * size * sy;
                return V{center.x + dx * size + x * c - y * s, center.y + dy * size + x * s + y * c};
            };
            const auto opacity = static_cast<Im::ImU32>(255 * std::clamp(alpha, 0.0F, 1.0F));
            D::AddImageQuad(draw, textures[asset], point(uv0), point({uv1.x, uv0.y}), point(uv1),
                            point({uv0.x, uv1.y}), uv0, {uv1.x, uv0.y}, uv1, {uv0.x, uv1.y},
                            (tint & ~IM_COL32_A_MASK) | (opacity << IM_COL32_A_SHIFT));
        }
    } // namespace
    void Chat(Im::ImDrawList* draw, V center, float size, float alpha)
    {
        Layer(draw, center, size * 1.08F, ChatBubble, alpha * .45F, 0, .03F, 0, 1, 1, {0, 0}, {1, 1},
              IM_COL32(0, 0, 0, 255));
        Layer(draw, center, size, ChatBubble, alpha);
    }
    void Loot(Im::ImDrawList* draw, V center, float size, bool valuable, float alpha)
    {
        // A soft shadow keeps the white chest legible against snow and bright skies.
        Layer(draw, center, size * 1.08F, LootChest, alpha * .45F, 0, .03F, 0, 1, 1, {0, 0}, {1, 1},
              IM_COL32(0, 0, 0, 255));
        Layer(draw, center, size, LootChest, alpha, 0, 0, 0, 1, 1, {0, 0}, {1, 1},
              valuable ? IM_COL32(240, 192, 72, 255) : IM_COL32(250, 248, 242, 255));
    }
    void PathfinderEmblem(Im::ImDrawList* draw, V center, float size, float alpha)
    {
        // Drawn like the loot chest: the same shadow under the same near-white.
        Layer(draw, center, size * 1.08F, Scout, alpha * .45F, 0, .03F, 0, 1, 1, {0, 0}, {1, 1},
              IM_COL32(0, 0, 0, 255));
        Layer(draw, center, size, Scout, alpha, 0, 0, 0, 1, 1, {0, 0}, {1, 1}, IM_COL32(250, 248, 242, 255));
    }
    void Sector(Im::ImDrawList* draw, V center, float radius, int mode, Im::ImU32 color)
    {
        static const auto texture =
            SKSEMenuFramework::LoadTexture("Data/Interface/WalkWithMe/wheel-slice.svg", {768, 768});
        if (!texture) {
            return;
        }
        const float angle =
            Wheel::slots[std::clamp(mode, 0, Wheel::count - 1)] * Wheel::sectorDegrees * Wheel::pi / 180;
        const float c = std::cos(angle), s = std::sin(angle), size = radius * 512 / 240;
        auto point = [&](float u, float v) {
            float x = (u - .5F) * size, y = (v - .5F) * size;
            return V{center.x + x * c - y * s, center.y + x * s + y * c};
        };
        D::AddImageQuad(draw, texture, point(0, 0), point(1, 0), point(1, 1), point(0, 1), {0, 0}, {1, 0},
                        {1, 1}, {0, 1}, color);
    }
    void Icon(Im::ImDrawList* draw, V p, float size, int mode, float elapsed, float alpha, bool bare,
              Im::ImU32 tint)
    {
        struct Tint
        {
            explicit Tint(Im::ImU32 value) { iconTint = value; }
            ~Tint() { iconTint = ivory; }
        } scope(tint);
        const float t = std::max(0.0F, elapsed);
        auto pulse = [](float t, float duration) {
            return t >= duration ? 0.0F : std::sin(Wheel::pi * std::clamp(t / duration, 0.0F, 1.0F));
        };
        switch (mode) {
        case 6: {
            // The player's own formation wears the emblem they chose; it lifts and settles on focus.
            const float q = 1 - Wheel::EaseOut(t / .45F);
            Crest(draw, {p.x, p.y - size * .05F * q}, size * (.84F + .05F * q), customEmblem.load(), alpha,
                  iconTint);
            break;
        }
        case 9: {
            // Pathfinder's scout strides: a few steps' bob, then still.
            const float q = t < 1.2F ? std::abs(std::sin(t * 7.5F)) * (1 - t / 1.2F) : 0;
            Layer(draw, p, size * .9F, Scout, alpha, .015F * q, -.03F * q);
            break;
        }
        case 8: {
            const float q = std::sin(t * 2.4F);
            Layer(draw, p, size * .92F, Teleport, alpha * (.93F + .07F * q), 0, -.014F * q, 0, 1,
                  1 + .025F * q);
            break;
        }
        case 0: {
            constexpr std::array<V, 4> lo{{{0, 0}, {0, .31F}, {.5F, .30F}, {.5F, .68F}}};
            constexpr std::array<V, 4> hi{{{.5F, .32F}, {.5F, .66F}, {1, .68F}, {1, 1}}};
            for (int i = 0; i < 4; ++i) {
                float q = Wheel::Smooth((t - (3 - i) * .4F) / .8F);
                Layer(draw, p, size, Foot, alpha * (.35F + .65F * q), 0, .025F * (1 - q), 0, 1, 1, lo[i],
                      hi[i]);
            }
            break;
        }
        case 1: {
            const float q = pulse(t, 2.8F), angle = -Wheel::pi / 2;
            Layer(draw, p, size * .86F, WingA, alpha * (1 - .2F * q), 0, 0, angle - .18F * q);
            Layer(draw, p, size * .86F, WingB, alpha * (1 - .2F * q), 0, 0, angle + .18F * q);
            Layer(draw, p, size * .86F, Arrow, alpha, .12F * q, -.12F * q, angle);
            break;
        }
        case 2: {
            float q = 1 - Wheel::Smooth(t / 1.5F);
            V left{p.x - size * (.15F + .055F * q), p.y + size * .025F};
            V right{p.x + size * (.15F + .055F * q), p.y - size * .025F};
            Layer(draw, left, size * .65F, Shield, alpha * .78F, 0, 0, -.18F * q);
            if (!bare) {
                D::AddCircleFilled(draw, right, size * .285F, IM_COL32(20, 28, 29, 255 * alpha), 40);
            }
            Layer(draw, right, size * .65F, Shield, alpha, 0, 0, .18F * q);
            break;
        }
        case 3: {
            const float q = pulse(t, 1.5F);
            Layer(draw, p, size * .87F, Shield, alpha, -.025F * q, .02F * q, -.12F * q,
                  .82F + .18F * Wheel::Smooth(t / 1.5F));
            if (t > .2F && t < 1.9F) {
                float u = (t - .2F) / 1.7F;
                float x = u < .5F ? .55F - .78F * u : .16F + .7F * (u - .5F);
                float y = u < .5F ? -.55F + .78F * u : -.16F + .8F * (u - .5F);
                Layer(draw, {p.x + x * size, p.y + y * size}, size * .22F, Arrow,
                      alpha * std::sin(Wheel::pi * u), 0, 0, Wheel::pi * (.5F + std::max(0.0F, u - .5F)));
            }
            break;
        }
        case 5: {

            const float q = 1 - Wheel::Smooth(t / .7F);
            Layer(draw, p, size * .85F, Shield, alpha * (1 - .2F * q), 0, .04F * q);
            break;
        }
        default: {
            float q = t < 2.8F ? std::sin(t * 8) * std::sin(Wheel::pi * t / 2.8F) : 0;
            Layer(draw, p, size * .88F, Flame, alpha * (.94F + .06F * q), 0, -.02F * q, .025F * q,
                  1 + .025F * q, 1 + .07F * q);
            for (int i = 0; i < 2; ++i) {
                float u = (t - i * .4F) / 2.6F;
                if (u > 0 && u < 1) {
                    Layer(draw, {p.x + size * (i == 0 ? -.05F : .16F), p.y + size * (.10F - .5F * u)},
                          size * (.22F - .1F * u), Ember, alpha * std::sin(Wheel::pi * u));
                }
            }
            Layer(draw, p, size * .88F, Logs, alpha);
            break;
        }
        }
    }

    void DrawGlyph(Im::ImDrawList* draw, V p, float size, Glyph glyph, float elapsed, Im::ImU32 color)
    {
        const float t = std::max(0.0F, elapsed);
        const float stroke = std::max(1.5F, size * .07F);
        auto at = [&](float x, float y) { return V{p.x + x * size, p.y + y * size}; };
        switch (glyph) {
        case Glyph::Target: {
            // The sighting ring turns into place on focus, a quarter of a turn.
            const float spin = (1 - Wheel::EaseOut(t / .6F)) * Wheel::pi / 4;
            for (int i = 0; i < 4; ++i) {
                const float mid = spin + Wheel::pi / 4 + i * Wheel::pi / 2;
                D::PathArcTo(draw, p, size * .40F, mid - .52F, mid + .52F, 12);
                D::PathStroke(draw, color, 0, stroke);
                const float tick = spin + i * Wheel::pi / 2;
                D::AddLine(draw, at(std::cos(tick) * .33F, std::sin(tick) * .33F),
                           at(std::cos(tick) * .49F, std::sin(tick) * .49F), color, stroke);
            }
            D::AddCircleFilled(draw, at(0, -.10F), size * .10F, color, 24);
            D::PathArcTo(draw, at(0, .215F), size * .185F, Wheel::pi, 2 * Wheel::pi, 20);
            D::PathFillConvex(draw, color);
            break;
        }
        case Glyph::Link: {
            // Two rings slide together and interlock: each dips under the other once.
            const float r = .22F, d = .14F + .10F * (1 - Wheel::EaseOut(t / .5F));
            for (int side = -1; side <= 1; side += 2) {
                const V c = at(side * d, 0);
                if (d >= r) {
                    D::AddCircle(draw, c, size * r, color, 40, stroke);
                    continue;
                }
                // Where this ring meets the other: the left ring passes under at the bottom
                // crossing, the right one under at the top.
                const float y = std::sqrt(r * r - d * d) * (side < 0 ? 1.0F : -1.0F);
                const float gap = std::atan2(y, static_cast<float>(-side) * d);
                D::PathArcTo(draw, c, size * r, gap + .30F, gap + 2 * Wheel::pi - .30F, 40);
                D::PathStroke(draw, color, 0, stroke);
            }
            break;
        }
        case Glyph::Power: {
            // The ring draws itself in on focus.
            const float start = -Wheel::pi / 2 + .62F, full = 2 * Wheel::pi - 1.24F;
            D::PathArcTo(draw, p, size * .30F, start, start + full * (.2F + .8F * Wheel::EaseOut(t / .5F)), 36);
            D::PathStroke(draw, color, 0, stroke);
            D::AddLine(draw, at(0, -.42F), at(0, -.06F), color, stroke);
            break;
        }
        }
    }

    void SetCustomEmblem(int index)
    {
        customEmblem = std::clamp(index, 0, Custom::emblemCount - 1);
    }

    void Crest(Im::ImDrawList* draw, V center, float size, int index, float alpha, Im::ImU32 tint)
    {
        static const auto textures = [] {
            std::array<Im::ImTextureID, Custom::emblemCount> value{};
            for (int i = 0; i < Custom::emblemCount; ++i) {
                value[i] = SKSEMenuFramework::LoadTexture(
                    std::string("Data/Interface/WalkWithMe/crest-") + Custom::emblems[i].key + ".svg", {256, 256});
            }
            return value;
        }();
        const auto texture = textures[std::clamp(index, 0, Custom::emblemCount - 1)];
        if (!texture || alpha <= 0) {
            return;
        }
        const float h = size * .5F;
        const auto opacity = static_cast<Im::ImU32>(255 * std::clamp(alpha, 0.0F, 1.0F));
        D::AddImageQuad(draw, texture, {center.x - h, center.y - h}, {center.x + h, center.y - h},
                        {center.x + h, center.y + h}, {center.x - h, center.y + h}, {0, 0}, {1, 0}, {1, 1}, {0, 1},
                        (tint & ~IM_COL32_A_MASK) | (opacity << IM_COL32_A_SHIFT));
    }
} // namespace Wayfarer::OrderVisuals

#include "order_visuals.h"
#include "order_wheel.h"
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
            Teleport
        };
        void Layer(Im::ImDrawList* draw, V center, float size, Asset asset, float alpha, float dx = 0,
                   float dy = 0, float rotation = 0, float sx = 1, float sy = 1, V uv0 = {0, 0},
                   V uv1 = {1, 1})
        {
            static const auto textures = [] {
                std::array<Im::ImTextureID, 9> value{};
                constexpr const char* names[]{"natural", "arrow", "wing-a", "wing-b",  "shield",
                                              "logs",    "flame", "ember",  "teleport"};
                for (int i = 0; i < 9; ++i) {
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
            D::AddImageQuad(draw, textures[asset], point(uv0), point({uv1.x, uv0.y}), point(uv1),
                            point({uv0.x, uv1.y}), uv0, {uv1.x, uv0.y}, uv1, {uv0.x, uv1.y},
                            IM_COL32(226, 218, 201, 255 * std::clamp(alpha, 0.0F, 1.0F)));
        }
    } // namespace
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
    void Icon(Im::ImDrawList* draw, V p, float size, int mode, float elapsed, float alpha, bool bare)
    {
        const float t = std::max(0.0F, elapsed);
        auto pulse = [](float t, float duration) {
            return t >= duration ? 0.0F : std::sin(Wheel::pi * std::clamp(t / duration, 0.0F, 1.0F));
        };
        switch (mode) {
        case 6: {
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
} // namespace Wayfarer::OrderVisuals

#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace Wayfarer::HandHolding::IK
{
    struct Point
    {
        float x{}, y{}, z{};
        Point operator+(Point b) const { return {x + b.x, y + b.y, z + b.z}; }
        Point operator-(Point b) const { return {x - b.x, y - b.y, z - b.z}; }
        Point operator*(float s) const { return {x * s, y * s, z * s}; }
    };
    inline float Dot(Point a, Point b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
    inline Point Cross(Point a, Point b)
    {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    inline float Length(Point a)
    {
        return std::sqrt(Dot(a, a));
    }
    inline bool Finite(Point a)
    {
        return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
    }
    inline Point Unit(Point a, Point fallback = {1, 0, 0})
    {
        const float n = Length(a);
        return n > 1e-5F ? a * (1 / n) : fallback;
    }

    inline std::optional<Point> SharedTarget(Point a, float ra, Point b, float rb, Point preferred)
    {
        if (!Finite(a) || !Finite(b) || !Finite(preferred) || !std::isfinite(ra) || !std::isfinite(rb) ||
            ra <= 0 || rb <= 0) {
            return {};
        }
        const auto offset = b - a;
        const float distance = Length(offset);
        if (distance > ra + rb) {
            return {};
        }
        auto project = [](Point p, Point center, float radius) {
            const auto d = p - center;
            const float n = Length(d);
            return n > radius ? center + d * (radius / n) : p;
        };
        const auto pa = project(preferred, a, ra);
        if (Length(pa - b) <= rb) {
            return pa;
        }
        const auto pb = project(preferred, b, rb);
        if (Length(pb - a) <= ra) {
            return pb;
        }
        if (distance < 1e-5F) {
            return project(preferred, a, std::min(ra, rb));
        }
        const auto axis = offset * (1 / distance);
        const float along = (ra * ra - rb * rb + distance * distance) / (2 * distance);
        const auto center = a + axis * along;
        const float radius = std::sqrt(std::max(0.0F, ra * ra - along * along));
        auto radial = preferred - center;
        radial = radial - axis * Dot(radial, axis);
        const auto fallback = Cross(axis, std::abs(axis.z) < .9F ? Point{0, 0, 1} : Point{0, 1, 0});
        return center + Unit(radial, Unit(fallback)) * radius;
    }
    struct Matrix
    {
        float m[3][3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    };

    inline std::optional<Matrix> WristAlignment(Point along, Point across, bool left, Point desiredPalm)
    {
        if (!Finite(along) || !Finite(across) || !Finite(desiredPalm) || Length(along) < 1e-5F) {
            return {};
        }
        along = Unit(along);
        auto palm = Cross(across, along) * (left ? -1.0F : 1.0F);
        desiredPalm.z = 0;
        if (Length(palm) < 1e-5F || Length(desiredPalm) < 1e-5F) {
            return {};
        }
        palm = Unit(palm);
        desiredPalm = Unit(desiredPalm);
        const Point down{0, 0, -1};
        const Point source[3]{along, palm, Cross(along, palm)};
        const Point target[3]{down, desiredPalm, Cross(down, desiredPalm)};
        Matrix result;
        for (int i = 0; i < 3; ++i) {
            const float row[3]{i == 0   ? target[0].x
                               : i == 1 ? target[0].y
                                        : target[0].z,
                               i == 0   ? target[1].x
                               : i == 1 ? target[1].y
                                        : target[1].z,
                               i == 0   ? target[2].x
                               : i == 1 ? target[2].y
                                        : target[2].z};
            result.m[i][0] = row[0] * source[0].x + row[1] * source[1].x + row[2] * source[2].x;
            result.m[i][1] = row[0] * source[0].y + row[1] * source[1].y + row[2] * source[2].y;
            result.m[i][2] = row[0] * source[0].z + row[1] * source[1].z + row[2] * source[2].z;
        }
        return result;
    }
    inline Matrix Rotation(float angle, Point axis)
    {
        axis = Unit(axis);
        const float x = axis.x, y = axis.y, z = axis.z;
        const float c = std::cos(angle), s = std::sin(angle), v = 1 - c;
        return {{{c + x * x * v, x * y * v - z * s, x * z * v + y * s},
                 {y * x * v + z * s, c + y * y * v, y * z * v - x * s},
                 {z * x * v - y * s, z * y * v + x * s, c + z * z * v}}};
    }
    struct Quaternion
    {
        float w{1}, x{}, y{}, z{};
    };
    inline Quaternion FromMatrix(const Matrix& matrix)
    {
        const auto& m = matrix.m;
        Quaternion q;
        const float trace = m[0][0] + m[1][1] + m[2][2];
        if (trace > 0) {
            const float s = 2 * std::sqrt(trace + 1);
            q = {s * .25F, (m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s};
        }
        else {
            const int i = m[0][0] > m[1][1] ? (m[0][0] > m[2][2] ? 0 : 2) : (m[1][1] > m[2][2] ? 1 : 2);
            const int j = (i + 1) % 3, k = (j + 1) % 3;
            const float s = 2 * std::sqrt(std::max(0.0F, 1 + m[i][i] - m[j][j] - m[k][k]));
            if (s < 1e-6F) {
                return {};
            }
            float v[3]{};
            v[i] = s * .25F;
            v[j] = (m[j][i] + m[i][j]) / s;
            v[k] = (m[k][i] + m[i][k]) / s;
            q = {(m[k][j] - m[j][k]) / s, v[0], v[1], v[2]};
        }
        return q;
    }
    inline Matrix BlendRotation(const Matrix& a, const Matrix& b, float weight)
    {
        const auto qa = FromMatrix(a);
        auto qb = FromMatrix(b);
        if (qa.w * qb.w + qa.x * qb.x + qa.y * qb.y + qa.z * qb.z < 0) {
            qb.w = -qb.w;
            qb.x = -qb.x;
            qb.y = -qb.y;
            qb.z = -qb.z;
        }
        const float t = std::clamp(weight, 0.0F, 1.0F);
        Quaternion q{qa.w + (qb.w - qa.w) * t, qa.x + (qb.x - qa.x) * t, qa.y + (qb.y - qa.y) * t,
                     qa.z + (qb.z - qa.z) * t};
        const float length = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
        if (!std::isfinite(length) || length < 1e-6F) {
            return a;
        }
        const float w = q.w / length, x = q.x / length, y = q.y / length, z = q.z / length;
        return {{{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
                 {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
                 {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}}};
    }
    struct Solution
    {
        Point elbow, hand;
    };

    inline std::optional<Solution> Solve(Point shoulder, float upper, float lower, Point target, Point pole)
    {
        if (!Finite(shoulder) || !Finite(target) || !Finite(pole) || !std::isfinite(upper) ||
            !std::isfinite(lower) || upper < 1 || lower < 1) {
            return {};
        }
        const auto delta = target - shoulder;
        const float d = Length(delta);
        if (d <= std::abs(upper - lower) + .01F || d >= upper + lower - .01F) {
            return {};
        }
        const auto axis = delta * (1 / d);
        auto bend = pole - shoulder;
        bend = bend - axis * Dot(bend, axis);
        if (Length(bend) < 1e-4F) {
            bend = Cross(axis, std::abs(axis.z) < .9F ? Point{0, 0, 1} : Point{0, 1, 0});
        }
        bend = Unit(bend);
        const float along = (upper * upper - lower * lower + d * d) / (2 * d);
        const float height = std::sqrt(std::max(0.0F, upper * upper - along * along));
        return Solution{shoulder + axis * along + bend * height, target};
    }
} // namespace Wayfarer::HandHolding::IK

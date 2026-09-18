#include "formation_math.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
    bool Near(float a_left, float a_right, float a_tolerance = 0.01F)
    {
        return std::abs(a_left - a_right) <= a_tolerance;
    }

    void Require(bool a_condition, const char* a_message)
    {
        if (!a_condition) {
            std::cerr << "FAILED: " << a_message << '\n';
            std::exit(1);
        }
    }
} // namespace

int main()
{
    using namespace Wayfarer;

    const auto north = ForwardFromYaw(0.0F);
    Require(Near(north.x, 0.0F) && Near(north.y, 1.0F), "zero yaw faces positive Y");

    const auto east = ForwardFromYaw(3.14159265F * 0.5F);
    Require(Near(east.x, 1.0F) && Near(east.y, 0.0F), "quarter-turn yaw faces positive X");

    const auto fallback = Normalize({});
    Require(Near(fallback.x, 0.0F) && Near(fallback.y, 1.0F),
            "zero velocity has a finite normalized fallback");
    const auto forward = Normalize({13.0F, -7.0F});
    const auto right = RightFromForward(forward);
    Require(Near(forward.x * right.x + forward.y * right.y, 0.0F), "travel axes stay orthogonal");

    const auto smoothed = SmoothDirection({0.0F, 1.0F}, {1.0F, 0.0F}, 0.1F, 5.0F);
    Require(smoothed.x > 0.0F && smoothed.x < 1.0F && smoothed.y > 0.0F,
            "direction smoothing avoids instant turns");

    std::cout << "Wayfarer formation math tests passed.\n";
    return 0;
}

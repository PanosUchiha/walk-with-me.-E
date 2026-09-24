#include "overhead_icons.h"
#include <cstdlib>
#include <iostream>
#include <limits>

void Check(bool ok, const char* message)
{
    if (!ok) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main()
{
    using namespace Wayfarer::Overhead;
    Animation motion;
    motion.BeginFrame(1.0F / 60);
    auto first = motion.Show(10);
    Check(first.alpha == 0 && first.scale < 1 && first.rise > 0, "new marker rises and fades in");
    motion.EndFrame();
    for (int i = 0; i < 60; ++i) {
        motion.BeginFrame(1.0F / 60);
        auto visible = motion.Show(10);
        Check(std::isfinite(visible.scale) && visible.alpha >= 0 && visible.alpha < .85F,
              "animation stays finite and slightly transparent");
        motion.EndFrame();
    }
    auto settled = motion.Show(10);
    Check(std::abs(settled.scale - 1) < .001F && std::abs(settled.rise) < .06F,
          "popup settles at full size with a small float");
    motion.BeginFrame(.016F);
    Check(motion.Show(20).alpha == 0, "a second companion gets an independent entrance");
    Check(motion.Show(10).alpha > .7F, "snapshot reordering does not replay the first companion");
    motion.EndFrame();
    auto still = motion.Show(10, false);
    Check(still.scale == 1 && still.rise == 0 && still.alpha == .8F,
          "disabling chat motion retains a still translucent icon");
    motion.BeginFrame(.016F);
    motion.Show(20);
    motion.EndFrame();
    Check(motion.Show(10).alpha == 0, "removed, hidden or disabled markers restart on return");
    motion.Clear();
    Check(motion.Show(20).alpha == 0, "load or menu reset drops previous animations");
    motion.BeginFrame(std::numeric_limits<float>::quiet_NaN());
    Check(std::isfinite(motion.Show(20).alpha), "invalid frame time cannot poison animation");

    auto atOneSecond = [](int fps) {
        Animation animation;
        animation.Show(1);
        for (int i = 0; i < fps; ++i) {
            animation.BeginFrame(1.0F / fps);
            animation.Show(1);
            animation.EndFrame();
        }
        return animation.Show(1);
    };
    const auto slow = atOneSecond(30), fast = atOneSecond(144);
    Check(std::abs(slow.rise - fast.rise) < .0001F && std::abs(slow.alpha - fast.alpha) < .0001F,
          "30 and 144 FPS produce the same motion over time");
}

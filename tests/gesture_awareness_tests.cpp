#include "gesture_awareness.h"
#include <array>
#include <iostream>
#include <limits>

int main()
{
    using namespace Wayfarer::CommandGesture;
    int failures{};
    auto check = [&](bool ok, const char* message) {
        if (!ok) {
            ++failures;
            std::cerr << message << '\n';
        }
    };
    auto glance = [](std::initializer_list<FollowerBearing> followers, int side = 1) {
        return GlanceDegrees(std::span(followers.begin(), followers.size()), side);
    };
    check(glance({}) == 0, "empty party does not turn the head");
    check(glance({{0, 100}, {0, 300}}) == 0, "followers directly ahead do not prompt a glance");
    check(glance({{100, 0}}) == 8 && glance({{-100, 0}}) == -8,
          "alongside glances stay subtle and mirror correctly");
    check(glance({{-80, -180}, {-50, -200}, {200, 0}}) == -28, "rear majority beats a follower alongside");
    check(glance({{80, -180}, {50, -200}, {-200, 0}}) == 28, "rear majority works on either side");
    check(glance({{0, -200}}, -1) == -28 && glance({{0, -200}}, 1) == 28,
          "directly behind uses a stable preferred side");
    check(glance({{-80, -180}, {80, -180}}, -1) == -28,
          "symmetric rear group uses a deterministic tie break");
    check(glance({{-100, 0}, {100, 0}}) == 0, "balanced alongside group stays neutral");
    check(glance({{-100, -150}, {50, 200}, {60, 220}}) < 8,
          "one rear follower does not create a strong glance");
    check(glance({{-100, -150}, {-50, -200}, {800, 0}}) == -28,
          "a distant party member does not dominate the vote");
    check(glance({{100, 0}, {-500, -1000}}) == 8, "out-of-range followers are ignored");
    check(glance({{0, 0}, {0, 10}, {std::numeric_limits<float>::quiet_NaN(), 0}}) == 0,
          "invalid and overlapping positions are ignored");
    for (float duration : {1.2F, 1.5F, 1.8F, 2.4F}) {
        check(GlanceWeight(0, duration) == 0 && GlanceWeight(duration, duration) == 0,
              "head starts and ends neutral");
        check(GlanceWeight(.5F, duration) == 1, "head reaches its intended glance during the gesture");
        float previous{};
        for (float time = 0; time <= duration + .1F; time += 1.0F / 60) {
            const float current = GlanceWeight(time, duration);
            check(current >= 0 && current <= 1, "head blend stays bounded");
            check(std::abs(current - previous) < .1F, "head blends without frame jumps");
            previous = current;
        }
    }
    check(GlanceWeight(0, 0) == 0 && GlanceWeight(std::numeric_limits<float>::infinity(), 2) == 0,
          "invalid timing cannot turn the head");
    return failures ? 1 : 0;
}

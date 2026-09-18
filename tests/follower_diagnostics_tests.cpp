#include "follower_diagnostics.h"
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace Wayfarer::Diagnostics;
void Check(bool value, const char* reason)
{
    if (!value) {
        std::cerr << reason << '\n';
        std::exit(1);
    }
}
int main()
{
    Recorder r;
    std::vector<std::string> events;
    auto emit = [&](unsigned, std::string_view kind, double, std::string_view text) {
        events.emplace_back(std::string(kind) + ":" + std::string(text));
    };
    double t = 100;
    auto sample = [&](bool travel, bool blocked, float step, float forward, bool arrived = false,
                      bool valid = true) {
        t += .2;
        r.Sample({t, .2F, step, forward, travel, blocked, !arrived, arrived, valid}, "sample", emit);
    };
    for (int i = 0; i < 60; ++i) {
        sample(false, false, 0, 0);
    }
    Check(!r.active, "ordinary rest must not create incidents");
    for (int i = 0; i < 20; ++i) {
        sample(true, false, 0, 0, false, false);
    }
    Check(!r.active, "unloaded/combat observations cannot trigger a travel freeze");
    sample(true, true, 0, 0);
    Check(r.active && r.sequence == 1, "blocked movement starts one incident");
    for (int i = 0; i < 30; ++i) {
        sample(true, true, 0, 0);
    }
    Check(r.sequence == 1, "repeated blocking stays in the same incident");
    for (int i = 0; i < 20; ++i) {
        sample(true, false, 8, 0);
    }
    Check(r.active, "sideways movement or package ownership cannot prove recovery");
    for (int i = 0; i < 8; ++i) {
        sample(true, false, 8, 8);
    }
    t += 20;
    sample(true, false, 8, 8);
    Check(r.active, "pause breaks continuity of recovered movement");
    for (int i = 0; i < 12; ++i) {
        sample(true, false, 8, 8);
    }
    Check(!r.active, "sustained forward motion resolves the incident");
    Check(events.back() == "end:sustained-forward-motion", "resolved outcome is explicit");
    for (int i = 0; i < 20; ++i) {
        sample(true, false, 0, 0);
    }
    Check(r.active && r.sequence == 2, "no-progress triggers without a blocked package");
    for (int i = 0; i < 20; ++i) {
        sample(false, false, 0, 0);
    }
    Check(r.active, "three-second recovery release must not end the incident");
    for (int i = 0; i < 40; ++i) {
        sample(false, false, 0, 0);
    }
    Check(!r.active && events.back() == "end:context-ended-unresolved",
          "rest/context change is not called recovered");
    sample(true, true, 0, 0);
    for (int i = 0; i < 12; ++i) {
        sample(true, false, 0, 0, true);
    }
    Check(!r.active && events.back() == "end:stable-arrival", "stable arrival resolves");
    for (int i = 0; i < 1000; ++i) {
        r.Event(t, "bounded", emit);
    }
    Check(r.history.size() <= 256, "history is bounded under event storms");
    t += 11;
    r.Event(t, "new", emit);
    Check(r.history.size() == 1, "history expires by observation time");
    Recorder persistent;
    events.clear();
    for (int i = 0; i < 3000; ++i) {
        t += .2;
        persistent.Sample({t, .2F, 0, 0, true, true, true, false, true}, "sample", emit);
    }
    Check(events.size() < 700, "persistent incidents throttle after the initial burst");
    std::cout << "Follower diagnostic lifecycle, recovery, pause, retention and rate tests passed\n";
}

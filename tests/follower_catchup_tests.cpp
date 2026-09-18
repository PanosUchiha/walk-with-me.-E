#include "follower_catchup.h"
#include "order_wheel.h"
#include "party_recall.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace Wayfarer;
void Check(bool v, const char* message)
{
    if (!v) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
int main()
{
    PartyRecallRequest request;
    Check(request.Request(4, 1000), "explicit teleport can be queued");
    Check(!request.Request(4, 1001), "double click cannot queue a duplicate teleport");
    Check(request.Poll(4, 1100, true, true) == RecallDispatch::Wait, "teleport waits for the menu to close");
    Check(request.Poll(4, 1200, false, true) == RecallDispatch::Run, "teleport runs after menu close");
    Check(request.Poll(4, 1201, false, true) == RecallDispatch::None, "teleport only executes once");
    request.Request(4, 2000);
    Check(request.Poll(5, 2100, false, true) == RecallDispatch::Cancel,
          "save change cancels queued teleport");
    request.Request(5, 3000);
    Check(request.Poll(5, 8001, true, true) == RecallDispatch::Cancel,
          "a blocked request expires instead of teleporting much later");
    request.Request(5, 9000);
    Check(request.Poll(5, 9100, false, false) == RecallDispatch::Cancel,
          "combat or unsafe player state cancels the request");
    for (auto screen : {std::pair{1280.0F, 720.0F}, std::pair{1920.0F, 1080.0F}, std::pair{3440.0F, 1440.0F},
                        std::pair{3840.0F, 2160.0F}}) {
        const float width = Wheel::Width(screen.first, screen.second);
        const auto left = Wheel::UtilityBox(0, width), right = Wheel::UtilityBox(1, width);
        Check(width < screen.first && width * Wheel::windowHeight < screen.second,
              "complete menu stays on screen");
        Check(left.y > width * .91F && left.x + left.w < right.x,
              "utility cards cannot overlap radial mouse target or each other");
    }
    FollowerCatchup c;
    for (int i = 0; i < 19; ++i) {
        Check(!c.Update(.1F, true, 4500, 4000), "must remain distant for two seconds");
    }
    Check(c.Update(.11F, true, 4500, 4000), "sustained separation recalls");
    c.Attempt(true);
    for (int i = 0; i < 149; ++i) {
        Check(!c.Update(.1F, true, 4500, 4000), "cooldown prevents repeated recalls");
    }
    Check(c.Update(.2F, true, 4500, 4000), "recall available after cooldown");
    c.Attempt(false);
    Check(!c.Update(1, true, 4500, 4000), "failed landing backs off");
    Check(!c.Update(1, false, 4500, 4000), "unsafe state clears delay");
    Check(!c.Update(1, true, 4500, 4000), "resumption earns a fresh delay");
    Check(!c.Update(1, true, 3999, 4000), "returning within distance cancels recall");
    Check(!c.Update(1, true, std::numeric_limits<float>::quiet_NaN(), 4000), "bad position never recalls");
    Check(!c.Update(1, true, 4500, 4000), "bad position reset delay");
    Check(c.Update(1, true, 4500, 4000), "valid distance eventually recalls");
    Wheel::Motion m;
    m.Open(0);
    float last = 0;
    for (int i = 0; i < 40; ++i) {
        m.Update(.01F, 0);
        Check(m.visibility >= last && m.visibility <= 1, "opening is monotonic and bounded");
        last = m.visibility;
    }
    m.Update(.016F, 3);
    Check(m.centerWeight[0] > 0 && m.centerWeight[3] > 0, "center crossfades instead of switching");
    float sum = 0;
    for (auto weight : m.centerWeight) {
        sum += weight;
    }
    Check(std::abs(sum - 1) < .0001F, "center maintains opacity through transitions");
    m.Close();
    last = m.visibility;
    for (int i = 0; i < 20; ++i) {
        m.Close();
        m.Update(.01F, 3);
        Check(m.visibility <= last && m.visibility >= 0, "repeated cancel never restarts close");
        last = m.visibility;
    }
    Check(m.Finished() && m.visibility == 0, "exit reaches invisible before closing");
    m.Open(1);
    m.Update(.01F, 1);
    m.Close();
    m.Update(.01F, 1);
    Check(m.visibility < .1F, "cancel during opening does not flash to full opacity");
    m.Open(0);
    Check(!m.closing && !m.Finished(), "reopening resets close state");
    Wheel::Motion a, b;
    a.Open(0);
    b.Open(0);
    for (int i = 0; i < 30; ++i) {
        a.Update(1.0F / 30, 1);
    }
    for (int i = 0; i < 144; ++i) {
        b.Update(1.0F / 144, 1);
    }
    Check(std::abs(a.highlight[1] - b.highlight[1]) < .0001F,
          "focus smoothing is independent of refresh rate");
    a.Open(0);
    a.Update(.016F, 1);
    Check(Wheel::Difference(a.angle, Wheel::Angle(0)) < .4F,
          "indicator takes the short path across angle wrap");
    std::cout << "Catch-up timing and wheel motion checks passed\n";
}

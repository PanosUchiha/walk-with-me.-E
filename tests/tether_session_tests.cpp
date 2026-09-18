#include "tether_session.h"
#include <iostream>
#include <limits>
int main()
{
    using namespace Wayfarer::HandHolding;
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::cerr << what << '\n';
            ++failures;
        }
    };
    auto finish = [&](TetherSession& s, TetherOp op, bool ok = true) {
        check(s.Begin(op), "expected serial operation");
        check(!s.Next(), "no concurrent physics operation");
        check(s.Complete(op, ok), "matching callback accepted");
    };
    TetherSession s;
    s.Start();
    check(!s.Begin(TetherOp::Connect), "constraint waits for both actors");
    finish(s, TetherOp::AddPlayer);
    finish(s, TetherOp::AddFollower);
    finish(s, TetherOp::Connect);
    check(s.connected && !s.Next(), "successful constraint holds without repeated writes");
    s.Cancel();
    check(!s.connected, "speed/grip stops immediately on cancellation");
    finish(s, TetherOp::Disconnect);
    finish(s, TetherOp::RemoveFollower);
    finish(s, TetherOp::RemovePlayer);
    check(s.Done(), "both actors cleaned after constraint removal");
    for (int at = 0; at < 3; ++at) {
        s.Start();
        for (int i = 0; i < at; ++i) {
            finish(s, static_cast<TetherOp>(i));
        }
        const auto op = static_cast<TetherOp>(at);
        s.Begin(op);
        s.Cancel();
        check(!s.Next(), "cancellation waits for pending callback");
        s.Complete(op, true);
        check(!s.connected, "late successful connect cannot reactivate cancelled grip");
        while (const auto cleanup = s.Next()) {
            check(static_cast<int>(*cleanup) >= 3, "cancelled setup never advances acquisition");
            finish(s, *cleanup);
        }
        check(s.Done(), "cancellation cleans exactly the resources acquired");
    }
    for (int failed = 0; failed < 3; ++failed) {
        s.Start();
        for (int i = 0; i < failed; ++i) {
            finish(s, static_cast<TetherOp>(i));
        }
        finish(s, static_cast<TetherOp>(failed), false);
        while (const auto cleanup = s.Next()) {
            finish(s, *cleanup);
        }
        check(s.Done(), "failed acquisition rolls back earlier operations");
    }
    s.Start();
    s.Begin(TetherOp::AddPlayer);
    check(TetherStepCurrent(s, 12, 12, 0), "current acquisition admitted");
    check(!TetherStepCurrent(s, 13, 12, 0),
          "old save/session ticket rejected even for matching actors and operation");
    check(!TetherStepCurrent(s, 12, 12, 1), "future operation cannot bypass serialized setup");
    check(!s.Complete(TetherOp::Connect, true) && s.pending == TetherOp::AddPlayer,
          "wrong or duplicate callback cannot change ownership");
    s.Complete(TetherOp::AddPlayer, true);
    check(!s.Complete(TetherOp::AddPlayer, true), "duplicate callback ignored");
    s.Cancel();
    finish(s, TetherOp::RemovePlayer, false);
    check(s.Done(), "unavailable old skeleton does not block cleanup forever");
    s.Start();
    s.Begin(TetherOp::AddPlayer);
    s.Cancel();
    check(!TetherStepCurrent(s, 12, 12, 0),
          "cancelled queued acquisition denied immediately before physics call");
    s.Complete(TetherOp::AddPlayer, true);
    s.Begin(TetherOp::RemovePlayer);
    check(TetherStepCurrent(s, 12, 12, 5), "cancelled session still permits owned teardown");
    TetherArrivalRadius radius;
    check(radius.Apply(65, true) == 5 && radius.Apply(5, true) == 5,
          "hand approach uses precise arrival without losing original radius");
    check(radius.Apply(5, false) == 65 && !radius.original, "release restores ordinary formation arrival");
    radius.Apply(80, true);
    check(radius.Apply(17, false) == 17, "release preserves a subsequent external radius change");
    check(TetherPlayerSpeed(20, 45, 100) == 1, "slack grip keeps ordinary player speed");
    check(std::abs(TetherPlayerSpeed(72.5F, 45, 100) - .775F) < .0001F, "speed interpolates with separation");
    check(TetherPlayerSpeed(200, 45, 100) == .55F, "speed floor bounded");
    check(TetherPlayerSpeed(std::numeric_limits<float>::quiet_NaN(), 45, 100) == 1,
          "invalid separation cannot poison movement speed");
    return failures ? 1 : 0;
}
